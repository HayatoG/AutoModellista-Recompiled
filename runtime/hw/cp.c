/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/cp.h"

#include <string.h>

#include "am/bus.h"
#include "am/gx_fifo.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/pi.h"

#define CP_REG_COUNT   ((CP_REG_LAST / 2U) + 1U)
#define CP_BURST_SIZE  32U
#define CP_PTR_HI_MASK 0x03FFU /* ponteiros de 26 bits (Dolphin WMASK_HI_RESTRICT no GameCube) */
#define CP_PTR_LO_MASK 0xFFE0U

static struct {
    u16 regs[CP_REG_COUNT];
    u64 bursts;
    u64 unlinked_reads;
} s_cp;

static u32 reg32(u32 lo_offset) {
    return ((u32)s_cp.regs[(lo_offset + 2U) / 2U] << 16) | s_cp.regs[lo_offset / 2U];
}

static void set_reg32(u32 lo_offset, u32 v) {
    s_cp.regs[lo_offset / 2U] = (u16)v;
    s_cp.regs[(lo_offset + 2U) / 2U] = (u16)(v >> 16);
}

void cp_reset(void) {
    memset(&s_cp, 0, sizeof(s_cp));
}

u64 cp_bursts(void) {
    return s_cp.bursts;
}

/* Abaixo da marca d'água baixa com distância 0, sempre; estouro nunca (a GPU consome na hora). */
static u16 status(void) {
    u16 sr = CP_SR_READ_IDLE | CP_SR_COMMAND_IDLE;
    if (reg32(CP_REG_FIFO_DIST) < reg32(CP_REG_FIFO_LOWM)) {
        sr |= CP_SR_UNDERFLOW;
    }
    return sr;
}

static void update_interrupts(void) {
    const u16 ctrl = s_cp.regs[CP_REG_CTRL / 2U];
    const u16 sr = status();
    pi_set_interrupt(PI_INT_CP, ((sr & CP_SR_OVERFLOW) != 0U && (ctrl & CP_CR_OVF_INT) != 0U) ||
                                    ((sr & CP_SR_UNDERFLOW) != 0U && (ctrl & CP_CR_UNF_INT) != 0U));
}

/* Ponteiro de escrita do CP avança como no Dolphin (volta à base depois do bloco no fim); o de leitura o segue. */
void cp_gather_pipe_burst(u32 phys, const u8 *data) {
    s_cp.bursts++;
    const u16 ctrl = s_cp.regs[CP_REG_CTRL / 2U];
    if ((ctrl & CP_CR_LINK) == 0U) {
        /* Sem link a FIFO da CPU não alimenta a GPU (display list em construção, GXBeginDisplayList). */
        return;
    }
    (void)phys;
    u32 wptr = reg32(CP_REG_FIFO_WPTR);
    wptr = wptr == reg32(CP_REG_FIFO_END) ? reg32(CP_REG_FIFO_BASE) : wptr + CP_BURST_SIZE;
    set_reg32(CP_REG_FIFO_WPTR, wptr);
    if ((ctrl & CP_CR_READ_ENABLE) != 0U) {
        gx_fifo_feed(data, CP_BURST_SIZE);
        set_reg32(CP_REG_FIFO_RPTR, wptr);
    }
    update_interrupts();
}

/* Com a leitura ligada e o link desligado, a GPU lê a FIFO da memória entre rptr e wptr (o jogo move wptr). */
static void consume_unlinked(void) {
    const u16 ctrl = s_cp.regs[CP_REG_CTRL / 2U];
    if ((ctrl & CP_CR_READ_ENABLE) == 0U || (ctrl & CP_CR_LINK) != 0U) {
        return;
    }
    u32 rptr = reg32(CP_REG_FIFO_RPTR);
    const u32 wptr = reg32(CP_REG_FIFO_WPTR);
    const u32 base = reg32(CP_REG_FIFO_BASE);
    const u32 end = reg32(CP_REG_FIFO_END);
    u8 block[CP_BURST_SIZE];
    while (rptr != wptr) {
        if (rptr < base || rptr > end) {
            am_fatal("CP: ponteiro de leitura 0x%08X fora da FIFO [0x%08X, 0x%08X]", rptr, base, end);
        }
        for (u32 i = 0; i < CP_BURST_SIZE; i++) {
            block[i] = mem_read8(NULL, MEM_CACHED_BASE | (rptr + i));
        }
        gx_fifo_feed(block, CP_BURST_SIZE);
        s_cp.unlinked_reads++;
        rptr = rptr == end ? base : rptr + CP_BURST_SIZE;
    }
    set_reg32(CP_REG_FIFO_RPTR, rptr);
}

static u16 read16(u32 offset) {
    switch (offset) {
    case CP_REG_STATUS:
        return status();
    case CP_REG_FIFO_DIST:
    case CP_REG_FIFO_DIST + 2U:
        return 0;
    default:
        return offset >= 0x40U ? 0U : s_cp.regs[offset / 2U]; /* contadores de desempenho: zero */
    }
}

static void write16(u32 offset, u16 v) {
    switch (offset) {
    case CP_REG_STATUS:
        am_fatal("CP: escrita 0x%04X no registrador de status (somente leitura)", v);
    case CP_REG_CLEAR:
        update_interrupts(); /* apaga estouro/falta: nunca ficam ligados */
        return;
    case CP_REG_FIFO_BASE + 2U:
    case CP_REG_FIFO_END + 2U:
    case CP_REG_FIFO_HIWM + 2U:
    case CP_REG_FIFO_LOWM + 2U:
    case CP_REG_FIFO_WPTR + 2U:
    case CP_REG_FIFO_RPTR + 2U:
    case CP_REG_FIFO_BP + 2U:
        s_cp.regs[offset / 2U] = (u16)(v & CP_PTR_HI_MASK);
        break;
    case CP_REG_FIFO_BASE:
    case CP_REG_FIFO_END:
    case CP_REG_FIFO_WPTR:
    case CP_REG_FIFO_RPTR:
    case CP_REG_FIFO_BP:
        s_cp.regs[offset / 2U] = (u16)(v & CP_PTR_LO_MASK);
        break;
    default:
        if (offset >= 0x40U) {
            bus_unhandled_write(BUS_CPU_ADDR(CP_BASE + offset), v, 2);
        }
        s_cp.regs[offset / 2U] = v;
        break;
    }
    if (offset == CP_REG_CTRL && (v & CP_CR_BP_ENABLE) != 0U) {
        am_fatal("CP: breakpoint da FIFO habilitado (CP_CTRL=0x%04X) não implementado (TODO(M6))", v);
    }
    consume_unlinked();
    update_interrupts();
}

u64 cp_read(u32 offset, u8 size) {
    if (offset > CP_REG_LAST || (offset & 1U) != 0U || (size != 2U && size != 4U)) {
        bus_unhandled_read(BUS_CPU_ADDR(CP_BASE + offset), size);
    }
    if (size == 4U) {
        return ((u32)read16(offset) << 16) | read16(offset + 2U);
    }
    return read16(offset);
}

void cp_write(u32 offset, u64 value, u8 size) {
    if (offset > CP_REG_LAST || (offset & 1U) != 0U || (size != 2U && size != 4U)) {
        bus_unhandled_write(BUS_CPU_ADDR(CP_BASE + offset), value, size);
    }
    if (size == 4U) {
        write16(offset, (u16)(value >> 16));
        write16(offset + 2U, (u16)value);
        return;
    }
    write16(offset, (u16)value);
}
