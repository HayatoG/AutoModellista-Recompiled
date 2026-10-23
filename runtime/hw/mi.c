/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/mi.h"

#include <string.h>

#include "am/bus.h"

/* Dolphin MemoryInterface.cpp registra 0x00..0x5A como registradores de 16 bits só de armazenamento (regiões de
 * proteção, tipo de proteção, máscara/flag de IRQ, endereço da violação, timers) e divide os acessos de 32 bits em
 * duas metades. A proteção de memória em si não é emulada (o Dolphin também não a aplica); o jogo só configura. */
#define MI_REG_LAST 0x5AU

static struct {
    u16 regs[(MI_REG_LAST / 2U) + 1U];
} s_mi;

void mi_reset(void) {
    memset(&s_mi, 0, sizeof(s_mi));
}

static bool valid(u32 offset, u8 size) {
    return (size == 2U || size == 4U) && (offset & (size - 1U)) == 0U && offset + size - 2U <= MI_REG_LAST;
}

u64 mi_read(u32 offset, u8 size) {
    if (!valid(offset, size)) {
        bus_unhandled_read(BUS_CPU_ADDR(MI_BASE + offset), size);
    }
    if (size == 2U) {
        return s_mi.regs[offset / 2U];
    }
    return ((u32)s_mi.regs[offset / 2U] << 16) | s_mi.regs[(offset / 2U) + 1U];
}

void mi_write(u32 offset, u64 value, u8 size) {
    if (!valid(offset, size)) {
        bus_unhandled_write(BUS_CPU_ADDR(MI_BASE + offset), value, size);
    }
    if (size == 2U) {
        s_mi.regs[offset / 2U] = (u16)value;
        return;
    }
    s_mi.regs[offset / 2U] = (u16)(value >> 16);
    s_mi.regs[(offset / 2U) + 1U] = (u16)value;
}
