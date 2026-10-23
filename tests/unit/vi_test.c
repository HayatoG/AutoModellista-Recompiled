/* SPDX-License-Identifier: GPL-3.0-or-later */
/* VI: feixe, interrupções de display e ritmo de campos pelo tempo da CPU; SI lido pelo VI. */
#include "am/bus.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/si.h"
#include "am/timebase.h"
#include "am/vi.h"

#include "am_test.h"

static void run_cycles(CPUState *cpu, s64 cycles) {
    timebase_begin_slice(cpu);
    cpu->downcount -= cycles;
    timebase_end_slice(cpu);
    bus_update();
}

/* Programa o modo NTSC entrelaçado como o VIInit: 27 MHz, HLW 429, 525 meias-linhas por campo. */
static void ntsc_27mhz(void) {
    bus_write(0xCC00206CU, 0, 2);           /* VI_CLOCK = 27 MHz */
    bus_write(0xCC002000U, 0x0F06U, 2);     /* EQU 6, ACV 240 */
    bus_write(0xCC00200CU, 0x00030018U, 4); /* VTO: PSB 3, PRB 24 */
    bus_write(0xCC002010U, 0x00020019U, 4); /* VTE: PSB 2, PRB 25 */
}

TEST(field_rate_is_59_94_hz_at_27mhz) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    ntsc_27mhz();
    EXPECT_EQ(vi_ticks_per_half_line(), 36U * 429U);
    const u64 fields = vi_field_count();
    /* 1 s de CPU: 486e6 / (15444 * 525) = 59,94 campos. */
    for (int i = 0; i < 486; i++) {
        run_cycles(&cpu, 1000000);
    }
    EXPECT_EQ(vi_field_count() - fields, 60U);
}

TEST(display_interrupt_at_programmed_line) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    ntsc_27mhz();
    pi_write(PI_REG_INTSR, 0xFFFFFFFFU, 4);
    pi_write(PI_REG_INTMR, PI_INT_VI, 4);
    bus_write(0xCC002034U, 0x00000000U, 4);                     /* DI1 desligado */
    bus_write(0xCC002030U, 0x10000000U | (100U << 16) | 1U, 4); /* DI0: linha 100, habilitado */
    EXPECT_TRUE(!pi_external_pending());
    while ((vi_half_line() / 2U) + 1U < 100U) {
        run_cycles(&cpu, 1000);
    }
    run_cycles(&cpu, (s64)vi_ticks_per_half_line());
    EXPECT_TRUE(pi_external_pending());
    EXPECT_EQ(bus_read(0xCC002030U, 2) & 0x8000U, 0x8000U);
    EXPECT_EQ(bus_read(0xCC00202CU, 2), (vi_half_line() / 2U) + 1U); /* VCOUNT */
    bus_write(0xCC002030U, 0x10000000U | (100U << 16) | 1U, 4);      /* apaga o status (escrita sem o bit 31) */
    EXPECT_TRUE(!pi_external_pending());
}

TEST(si_polled_from_vi_with_neutral_pad) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    ntsc_27mhz();
    bus_write(0xCC006430U, (8U << 16) | 0xF0U, 4); /* SIPOLL: X = 8 linhas, EN0..3 */
    const u64 polls = si_poll_count();
    for (int i = 0; i < 20; i++) {
        run_cycles(&cpu, 1000000);
    }
    EXPECT_TRUE(si_poll_count() > polls);
    EXPECT_EQ(bus_read(0xCC006438U, 4) & 0x20000000U, 0x20000000U); /* RDST0 */
    EXPECT_EQ(bus_read(0xCC006404U, 4), SI_PAD_NEUTRAL_HI);
    EXPECT_EQ(bus_read(0xCC006438U, 4) & 0x20000000U, 0U);          /* ler IN apaga RDST0 */
    EXPECT_EQ(bus_read(0xCC006438U, 4) & 0x08080808U, 0x00080808U); /* NOREP nas portas vazias */
    si_set_pad_buttons(SI_PAD_A | SI_PAD_START);
    run_cycles(&cpu, 2000000);
    EXPECT_EQ(bus_read(0xCC006404U, 4), SI_PAD_NEUTRAL_HI | ((SI_PAD_A | SI_PAD_START) << 16));
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(field_rate_is_59_94_hz_at_27mhz);
    RUN_TEST(display_interrupt_at_programmed_line);
    RUN_TEST(si_polled_from_vi_with_neutral_pad);
    mem_shutdown();
    return am_test_finish();
}
