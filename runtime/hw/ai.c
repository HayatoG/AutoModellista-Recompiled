/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Audio Interface como no Dolphin 2609a (Core/HW/AudioInterface.cpp). O contador de amostras do streaming (AISCNT)
 * anda com o tempo da CPU enquanto PSTAT = 1 e a interrupção AIINT sobe quando ele passa por AIIT. O SDK liga PSTAT
 * no AIInit (__AI_SRC_INIT mede o atraso do conversor contando amostras); o áudio em si do streaming do disco não é
 * tocado (só existiria com o comando DI 0xE1, que é am_fatal em di.c). */
#include "am/ai.h"

#include <stdint.h>

#include "am/bus.h"
#include "am/log.h"
#include "am/pi.h"
#include "am/timebase.h"

/* Mixer::FIXED_SAMPLE_RATE_DIVIDEND = 54 MHz * 2; divisores de 48 kHz (1124 * 2, GameCube) e 32 kHz (* 3/2). Com a
 * CPU a 486 MHz: 10116 ciclos por amostra a 48 kHz e 15174 a 32 kHz. */
#define AI_RATE_DIVIDEND   108000000ULL
#define AI_DIVISOR_48KHZ   2248ULL
#define AI_DIVISOR_32KHZ   3372ULL
#define AI_CPU_TICKS_PER_S 486000000ULL

static struct {
    u32 control;
    u32 volume;
    u32 sample_counter;
    u32 interrupt_timing;
    u64 last_cycle;
    u64 cycles_per_sample;
    u64 stream_starts;
} s_ai;

static u64 cycles_per_sample(bool ais_48khz) {
    return AI_CPU_TICKS_PER_S * (ais_48khz ? AI_DIVISOR_48KHZ : AI_DIVISOR_32KHZ) / AI_RATE_DIVIDEND;
}

/* AudioInterfaceManager::Init: AISFR = 48 kHz (bit = 1), AIDFR = 32 kHz (bit = 1). */
void ai_reset(void) {
    s_ai.control = AI_CR_AISFR | AI_CR_AIDFR;
    s_ai.volume = 0;
    s_ai.sample_counter = 0;
    s_ai.interrupt_timing = 0;
    s_ai.last_cycle = timebase_cycles(NULL);
    s_ai.cycles_per_sample = cycles_per_sample(true);
    s_ai.stream_starts = 0;
}

static bool playing(void) {
    return (s_ai.control & AI_CR_PSTAT) != 0U;
}

static void update_interrupts(void) {
    pi_set_interrupt(PI_INT_AI, (s_ai.control & AI_CR_AIINT) != 0U && (s_ai.control & AI_CR_AIINTMSK) != 0U);
}

/* Update + IncreaseSampleCount: amostras inteiras desde a última conta; AIINT se o contador passou por AIIT. */
void ai_update(void) {
    if (!playing()) {
        return;
    }
    const u64 now = timebase_cycles(NULL);
    const u64 samples = (now - s_ai.last_cycle) / s_ai.cycles_per_sample;
    if (samples == 0U) {
        return;
    }
    s_ai.last_cycle += samples * s_ai.cycles_per_sample;
    const u32 old = s_ai.sample_counter + 1U;
    s_ai.sample_counter += (u32)samples;
    if (s_ai.interrupt_timing - old <= s_ai.sample_counter - old) {
        s_ai.control |= AI_CR_AIINT;
        update_interrupts();
    }
}

/* Ciclo em que o contador passa por AIIT (o ai_update sobe a AIINT ali); UINT64_MAX parado. */
u64 ai_next_event_cycle(void) {
    if (!playing()) {
        return UINT64_MAX;
    }
    const u32 k = s_ai.interrupt_timing - s_ai.sample_counter;
    const u64 samples = k != 0U ? (u64)k : (1ULL << 32);
    return s_ai.last_cycle + samples * s_ai.cycles_per_sample;
}

u64 ai_stream_starts(void) {
    return s_ai.stream_starts;
}

static void write_control(u32 v) {
    ai_update();
    const u32 copied = AI_CR_AIINTMSK | AI_CR_AIINTVLD | AI_CR_AIDFR;
    s_ai.control = (s_ai.control & ~copied) | (v & copied);
    if (((v ^ s_ai.control) & AI_CR_AISFR) != 0U) {
        s_ai.control ^= AI_CR_AISFR;
        s_ai.cycles_per_sample = cycles_per_sample((s_ai.control & AI_CR_AISFR) != 0U);
    }
    if (((v ^ s_ai.control) & AI_CR_PSTAT) != 0U) {
        s_ai.control ^= AI_CR_PSTAT;
        s_ai.last_cycle = timebase_cycles(NULL);
        if (playing()) {
            s_ai.stream_starts++;
        }
    }
    if ((v & AI_CR_AIINT) != 0U) {
        s_ai.control &= ~AI_CR_AIINT;
    }
    if ((v & AI_CR_SCRESET) != 0U) {
        s_ai.sample_counter = 0;
        s_ai.last_cycle = timebase_cycles(NULL);
    }
    update_interrupts();
}

u64 ai_read(u32 offset, u8 size) {
    if (size != 4U) {
        bus_unhandled_read(BUS_CPU_ADDR(AI_BASE + offset), size);
    }
    ai_update();
    switch (offset) {
    case AI_REG_CONTROL:
        return s_ai.control;
    case AI_REG_VOLUME:
        return s_ai.volume;
    case AI_REG_SCNT:
        /* Dolphin soma as amostras da fatia em andamento; parado, devolve o contador (o Dolphin soma também o
         * tempo da última parada, o que parece um descuido e não é reproduzido). */
        if (playing()) {
            return s_ai.sample_counter + (u32)((timebase_cycles(NULL) - s_ai.last_cycle) / s_ai.cycles_per_sample);
        }
        return s_ai.sample_counter;
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
        s_ai.last_cycle = timebase_cycles(NULL);
        return;
    case AI_REG_IT:
        ai_update();
        s_ai.interrupt_timing = (u32)value;
        return;
    default:
        bus_unhandled_write(BUS_CPU_ADDR(AI_BASE + offset), value, size);
    }
}
