/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/pe.h"

#include <string.h>

#include "am/bus.h"
#include "am/pi.h"

#define PE_REG_COUNT ((PE_REG_LAST / 2U) + 1U)

static struct {
    u16 regs[PE_REG_COUNT];
    u16 token;
} s_pe;

void pe_reset(void) {
    memset(&s_pe, 0, sizeof(s_pe));
}

/* PixelEngineManager::UpdateInterrupts: cada causa só sobe a linha com o bit de habilitação. */
static void update_interrupts(void) {
    const u16 c = s_pe.regs[PE_REG_CTRL / 2U];
    pi_set_interrupt(PI_INT_PE_TOKEN, (c & PE_CTRL_TOKEN) != 0U && (c & PE_CTRL_TOKEN_ENABLE) != 0U);
    pi_set_interrupt(PI_INT_PE_FINISH, (c & PE_CTRL_FINISH) != 0U && (c & PE_CTRL_FINISH_ENABLE) != 0U);
}

void pe_signal_finish(void) {
    s_pe.regs[PE_REG_CTRL / 2U] |= PE_CTRL_FINISH;
    update_interrupts();
}

void pe_signal_token(u16 token, bool interrupt) {
    s_pe.token = token;
    if (interrupt) {
        s_pe.regs[PE_REG_CTRL / 2U] |= PE_CTRL_TOKEN;
        update_interrupts();
    }
}

static u16 read16(u32 offset) {
    if (offset == PE_REG_TOKEN) {
        return s_pe.token;
    }
    if (offset >= 0x10U) {
        return 0; /* caixa delimitadora e contadores de desempenho: sem GPU, zero */
    }
    return s_pe.regs[offset / 2U];
}

static void write16(u32 offset, u16 v) {
    if (offset == PE_REG_CTRL) {
        /* Dolphin PE_CTRL_REGISTER: habilitações copiadas; escrever 1 em token/fim apaga o status. */
        u16 c = s_pe.regs[PE_REG_CTRL / 2U];
        c = (u16)((c & ~(PE_CTRL_TOKEN_ENABLE | PE_CTRL_FINISH_ENABLE)) |
                  (v & (PE_CTRL_TOKEN_ENABLE | PE_CTRL_FINISH_ENABLE)));
        c = (u16)(c & ~(v & (PE_CTRL_TOKEN | PE_CTRL_FINISH)));
        s_pe.regs[PE_REG_CTRL / 2U] = (u16)(c | (v & ~0xFU));
        update_interrupts();
        return;
    }
    if (offset >= 0x10U || offset == PE_REG_TOKEN) {
        bus_unhandled_write(BUS_CPU_ADDR(PE_BASE + offset), v, 2);
    }
    s_pe.regs[offset / 2U] = v; /* ZCONF, ALPHACONF, DSTALPHACONF, ALPHAMODE, ALPHAREAD: estado de desenho (M6) */
}

u64 pe_read(u32 offset, u8 size) {
    if (offset > PE_REG_LAST || (offset & 1U) != 0U || (size != 2U && size != 4U)) {
        bus_unhandled_read(BUS_CPU_ADDR(PE_BASE + offset), size);
    }
    if (size == 4U) {
        return ((u32)read16(offset) << 16) | read16(offset + 2U);
    }
    return read16(offset);
}

void pe_write(u32 offset, u64 value, u8 size) {
    if (offset > PE_REG_LAST || (offset & 1U) != 0U || (size != 2U && size != 4U)) {
        bus_unhandled_write(BUS_CPU_ADDR(PE_BASE + offset), value, size);
    }
    if (size == 4U) {
        write16(offset, (u16)(value >> 16));
        write16(offset + 2U, (u16)value);
        return;
    }
    write16(offset, (u16)value);
}
