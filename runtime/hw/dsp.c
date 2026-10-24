/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Interface do DSP como o Dolphin 2609a a apresenta com DSP HLE (Core/HW/DSP.cpp + DSPHLE/DSPHLE.cpp): registrador
 * de controle, mailboxes, DMA de ARAM e DMA de áudio (AI). O que roda "dentro" do DSP (protocolo dos microcódigos)
 * está em audio/dsp_hle.c. */
#include "am/dsp.h"

#include <string.h>

#include "am/bus.h"
#include "am/dsp_hle.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/timebase.h"

/* Bits que pertencem ao emulador do DSP (Dolphin DSP.h: DSP_CONTROL_MASK); o resto é da interface. */
#define DSP_CONTROL_MASK 0x0C07U
#define DSP_CSR_PAD      0xF000U
#define DSP_INT_BITS     (DSP_CSR_AID | DSP_CSR_ARAM | DSP_CSR_DSP)

/* Dolphin Do_ARAM_DMA: (bytes / 32) * 246 ciclos de CPU por transferência (medido em hardware). */
#define DSP_ARAM_DMA_CYCLES_PER_32B 246U
/* DSPHLE::DSP_WriteControlRegister: DSPInitCode some 130 tiques de TB depois de DSPInit ir a 0. */
#define DSP_INIT_CODE_TICKS 130U

/* SystemTimers::GetAudioDMACallbackPeriod a 32 kHz: 486 MHz * 3372 / (108 MHz * 4 / 32) ciclos por bloco de 32
 * bytes (8 amostras estéreo de 16 bits). */
#define DSP_AUDIO_DMA_PERIOD ((486000000ULL * 3372ULL) / (108000000ULL * 4ULL / 32ULL))

static struct {
    u16 control;     /* bits da interface (fora de DSP_CONTROL_MASK) */
    u16 hle_control; /* bits do "emulador" (DSPHLE::m_dsp_control) */
    u64 init_code_clear_tb;
    u32 cpu_mailbox;
    u16 aram_info;
    u16 aram_mode;
    u16 aram_refresh;
    u32 dma_mm;
    u32 dma_ar;
    u32 dma_cnt;
    bool dma_busy;
    u64 dma_done_cycle;
    u64 dma_count;
    u64 dma_hsp;
    /* DMA de áudio (AI): blocos de 32 bytes lidos da MRAM a 32 kHz. */
    u32 audio_start;
    u16 audio_control; /* bit 15 habilita, bits 0-14 blocos */
    u32 audio_current;
    u32 audio_remaining;
    u64 audio_next_cycle;
    u64 audio_blocks;
    u64 audio_interrupts;
    bool audio_enable_irq;
    u64 audio_enable_irq_cycle;
    DspAudioSink sink;
    void *sink_user;
} s_dsp;

void dsp_set_audio_sink(DspAudioSink sink, void *user) {
    s_dsp.sink = sink;
    s_dsp.sink_user = user;
}

void dsp_reset(void) {
    const DspAudioSink sink = s_dsp.sink;
    void *sink_user = s_dsp.sink_user;
    memset(&s_dsp, 0, sizeof(s_dsp));
    s_dsp.sink = sink;
    s_dsp.sink_user = sink_user;
    /* DSPManager::Reinit e DSPHLE::Initialize: DSP parado, DSPInit em 1, microcódigo da ROM. */
    s_dsp.control = DSP_CSR_HALT;
    s_dsp.hle_control = DSP_CSR_HALT | DSP_CSR_INIT;
    dsp_hle_reset();
    s_dsp.aram_mode = 1; /* "ARAM Controller has init'd" */
    s_dsp.aram_refresh = 156;
    s_dsp.audio_next_cycle = timebase_cycles(NULL) + DSP_AUDIO_DMA_PERIOD;
}

static void update_interrupts(void) {
    /* Cada bit de interrupção tem a máscara logo à esquerda (Dolphin DSPManager::UpdateInterrupts). */
    const u16 c = s_dsp.control;
    pi_set_interrupt(PI_INT_DSP, ((c >> 1) & c & DSP_INT_BITS) != 0U);
}

/* GenerateDSPInterrupt: liga o bit de causa na interface. */
static void raise(u16 cause) {
    s_dsp.control |= cause;
    update_interrupts();
}

/* AudioCommon::SendAIBuffer: 8 quadros estéreo da MRAM (R, L big-endian, a ordem do OutputSamples do AX) para a
 * saída, em L, R do host. Com a DMA desligada o Dolphin manda silêncio. */
static void send_block(bool enabled) {
    if (s_dsp.sink == NULL) {
        return;
    }
    s16 lr[DSP_AUDIO_BLOCK_FRAMES * 2U] = {0};
    if (enabled) {
        const GuestAddr at = MEM_CACHED_BASE | (s_dsp.audio_current & 0x03FFFFE0U);
        for (u32 i = 0; i < DSP_AUDIO_BLOCK_FRAMES; i++) {
            const size_t k = (size_t)i * 2U;
            lr[k] = (s16)mem_read16(NULL, at + (i * 4U) + 2U);
            lr[k + 1U] = (s16)mem_read16(NULL, at + (i * 4U));
        }
    }
    s_dsp.sink(lr, DSP_AUDIO_BLOCK_FRAMES, s_dsp.sink_user);
}

/* DSPManager::UpdateAudioDMA, chamado a cada bloco de 32 bytes: no fim dos blocos recarrega e pede AID. */
static void audio_dma_block(void) {
    const bool enabled = (s_dsp.audio_control & 0x8000U) != 0U;
    send_block(enabled);
    if (!enabled) {
        return;
    }
    s_dsp.audio_blocks++;
    if (s_dsp.audio_remaining != 0U) {
        s_dsp.audio_remaining--;
        s_dsp.audio_current += 32U;
    }
    if (s_dsp.audio_remaining == 0U) {
        s_dsp.audio_current = s_dsp.audio_start;
        s_dsp.audio_remaining = s_dsp.audio_control & 0x7FFFU;
        s_dsp.audio_interrupts++;
        raise(DSP_CSR_AID);
    }
}

void dsp_update(void) {
    const u64 now = timebase_cycles(NULL);
    if (s_dsp.dma_busy && now >= s_dsp.dma_done_cycle) {
        s_dsp.dma_busy = false;
        s_dsp.control = (u16)((s_dsp.control & ~DSP_CSR_DMA_STATE) | DSP_CSR_ARAM); /* CompleteARAM */
        update_interrupts();
    }
    while (now >= s_dsp.audio_next_cycle) {
        audio_dma_block();
        s_dsp.audio_next_cycle += DSP_AUDIO_DMA_PERIOD;
    }
    if (s_dsp.audio_enable_irq && now >= s_dsp.audio_enable_irq_cycle) {
        s_dsp.audio_enable_irq = false;
        s_dsp.audio_interrupts++;
        raise(DSP_CSR_AID);
    }
    if (dsp_hle_take_interrupt(now)) {
        raise(DSP_CSR_DSP);
    }
}

u64 dsp_next_event_cycle(void) {
    u64 next = s_dsp.audio_next_cycle;
    if (s_dsp.dma_busy && s_dsp.dma_done_cycle < next) {
        next = s_dsp.dma_done_cycle;
    }
    if (s_dsp.audio_enable_irq && s_dsp.audio_enable_irq_cycle < next) {
        next = s_dsp.audio_enable_irq_cycle;
    }
    const u64 hle = dsp_hle_next_event();
    return hle < next ? hle : next;
}

u64 dsp_audio_dma_interrupts(void) {
    return s_dsp.audio_interrupts;
}

static u16 read_control(void) {
    if ((s_dsp.hle_control & DSP_CSR_INIT_CODE) != 0U && timebase_read(NULL) >= s_dsp.init_code_clear_tb) {
        s_dsp.hle_control &= (u16)~DSP_CSR_INIT_CODE;
    }
    return (u16)((s_dsp.control & ~DSP_CONTROL_MASK) | (s_dsp.hle_control & DSP_CONTROL_MASK));
}

/* DSPHLE::DSP_WriteControlRegister. */
static u16 hle_write_control(u16 value) {
    u16 v = value;
    if (((s_dsp.hle_control ^ v) & DSP_CSR_HALT) != 0U) {
        dsp_hle_set_halted((v & DSP_CSR_HALT) != 0U);
    }
    if ((v & DSP_CSR_RESET) != 0U) {
        dsp_hle_boot_rom();
        v &= (u16)~DSP_CSR_RESET;
    }
    if ((s_dsp.hle_control & DSP_CSR_INIT) != 0U && (v & DSP_CSR_INIT) == 0U) {
        dsp_hle_boot_init();
        v |= DSP_CSR_INIT_CODE;
        s_dsp.init_code_clear_tb = timebase_read(NULL) + DSP_INIT_CODE_TICKS;
    }
    s_dsp.hle_control = v;
    return v;
}

/* DSP_CONTROL na interface (Dolphin DSP.cpp, ComplexWrite de DSP_CONTROL). */
static void write_control(u16 value) {
    if ((value & DSP_CSR_PAD) != 0U) {
        am_fatal("DSP_CONTROL: valor 0x%04X com bits reservados (Dolphin também acusa)", value);
    }
    const u16 tmp = (u16)((value & ~DSP_CONTROL_MASK) | (hle_write_control(value) & DSP_CONTROL_MASK));
    const u16 keep = (u16)(s_dsp.control & (DSP_CSR_DMA_STATE | DSP_INT_BITS));
    const u16 cleared = (u16)(keep & ~(tmp & DSP_INT_BITS)); /* escrever 1 num bit de interrupção o apaga */
    const u16 taken = (u16)(tmp & ~(DSP_CSR_DMA_STATE | DSP_INT_BITS));
    s_dsp.control = (u16)(taken | cleared);
    update_interrupts();
}

/* Do_ARAM_DMA: a cópia é feita na hora; o fim (DMAState=0, interrupção ARAM) vem depois do tempo medido. Endereços
 * espelhados a cada 64 MiB; acima dos 16 MiB da ARAM a DMA vai para a porta HSP, vazia no GameCube padrão do Dolphin
 * (CHSPDevice_Null: leitura não altera a MRAM, escrita é descartada) — é assim que o ARInit (__ARChecksize) mede o
 * tamanho da ARAM. Com AR_INFO modo 4, escritas abaixo de 4 MiB são espelhadas em +4 MiB. */
static void start_aram_dma(void) {
    const bool to_mram = (s_dsp.dma_cnt & 0x80000000U) != 0U;
    const u32 count = s_dsp.dma_cnt & 0x7FFFFFFFU;
    const u32 mm = s_dsp.dma_mm & 0x03FFFFFFU;
    const u32 ar = s_dsp.dma_ar & 0x03FFFFFFU;
    s_dsp.dma_count++;
    if (ar >= MEM_ARAM_SIZE) {
        s_dsp.dma_hsp++;
    } else {
        if (ar + count > MEM_ARAM_SIZE) {
            am_fatal("DMA de ARAM atravessa o fim dos 16 MiB: ar=0x%08X cnt=0x%X", ar, count);
        }
        const bool mirror = (s_dsp.aram_info & 0xFU) == 4U;
        for (u32 i = 0; i < count; i += 4U) {
            if (to_mram) {
                mem_write32(NULL, MEM_CACHED_BASE + mm + i, mem_aram_read32(ar + i));
            } else {
                const u32 word = mem_read32(NULL, MEM_CACHED_BASE + mm + i);
                mem_aram_write32(ar + i, word);
                if (mirror && ar + i < 0x400000U) {
                    mem_aram_write32(ar + i + 0x400000U, word);
                }
            }
        }
    }
    s_dsp.control |= DSP_CSR_DMA_STATE;
    s_dsp.dma_busy = true;
    s_dsp.dma_done_cycle = timebase_cycles(NULL) + ((u64)(count / 32U) * DSP_ARAM_DMA_CYCLES_PER_32B);
    s_dsp.dma_cnt &= 0x80000000U; /* Dolphin decrementa Cnt.count até 0 */
}

static u16 read16(u32 offset) {
    dsp_update();
    switch (offset) {
    case DSP_REG_MAIL_TO_DSP_HI:
        return (u16)(s_dsp.cpu_mailbox >> 16);
    case DSP_REG_MAIL_TO_DSP_LO:
        return (u16)s_dsp.cpu_mailbox;
    case DSP_REG_MAIL_FROM_DSP_HI:
        return dsp_hle_read_mail_high();
    case DSP_REG_MAIL_FROM_DSP_LO: {
        const u16 lo = dsp_hle_read_mail_low();
        dsp_update(); /* a leitura pode ter liberado a interrupção do mail seguinte */
        return lo;
    }
    case DSP_REG_AUDIO_START_HI:
        return (u16)(s_dsp.audio_start >> 16);
    case DSP_REG_AUDIO_START_LO:
        return (u16)s_dsp.audio_start;
    case DSP_REG_AUDIO_CONTROL:
        return s_dsp.audio_control;
    case DSP_REG_AUDIO_BLOCKS_LEFT:
        return (u16)(s_dsp.audio_remaining > 0U ? s_dsp.audio_remaining - 1U : 0U); /* contagem a partir de zero */
    case DSP_REG_CONTROL:
        return read_control();
    case DSP_REG_AR_INFO:
        return s_dsp.aram_info;
    case DSP_REG_AR_MODE:
        return s_dsp.aram_mode;
    case DSP_REG_AR_REFRESH:
        return s_dsp.aram_refresh;
    case DSP_REG_AR_MMADDR_H:
        return (u16)(s_dsp.dma_mm >> 16);
    case DSP_REG_AR_MMADDR_L:
        return (u16)s_dsp.dma_mm;
    case DSP_REG_AR_ARADDR_H:
        return (u16)(s_dsp.dma_ar >> 16);
    case DSP_REG_AR_ARADDR_L:
        return (u16)s_dsp.dma_ar;
    case DSP_REG_AR_CNT_H:
        return (u16)(s_dsp.dma_cnt >> 16);
    case DSP_REG_AR_CNT_L:
        return (u16)s_dsp.dma_cnt;
    default:
        bus_unhandled_read(BUS_CPU_ADDR(DSP_BASE + offset), 2);
    }
}

static u32 set_hi(u32 reg, u16 v, u16 mask) {
    return (reg & 0x0000FFFFU) | ((u32)(v & mask) << 16);
}

static u32 set_lo(u32 reg, u16 v, u16 mask) {
    return (reg & 0xFFFF0000U) | (u32)(v & mask);
}

/* Registradores da DMA de áudio (Dolphin DSP.cpp: AUDIO_DMA_START_HI/LO, AUDIO_DMA_CONTROL_LEN). Só carrega
 * endereço e blocos se a DMA estava parada; senão entram no fim da transferência atual. O Dolphin pede uma AID 200
 * ciclos depois de ligar (Sky Crawlers trava com menos). */
static void write_audio_dma(u32 offset, u16 v) {
    if (offset == DSP_REG_AUDIO_START_HI) {
        s_dsp.audio_start = set_hi(s_dsp.audio_start, v, 0x03FFU);
        return;
    }
    if (offset == DSP_REG_AUDIO_START_LO) {
        s_dsp.audio_start = set_lo(s_dsp.audio_start, v, 0xFFE0U);
        return;
    }
    const bool was_enabled = (s_dsp.audio_control & 0x8000U) != 0U;
    s_dsp.audio_control = v;
    if (!was_enabled && (v & 0x8000U) != 0U) {
        s_dsp.audio_current = s_dsp.audio_start;
        s_dsp.audio_remaining = v & 0x7FFFU;
        s_dsp.audio_enable_irq_cycle = timebase_cycles(NULL) + 200U;
        s_dsp.audio_enable_irq = true;
    }
}

/* Máscaras de escrita do Dolphin DSPManager::RegisterMMIO (WMASK_*). */
static void write16(u32 offset, u16 v) {
    switch (offset) {
    case DSP_REG_MAIL_TO_DSP_HI:
        s_dsp.cpu_mailbox = set_hi(s_dsp.cpu_mailbox, v, 0xFFFFU);
        return;
    case DSP_REG_MAIL_TO_DSP_LO:
        /* DSP_WriteMailBoxLow: o microcódigo trata na hora e o bit 31 apaga ("mail recebido"). */
        s_dsp.cpu_mailbox = set_lo(s_dsp.cpu_mailbox, v, 0xFFFFU);
        dsp_hle_mail_to_dsp(s_dsp.cpu_mailbox);
        s_dsp.cpu_mailbox &= 0x7FFFFFFFU;
        dsp_update();
        return;
    case DSP_REG_AUDIO_START_HI:
    case DSP_REG_AUDIO_START_LO:
    case DSP_REG_AUDIO_CONTROL:
        write_audio_dma(offset, v);
        return;
    case DSP_REG_CONTROL:
        write_control(v);
        return;
    case DSP_REG_AR_INFO:
        s_dsp.aram_info = (u16)(v & 0x007FU);
        return;
    case DSP_REG_AR_REFRESH:
        s_dsp.aram_refresh = (u16)(v & 0x07FFU);
        return;
    case DSP_REG_AR_MMADDR_H:
        s_dsp.dma_mm = set_hi(s_dsp.dma_mm, v, 0x03FFU);
        return;
    case DSP_REG_AR_MMADDR_L:
        s_dsp.dma_mm = set_lo(s_dsp.dma_mm, v, 0xFFE0U);
        return;
    case DSP_REG_AR_ARADDR_H:
        s_dsp.dma_ar = set_hi(s_dsp.dma_ar, v, 0x03FFU);
        return;
    case DSP_REG_AR_ARADDR_L:
        s_dsp.dma_ar = set_lo(s_dsp.dma_ar, v, 0xFFE0U);
        return;
    case DSP_REG_AR_CNT_H:
        s_dsp.dma_cnt = set_hi(s_dsp.dma_cnt, v, 0x83FFU);
        return;
    case DSP_REG_AR_CNT_L:
        s_dsp.dma_cnt = set_lo(s_dsp.dma_cnt, v, 0xFFE0U);
        start_aram_dma();
        return;
    default:
        bus_unhandled_write(BUS_CPU_ADDR(DSP_BASE + offset), v, 2);
    }
}

/* 32 bits = duas metades de 16, a alta primeiro (Dolphin ReadToSmaller/WriteToSmaller). */
u64 dsp_read(u32 offset, u8 size) {
    if (size == 2U && (offset & 1U) == 0U) {
        return read16(offset);
    }
    if (size == 4U && (offset & 3U) == 0U) {
        const u32 hi = read16(offset);
        return (hi << 16) | read16(offset + 2U);
    }
    bus_unhandled_read(BUS_CPU_ADDR(DSP_BASE + offset), size);
}

void dsp_write(u32 offset, u64 value, u8 size) {
    if (size == 2U && (offset & 1U) == 0U) {
        write16(offset, (u16)value);
        return;
    }
    if (size == 4U && (offset & 3U) == 0U) {
        write16(offset, (u16)(value >> 16));
        write16(offset + 2U, (u16)value);
        return;
    }
    bus_unhandled_write(BUS_CPU_ADDR(DSP_BASE + offset), value, size);
}
