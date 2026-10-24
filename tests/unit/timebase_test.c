/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/timebase.h"

#include "am_test.h"

TEST(tb_advances_one_tick_per_12_cycles) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(1000U);
    EXPECT_EQ(timebase_read(&cpu), 1000U);
    timebase_begin_slice(&cpu);
    cpu.downcount -= 24;
    EXPECT_EQ(timebase_read(&cpu), 1002U); /* fatia em andamento conta */
    EXPECT_EQ(timebase_read(NULL), 1002U); /* dispositivos enxergam a CPU da fatia */
    cpu.downcount -= 11;
    timebase_end_slice(&cpu);
    EXPECT_EQ(timebase_cycles(NULL), 35U);
    EXPECT_EQ(cpu.downcount, 0);
    EXPECT_EQ(timebase_read(&cpu), 1002U);
    EXPECT_EQ(ppc_mftb(&cpu, 268U, 0x80003100U), 1002U);
    EXPECT_EQ(ppc_mftb(&cpu, 269U, 0x80003100U), 0U);
}

TEST(tb_writes_keep_running) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    timebase_write_upper(&cpu, 0x12U);
    timebase_write_lower(&cpu, 0xFFFFFFFFU);
    EXPECT_EQ(timebase_read(&cpu), 0x12FFFFFFFFULL);
    timebase_begin_slice(&cpu);
    cpu.downcount -= 12;
    timebase_end_slice(&cpu);
    EXPECT_EQ(timebase_read(&cpu), 0x1300000000ULL);
}

TEST(decrementer_counts_down_and_expires_on_transition) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    EXPECT_EQ(timebase_dec_read(&cpu), 0xFFFFFFFFU);
    EXPECT_TRUE(!timebase_dec_expired(&cpu)); /* já negativo: sem transição */
    timebase_dec_write(&cpu, 2U);
    timebase_begin_slice(&cpu);
    cpu.downcount -= 24;
    EXPECT_EQ(timebase_dec_read(&cpu), 0U);
    EXPECT_TRUE(!timebase_dec_expired(&cpu));
    cpu.downcount -= 12;
    EXPECT_EQ(timebase_dec_read(&cpu), 0xFFFFFFFFU);
    EXPECT_TRUE(timebase_dec_expired(&cpu));
    timebase_end_slice(&cpu);
}

/* M8: o --timebase-out grava o último mftb do jogo (como o stub GDB do Dolphin) e o boot avança o tempo do BS2. */
TEST(guest_last_follows_mftb_and_advance_counts) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    timebase_advance(120U);
    EXPECT_EQ(timebase_read(&cpu), 10U);
    EXPECT_EQ(timebase_guest_last(), 0U);
    timebase_begin_slice(&cpu);
    cpu.downcount -= 24;
    EXPECT_EQ(ppc_mftb(&cpu, 268U, 0x80003100U), 12U);
    cpu.downcount -= 24;
    timebase_end_slice(&cpu);
    EXPECT_EQ(timebase_guest_last(), 12U);
    EXPECT_EQ(timebase_read(&cpu), 14U);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(tb_advances_one_tick_per_12_cycles);
    RUN_TEST(tb_writes_keep_running);
    RUN_TEST(decrementer_counts_down_and_expires_on_transition);
    RUN_TEST(guest_last_follows_mftb_and_advance_counts);
    mem_shutdown();
    return am_test_finish();
}
