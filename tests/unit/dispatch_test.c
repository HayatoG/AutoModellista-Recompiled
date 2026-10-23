/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/dispatch.h"
#include "am/guest_cpu.h"

#include "am_test.h"

static int s_calls_a;
static int s_calls_b;

static void fn_a(CPUState *cpu) {
    s_calls_a++;
    cpu->pc = 0x1U;
}

static void fn_b(CPUState *cpu) {
    (void)cpu;
    s_calls_b++;
}

TEST(empty_table_finds_nothing) {
    dispatch_clear();
    EXPECT_EQ(dispatch_count(), 0);
    EXPECT_NULL(dispatch_lookup(0x80003100U));
}

TEST(lookup_respects_half_open_ranges) {
    dispatch_clear();
    EXPECT_EQ(dispatch_register(0x80003100U, 0x80005000U, fn_a), DISPATCH_OK);
    EXPECT_NULL(dispatch_lookup(0x800030FCU));
    EXPECT_TRUE(dispatch_lookup(0x80003100U) == fn_a);
    EXPECT_TRUE(dispatch_lookup(0x80004FFCU) == fn_a);
    EXPECT_NULL(dispatch_lookup(0x80005000U));
}

TEST(entries_stay_sorted_regardless_of_insertion_order) {
    dispatch_clear();
    EXPECT_EQ(dispatch_register(0x80300000U, 0x80301000U, fn_b), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(0x80100000U, 0x80101000U, fn_a), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(0x80200000U, 0x80201000U, fn_b), DISPATCH_OK);
    EXPECT_EQ(dispatch_count(), 3);
    const DispatchEntry *entries = dispatch_entries();
    EXPECT_EQ(entries[0].start, 0x80100000U);
    EXPECT_EQ(entries[1].start, 0x80200000U);
    EXPECT_EQ(entries[2].start, 0x80300000U);
    EXPECT_TRUE(dispatch_lookup(0x80100004U) == fn_a);
    EXPECT_TRUE(dispatch_lookup(0x80200ffcU) == fn_b);
    EXPECT_NULL(dispatch_lookup(0x80201000U));
}

TEST(adjacent_ranges_are_allowed_overlaps_are_not) {
    dispatch_clear();
    EXPECT_EQ(dispatch_register(0x1000U, 0x2000U, fn_a), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(0x2000U, 0x3000U, fn_b), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(0x1ffcU, 0x2004U, fn_a), DISPATCH_ERR_OVERLAP);
    EXPECT_EQ(dispatch_register(0x0800U, 0x1004U, fn_a), DISPATCH_ERR_OVERLAP);
    EXPECT_EQ(dispatch_register(0x1400U, 0x1800U, fn_a), DISPATCH_ERR_OVERLAP);
    EXPECT_EQ(dispatch_register(0x0U, 0x4000U, fn_a), DISPATCH_ERR_OVERLAP);
    EXPECT_EQ(dispatch_count(), 2);
    EXPECT_TRUE(dispatch_lookup(0x1ffcU) == fn_a);
    EXPECT_TRUE(dispatch_lookup(0x2000U) == fn_b);
}

TEST(invalid_registrations_are_rejected) {
    dispatch_clear();
    EXPECT_EQ(dispatch_register(0x1000U, 0x2000U, NULL), DISPATCH_ERR_INVALID);
    EXPECT_EQ(dispatch_register(0x2000U, 0x2000U, fn_a), DISPATCH_ERR_INVALID);
    EXPECT_EQ(dispatch_register(0x3000U, 0x2000U, fn_a), DISPATCH_ERR_INVALID);
    EXPECT_EQ(dispatch_register(0x1001U, 0x2000U, fn_a), DISPATCH_ERR_INVALID);
    EXPECT_EQ(dispatch_count(), 0);
}

TEST(table_grows_past_initial_capacity_and_lookup_stays_correct) {
    dispatch_clear();
    for (u32 i = 0; i < 1000U; i++) {
        /* ordem decrescente: pior caso para a inserção ordenada */
        const GuestAddr start = 0x80000000U + ((999U - i) * 0x1000U);
        EXPECT_EQ(dispatch_register(start, start + 0x800U, (i % 2U) != 0U ? fn_a : fn_b), DISPATCH_OK);
    }
    EXPECT_EQ(dispatch_count(), 1000);
    const DispatchEntry *entries = dispatch_entries();
    for (size_t i = 1; i < dispatch_count(); i++) {
        EXPECT_TRUE(entries[i - 1U].start < entries[i].start);
    }
    EXPECT_TRUE(dispatch_lookup(0x80000000U + (999U * 0x1000U) + 0x7FCU) == fn_b);
    EXPECT_NULL(dispatch_lookup(0x80000000U + (500U * 0x1000U) + 0x800U));
}

TEST(step_calls_the_function_covering_pc) {
    dispatch_clear();
    EXPECT_EQ(dispatch_register(0x80010000U, 0x80011000U, fn_a), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(0x80020000U, 0x80021000U, fn_b), DISPATCH_OK);
    s_calls_a = 0;
    s_calls_b = 0;
    CPUState cpu;
    cpu.pc = 0x80010abcU & ~3U;
    dispatch_step(&cpu);
    EXPECT_EQ(s_calls_a, 1);
    EXPECT_EQ(s_calls_b, 0);
    EXPECT_EQ(cpu.pc, 0x1);
    cpu.pc = 0x80020000U;
    dispatch_step(&cpu);
    EXPECT_EQ(s_calls_b, 1);
}

int main(void) {
    RUN_TEST(empty_table_finds_nothing);
    RUN_TEST(lookup_respects_half_open_ranges);
    RUN_TEST(entries_stay_sorted_regardless_of_insertion_order);
    RUN_TEST(adjacent_ranges_are_allowed_overlaps_are_not);
    RUN_TEST(invalid_registrations_are_rejected);
    RUN_TEST(table_grows_past_initial_capacity_and_lookup_stays_correct);
    RUN_TEST(step_calls_the_function_covering_pc);
    dispatch_clear();
    return am_test_finish();
}
