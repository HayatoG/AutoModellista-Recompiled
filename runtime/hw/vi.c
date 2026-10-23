/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/vi.h"

#include "am/bus.h"

static struct {
    u16 clock;
} s_vi;

/* VideoInterfaceManager::Preset: m_clock = IsNTSC(região) (1 = 54 MHz, capaz de progressivo). */
void vi_reset(bool ntsc) {
    s_vi.clock = ntsc ? 1U : 0U;
}

u64 vi_read(u32 offset, u8 size) {
    if (size == 2U && offset == VI_REG_CLOCK) {
        return s_vi.clock;
    }
    bus_unhandled_read(BUS_CPU_ADDR(VI_BASE + offset), size);
}

void vi_write(u32 offset, u64 value, u8 size) {
    bus_unhandled_write(BUS_CPU_ADDR(VI_BASE + offset), value, size);
}
