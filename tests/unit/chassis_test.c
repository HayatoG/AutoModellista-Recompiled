/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Chassi com funções sintéticas no lugar do C gerado. */
#include <stdio.h>

#include "am/bus.h"
#include "am/chassis.h"
#include "am/dispatch.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/timebase.h"
#include "am/trace.h"

#include "am_test.h"

#define CALLER 0x80010000U
#define CALLEE 0x80020000U
#define DONE   0x80030000U

/* Como o gerado faz um bl para outro trecho: LR = endereço seguinte, pc = alvo, volta ao chassi. */
static void caller(CPUState *cpu) {
    cpu->downcount -= 5;
    cpu->lr = CALLER + 4U;
    cpu->pc = CALLEE;
}

static void callee(CPUState *cpu) {
    cpu->downcount -= 7;
    cpu->pc = DONE;
}

TEST(runs_until_stop_address_and_traces_the_call) {
    EXPECT_TRUE(mem_init());
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    bus_reset();
    timebase_reset(0);
    dispatch_clear();
    EXPECT_EQ(dispatch_register(CALLER, CALLER + 0x100U, caller), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(CALLEE, CALLEE + 0x100U, callee), DISPATCH_OK);
    mem_write32(NULL, CALLER, 0x48010001U); /* bl CALLEE (deslocamento +0x10000) */
    const char *path = "chassis_test_trace.txt";
    EXPECT_TRUE(trace_open(path, NULL, 0));
    cpu.pc = CALLER;
    cpu.msr = 0x2032U;
    const ChassisConfig cfg = {.stop_at = DONE};
    ChassisStats st = {0};
    EXPECT_EQ(chassis_run(&cpu, &cfg, &st), CHASSIS_STOP_ADDRESS);
    trace_close();
    (void)remove(path);
    EXPECT_EQ(st.steps, 2U);
    EXPECT_EQ(trace_event_count(), 1U);
    EXPECT_EQ(timebase_cycles(NULL), 12U);
    EXPECT_EQ(cpu.pc, DONE);
}

TEST(step_limit) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    cpu.pc = CALLER;
    const ChassisConfig cfg = {.stop_at = DONE, .max_steps = 1};
    ChassisStats st = {0};
    EXPECT_EQ(chassis_run(&cpu, &cfg, &st), CHASSIS_STOP_STEPS);
    EXPECT_EQ(cpu.pc, CALLEE);
}

int main(void) {
    RUN_TEST(runs_until_stop_address_and_traces_the_call);
    RUN_TEST(step_limit);
    dispatch_clear();
    mem_shutdown();
    return am_test_finish();
}
