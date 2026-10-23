/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/vi.h"

#include <string.h>

#include "am/bus.h"
#include "am/log.h"
#include "am/pi.h"
#include "am/si.h"
#include "am/timebase.h"

/* Dolphin VideoInterface.cpp: CLOCK_FREQUENCIES e NUM_HALF_LINES_FOR_SI_POLL ("quanto dura uma leitura do SI"). */
#define VI_CLOCK_27MHZ            27000000ULL
#define VI_CLOCK_54MHZ            54000000ULL
#define VI_CPU_TICKS_PER_SECOND   486000000ULL /* SystemTimers: GetTicksPerSecond do GameCube */
#define VI_HALF_LINES_FOR_SI_POLL ((7U * 2U) + 1U)
#define VI_REG_COUNT              ((VI_REG_LAST / 2U) + 1U)

static struct {
    u16 regs[VI_REG_COUNT];
    u32 half_line;
    u32 next_si_poll;
    u64 next_cycle;       /* ciclo de CPU da próxima meia-linha */
    u64 line_start_cycle; /* início da linha atual (para HCOUNT) */
    u64 fields;
} s_vi;

static u16 reg(u32 offset) {
    return s_vi.regs[offset / 2U];
}

static void set_reg(u32 offset, u16 v) {
    s_vi.regs[offset / 2U] = v;
}

static u32 reg32(u32 hi_offset) {
    return ((u32)reg(hi_offset) << 16) | reg(hi_offset + 2U);
}

/* GetTicksPerSample * HLW; com HLW = 0 (antes do VIInit) usa 1 para não dividir por zero. */
u64 vi_ticks_per_half_line(void) {
    const u64 clock = (reg(VI_REG_CLOCK) & 1U) != 0U ? VI_CLOCK_54MHZ : VI_CLOCK_27MHZ;
    const u64 hlw = reg(VI_REG_HTR0_LO) & 0x3FFU;
    return (2ULL * VI_CPU_TICKS_PER_SECOND / clock) * (hlw != 0U ? hlw : 1U);
}

/* GetHalfLinesPerOddField/EvenField: 3*EQU + PRB + 2*ACV + PSB. */
static u32 half_lines_per_field(u32 vt_hi) {
    const u32 vtr = reg(VI_REG_VTR);
    const u32 equ = vtr & 0xFU;
    const u32 acv = (vtr >> 4) & 0x3FFU;
    const u32 psb = reg(vt_hi) & 0x3FFU;
    const u32 prb = reg(vt_hi + 2U) & 0x3FFU;
    return (3U * equ) + prb + (2U * acv) + psb;
}

/* Preset(_bNTSC): valores que o IPL deixa (o jogo reprograma tudo no VIInit). */
void vi_reset(bool ntsc) {
    memset(&s_vi, 0, sizeof(s_vi));
    set_reg(VI_REG_VTR, 6U);                           /* EQU = 6, ACV = 0 */
    set_reg(VI_REG_DCR, ntsc ? 0x0001U : 0x0101U);     /* ENB = 1, FMT = 0 (NTSC) ou 1 (PAL) */
    set_reg(VI_REG_HTR0_HI, (u16)((71U << 8) | 105U)); /* HCS = 71, HCE = 105 */
    set_reg(VI_REG_HTR0_LO, 429U);                     /* HLW */
    set_reg(0x08U, 0x02EAU);                           /* HTR1: HSY = 64, HBE640 = 162, HBS640 = 373 (Hex 0x02EA5140) */
    set_reg(0x0AU, 0x5140U);
    set_reg(VI_REG_VTO_HI, 5U);
    set_reg(VI_REG_VTO_LO, 502U);
    set_reg(VI_REG_VTE_HI, 4U);
    set_reg(VI_REG_VTE_LO, 503U);
    set_reg(0x14U, (u16)((520U << 5) | 12U)); /* BBOI: BS2/BE2 na metade alta, BS0/BE0 na baixa */
    set_reg(0x16U, (u16)((520U << 5) | 12U));
    set_reg(0x18U, (u16)((519U << 5) | 13U)); /* BBEI */
    set_reg(0x1AU, (u16)((519U << 5) | 13U));
    set_reg(VI_REG_DI0_HI, (u16)(VI_DI_MASK_HI | 263U)); /* DI0: VCT 263, HCT 430, máscara */
    set_reg(VI_REG_DI0_HI + 2U, 430U);
    set_reg(VI_REG_DI0_HI + 4U, (u16)(VI_DI_MASK_HI | 1U)); /* DI1: VCT 1, HCT 1 */
    set_reg(VI_REG_DI0_HI + 6U, 1U);
    set_reg(0x48U, (u16)((40U << 8) | 40U)); /* PICCONF: STD = WPL = 40 */
    set_reg(VI_REG_CLOCK, ntsc ? 1U : 0U);   /* 54 MHz "capable of progressive scan" */
    s_vi.next_si_poll = VI_HALF_LINES_FOR_SI_POLL;
    s_vi.next_cycle = timebase_cycles(NULL) + vi_ticks_per_half_line();
}

static void update_interrupts(void) {
    bool active = false;
    for (u32 i = 0; i < VI_DI_COUNT; i++) {
        const u16 hi = reg(VI_REG_DI0_HI + (i * 4U));
        active = active || ((hi & VI_DI_INT_HI) != 0U && (hi & VI_DI_MASK_HI) != 0U);
    }
    pi_set_interrupt(PI_INT_VI, active);
}

/* SI: SIPOLL.X em linhas (Dolphin SerialInterface::GetPollXLines). */
static void si_poll_step(bool field_boundary) {
    if (s_vi.half_line == s_vi.next_si_poll) {
        si_update_devices();
        s_vi.next_si_poll += 2U * si_poll_x_lines();
    }
    if (field_boundary) {
        s_vi.next_si_poll = s_vi.half_line + VI_HALF_LINES_FOR_SI_POLL;
    }
}

/* VideoInterfaceManager::Update, uma meia-linha. */
static void step_half_line(u64 cycle) {
    const u32 odd = half_lines_per_field(VI_REG_VTO_HI);
    const u32 even = half_lines_per_field(VI_REG_VTE_HI);
    const bool boundary = s_vi.half_line == 0U || s_vi.half_line == odd;
    if (boundary) {
        s_vi.fields++;
    }
    si_poll_step(boundary);
    s_vi.half_line++;
    if (s_vi.half_line >= odd + even) {
        s_vi.half_line = 0;
    }
    if ((s_vi.half_line & 1U) == 0U) {
        s_vi.line_start_cycle = cycle;
    }
    const u32 hlw = reg(VI_REG_HTR0_LO) & 0x3FFU;
    for (u32 i = 0; i < VI_DI_COUNT; i++) {
        const u32 off = VI_REG_DI0_HI + (i * 4U);
        const u32 di = reg32(off);
        const u32 vct = (di >> 16) & 0x7FFU;
        const u32 hct = di & 0x7FFU;
        const u32 target = hct > hlw ? 1U : 0U;
        if (1U + (s_vi.half_line / 2U) == vct && (s_vi.half_line & 1U) == target) {
            set_reg(off, (u16)(reg(off) | VI_DI_INT_HI));
        }
    }
    update_interrupts();
}

void vi_update(void) {
    const u64 now = timebase_cycles(NULL);
    while (now >= s_vi.next_cycle) {
        const u64 at = s_vi.next_cycle;
        step_half_line(at);
        s_vi.next_cycle = at + vi_ticks_per_half_line();
    }
}

u64 vi_next_event_cycle(void) {
    return s_vi.next_cycle;
}

u64 vi_field_count(void) {
    return s_vi.fields;
}

u32 vi_half_line(void) {
    return s_vi.half_line;
}

/* GetXFBAddressTop: com POFF o FBB está em unidades de 32 bytes. */
u32 vi_xfb_top(void) {
    const u32 v = reg32(VI_REG_TFBL_HI);
    return (v & 0x10000000U) != 0U ? (v & 0x00FFFFFFU) << 5 : (v & 0x00FFFFFFU);
}

static u16 read16(u32 offset) {
    vi_update();
    switch (offset) {
    case VI_REG_VCOUNT:
        return (u16)(1U + (s_vi.half_line / 2U));
    case VI_REG_HCOUNT: {
        const u64 hlw = reg(VI_REG_HTR0_LO) & 0x3FFU;
        const u64 elapsed = timebase_cycles(NULL) - s_vi.line_start_cycle;
        u64 value = 1U + (hlw * elapsed / vi_ticks_per_half_line());
        if (value > hlw * 2U) {
            value = hlw * 2U;
        }
        return (u16)(value < 1U ? 1U : value);
    }
    default:
        return reg(offset);
    }
}

/* Bits que o Dolphin copia na escrita de VI_CONTROL_REGISTER; RST zera os registradores de interrupção. */
static void write_control(u16 v) {
    const u16 keep = 0x03FDU; /* ENB, NIN, DLR, LE0, LE1, FMT */
    set_reg(VI_REG_DCR, (u16)(v & keep));
    if ((v & VI_DCR_RST) != 0U) {
        for (u32 off = VI_REG_DI0_HI; off <= VI_REG_DI3_LO; off += 2U) {
            set_reg(off, 0);
        }
        update_interrupts();
    }
}

static void write16(u32 offset, u16 v) {
    vi_update();
    switch (offset) {
    case VI_REG_DCR:
        write_control(v);
        return;
    case VI_REG_VCOUNT:
    case VI_REG_HCOUNT:
        am_fatal("VI: escrita 0x%04X na posição do feixe (0x%02X): não documentado (Dolphin só avisa)", v, offset);
    case VI_REG_TFBL_HI:
    case VI_REG_BFBL_HI:
    case 0x20U: /* RFBL_HI e RFBR_HI (3D) */
    case 0x28U:
        /* CLRPOFF (bits 29-31): escrever 1 apaga POFF. */
        set_reg(offset, (v & 0x8000U) != 0U ? (u16)(v & ~0x1000U) : v);
        return;
    default:
        break;
    }
    set_reg(offset, v);
    if (offset >= VI_REG_DI0_HI && offset <= VI_REG_DI3_LO) {
        update_interrupts();
    }
}

u64 vi_read(u32 offset, u8 size) {
    if (offset > VI_REG_LAST + 1U || (offset & (size - 1U)) != 0U) {
        bus_unhandled_read(BUS_CPU_ADDR(VI_BASE + offset), size);
    }
    if (size == 2U) {
        return read16(offset);
    }
    if (size == 4U) {
        const u32 hi = read16(offset);
        return (hi << 16) | read16(offset + 2U);
    }
    if (size == 1U) { /* ReadToLarger: metade do registrador de 16 bits */
        const u16 v = read16(offset & ~1U);
        return (offset & 1U) != 0U ? (v & 0xFFU) : (u32)(v >> 8);
    }
    bus_unhandled_read(BUS_CPU_ADDR(VI_BASE + offset), size);
}

void vi_write(u32 offset, u64 value, u8 size) {
    if (offset > VI_REG_LAST + 1U || (offset & (size - 1U)) != 0U || (size != 2U && size != 4U)) {
        bus_unhandled_write(BUS_CPU_ADDR(VI_BASE + offset), value, size);
    }
    if (size == 2U) {
        write16(offset, (u16)value);
        return;
    }
    write16(offset, (u16)(value >> 16)); /* WriteToSmaller: a metade alta primeiro */
    write16(offset + 2U, (u16)value);
}
