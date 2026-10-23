/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>

#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/trace.h"

#include "am_test.h"

/* Codificações montadas à mão (750CL manual cap. 12). */
#define BL_FWD_1000 0x48001001U /* bl +0x1000 */
#define BL_BACK_10  0x4BFFFFF1U /* bl -0x10 */
#define BLA_2000    0x48002003U /* bla 0x2000 */
#define BCL_FWD_40  0x42800041U /* bcl 20,0,+0x40 */
#define BCTRL       0x4E800421U
#define BLRL        0x4E800021U
#define BL_NOLINK   0x48001000U /* b +0x1000 */
#define BLR         0x4E800020U

TEST(direct_calls_check_the_target) {
    EXPECT_TRUE(trace_is_call(BL_FWD_1000, 0x80001004U, 0x80002000U, 0));
    EXPECT_TRUE(!trace_is_call(BL_FWD_1000, 0x80001004U, 0x80002004U, 0));
    EXPECT_TRUE(trace_is_call(BL_BACK_10, 0x80001004U, 0x80000FF0U, 0));
    EXPECT_TRUE(trace_is_call(BLA_2000, 0x80001004U, 0x00002000U, 0));
    EXPECT_TRUE(trace_is_call(BCL_FWD_40, 0x80001004U, 0x80001040U, 0));
    EXPECT_TRUE(!trace_is_call(BL_NOLINK, 0x80001004U, 0x80002000U, 0));
    EXPECT_TRUE(!trace_is_call(BLR, 0x80001004U, 0x80002000U, 0));
}

TEST(register_calls) {
    EXPECT_TRUE(trace_is_call(BCTRL, 0x80001004U, 0x80005000U, 0x80005000U));
    EXPECT_TRUE(!trace_is_call(BCTRL, 0x80001004U, 0x80005000U, 0x80006000U));
    EXPECT_TRUE(trace_is_call(BLRL, 0x80001004U, 0x80005000U, 0));
    EXPECT_TRUE(!trace_is_call(BLRL, 0x80001004U, 0x80001004U, 0)); /* é o retorno */
    EXPECT_TRUE(!trace_is_call(BL_FWD_1000, 0U, 0x80002000U, 0));
}

TEST(file_has_header_and_one_line_per_call) {
    EXPECT_TRUE(mem_init());
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    const char *path = "trace_test_out.txt";
    EXPECT_TRUE(trace_open(path, "k=v", 0));
    mem_write32(&cpu, 0x80001000U, BL_FWD_1000);
    cpu.lr = 0x80001004U;
    cpu.pc = 0x80002000U;
    EXPECT_TRUE(trace_on_dispatch(&cpu));
    cpu.pc = 0x80001004U; /* volta do bl: não é chamada */
    EXPECT_TRUE(!trace_on_dispatch(&cpu));
    cpu.lr = 0; /* LR fora da MEM1 */
    EXPECT_TRUE(!trace_on_dispatch(&cpu));
    EXPECT_EQ(trace_event_count(), 1U);
    trace_close();
    FILE *f = fopen(path, "r");
    char text[256] = {0};
    if (f != NULL) {
        (void)fread(text, 1, sizeof(text) - 1U, f);
        (void)fclose(f);
    }
    EXPECT_STREQ(text, "# am-trace v1 source=runtime kind=calls k=v\n80002000 80001004 00000000\n");
    (void)remove(path);
}

int main(void) {
    RUN_TEST(direct_calls_check_the_target);
    RUN_TEST(register_calls);
    RUN_TEST(file_has_header_and_one_line_per_call);
    mem_shutdown();
    return am_test_finish();
}
