/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/ai.h"

#include "am/bus.h"
#include "am/log.h"
#include "am/pi.h"

static struct {
    u32 control;
    u32 volume;
    u32 sample_counter;
    u32 interrupt_timing;
} s_ai;

void ai_reset(void) {
    s_ai.control = 0; /* Dolphin AudioInterfaceManager::Init */
    s_ai.volume = 0;
    s_ai.sample_counter = 0;
    s_ai.interrupt_timing = 0;
}

static void update_interrupts(void) {
    pi_set_interrupt(PI_INT_AI, (s_ai.control & AI_CR_AIINT) != 0U && (s_ai.control & AI_CR_AIINTMSK) != 0U);
}

/* Dolphin RegisterMMIO, AI_CONTROL_REGISTER: AIINTMSK/AIINTVLD/AISFR/AIDFR copiados, AIINT=1 apaga, SCRESET zera o
 * contador. PSTAT liga o streaming do disco, que ainda não existe. */
static void write_control(u32 v) {
    if ((v & AI_CR_PSTAT) != 0U) {
        am_fatal("AI: PSTAT=1 (streaming de áudio do disco) não implementado (TODO(M5))");
    }
    const u32 copied = AI_CR_AIINTMSK | AI_CR_AIINTVLD | AI_CR_AISFR | AI_CR_AIDFR;
    s_ai.control = (s_ai.control & ~copied) | (v & copied);
    if ((v & AI_CR_AIINT) != 0U) {
        s_ai.control &= ~AI_CR_AIINT;
    }
    if ((v & AI_CR_SCRESET) != 0U) {
        s_ai.sample_counter = 0;
    }
    update_interrupts();
}

u64 ai_read(u32 offset, u8 size) {
    if (size != 4U) {
        bus_unhandled_read(BUS_CPU_ADDR(AI_BASE + offset), size);
    }
    switch (offset) {
    case AI_REG_CONTROL:
        return s_ai.control;
    case AI_REG_VOLUME:
        return s_ai.volume;
    case AI_REG_SCNT:
        return s_ai.sample_counter; /* parado (PSTAT=0): não avança */
    case AI_REG_IT:
        return s_ai.interrupt_timing;
    default:
        bus_unhandled_read(BUS_CPU_ADDR(AI_BASE + offset), size);
    }
}

void ai_write(u32 offset, u64 value, u8 size) {
    if (size != 4U) {
        bus_unhandled_write(BUS_CPU_ADDR(AI_BASE + offset), value, size);
    }
    switch (offset) {
    case AI_REG_CONTROL:
        write_control((u32)value);
        return;
    case AI_REG_VOLUME:
        s_ai.volume = (u32)value;
        return;
    case AI_REG_SCNT:
        s_ai.sample_counter = (u32)value;
        return;
    case AI_REG_IT:
        s_ai.interrupt_timing = (u32)value;
        return;
    default:
        bus_unhandled_write(BUS_CPU_ADDR(AI_BASE + offset), value, size);
    }
}
