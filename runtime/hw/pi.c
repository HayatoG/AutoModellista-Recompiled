/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/pi.h"

#include <string.h>

#include "am/bus.h"

static struct {
    u32 cause;
    u32 mask;
    u32 fifo_base;
    u32 fifo_end;
    u32 fifo_wptr;
    u32 reset_code;
    u64 deliveries[16];
} s_pi;

/* Dolphin ProcessorInterfaceManager::Init: causa = botão de reset solto + VI (o VI já pede interrupção ao ligar; fica
 * mascarado até o VIInit do jogo). Código de reset 0 = cold reset. */
void pi_reset(void) {
    s_pi.cause = PI_INT_RST_BUTTON | PI_INT_VI;
    s_pi.mask = 0;
    s_pi.fifo_base = 0;
    s_pi.fifo_end = 0;
    s_pi.fifo_wptr = 0;
    s_pi.reset_code = 0;
    memset(s_pi.deliveries, 0, sizeof(s_pi.deliveries));
}

void pi_count_delivery(void) {
    const u32 active = s_pi.cause & s_pi.mask;
    for (u32 bit = 0; bit < 16U; bit++) {
        s_pi.deliveries[bit] += (active >> bit) & 1U;
    }
}

u64 pi_delivery_count(u32 bit) {
    return bit < 16U ? s_pi.deliveries[bit] : 0U;
}

static u32 read32(u32 offset) {
    switch (offset) {
    case PI_REG_INTSR:
        return s_pi.cause;
    case PI_REG_INTMR:
        return s_pi.mask;
    case PI_REG_FIFO_BASE:
        return s_pi.fifo_base;
    case PI_REG_FIFO_END:
        return s_pi.fifo_end;
    case PI_REG_FIFO_WPTR:
        return s_pi.fifo_wptr;
    case PI_REG_RESET_CODE:
        return s_pi.reset_code;
    case PI_REG_FLIPPER_REV:
        return PI_FLIPPER_REV_C;
    default:
        bus_unhandled_read(BUS_CPU_ADDR(PI_BASE + offset), 4);
    }
}

u64 pi_read(u32 offset, u8 size) {
    /* Dolphin: leituras de 16 bits são metades da leitura de 32 (ReadToLarger). */
    if (size == 4U) {
        return read32(offset);
    }
    if (size == 2U) {
        const u32 word = read32(offset & ~3U);
        return (offset & 2U) != 0U ? (word & 0xFFFFU) : (word >> 16);
    }
    bus_unhandled_read(BUS_CPU_ADDR(PI_BASE + offset), size);
}

void pi_write(u32 offset, u64 value, u8 size) {
    if (size != 4U) {
        bus_unhandled_write(BUS_CPU_ADDR(PI_BASE + offset), value, size);
    }
    const u32 v = (u32)value;
    switch (offset) {
    case PI_REG_INTSR:
        s_pi.cause &= ~v; /* escrever 1 limpa a causa (Dolphin PI_INTERRUPT_CAUSE) */
        return;
    case PI_REG_INTMR:
        s_pi.mask = v;
        return;
    case PI_REG_FIFO_BASE:
        s_pi.fifo_base = v & 0x03FFFFE0U; /* máscara física do GameCube (Dolphin CommandProcessor) */
        return;
    case PI_REG_FIFO_END:
        s_pi.fifo_end = v & 0x03FFFFE0U;
        return;
    case PI_REG_FIFO_WPTR:
        s_pi.fifo_wptr = v & 0x03FFFFE0U;
        return;
    case PI_REG_RESET_CODE:
        /* Dolphin reinicia o drive quando o bit 2 vai a 0; ainda sem DI de verdade, então só guardamos o valor se ele
         * não pedir reset do drive. */
        if ((~v & 0x4U) != 0U) {
            bus_unhandled_write(BUS_CPU_ADDR(PI_BASE + offset), value, size);
        }
        s_pi.reset_code = v;
        return;
    default:
        bus_unhandled_write(BUS_CPU_ADDR(PI_BASE + offset), value, size);
    }
}

void pi_set_interrupt(u32 cause, bool set) {
    if (set) {
        s_pi.cause |= cause;
    } else {
        s_pi.cause &= ~cause;
    }
}

u32 pi_fifo_write_pointer(void) {
    return s_pi.fifo_wptr;
}

void pi_fifo_advance(void) {
    s_pi.fifo_wptr = s_pi.fifo_wptr == s_pi.fifo_end ? s_pi.fifo_base : s_pi.fifo_wptr + 32U;
}

u32 pi_cause(void) {
    return s_pi.cause;
}

u32 pi_mask(void) {
    return s_pi.mask;
}

/* Dolphin ProcessorInterfaceManager::UpdateException: (causa & máscara) != 0. */
bool pi_external_pending(void) {
    return (s_pi.cause & s_pi.mask) != 0U;
}
