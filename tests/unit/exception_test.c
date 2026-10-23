/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Modelo de vetor sintético (palavras quaisquer, não as do SDK): o teste confere a cópia e o efeito do HLE. */
#include "am/exception.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/ppc_sys.h"

#include "am_test.h"

#define TPL_START 0x80005000U
#define TPL_END   (TPL_START + 0x98U)
#define TPL_SETN  (TPL_START + 0x68U)

static void install(u32 number) {
    for (GuestAddr a = TPL_START; a < TPL_END; a += 4U) {
        const u32 word = a == TPL_SETN ? 0x38600000U : 0x60000000U | (a & 0xFFFFU);
        mem_write32(NULL, a, word);
        mem_write32(NULL, 0x80000500U + (a - TPL_START), a == TPL_SETN ? word | number : word);
    }
    mem_write32(NULL, 0x800000C0U, 0x00006000U); /* contexto atual, físico */
    mem_write32(NULL, 0x800000D4U, 0x80006000U); /* idem, virtual */
    mem_write32(NULL, 0x80003000U + (4U * 4U), 0x80007000U);
}

static void init_layout(void) {
    const ExceptionLayout l = {TPL_START, TPL_END, TPL_SETN, 0x80008000U, 0x80009000U, 0x80009008U};
    exception_init(&l);
}

TEST(external_interrupt_reaches_os_handler) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    ppc_sys_reset();
    init_layout();
    install(4);
    cpu.pc = 0x80001234U;
    cpu.msr = 0x0000A032U; /* EE|FP|IR|DR|RI */
    cpu.lr = 0x80004444U;
    cpu.cr = 0x00000000U;
    cpu.gpr[3] = 3;
    cpu.gpr[4] = 4;
    cpu.gpr[5] = 5;
    exception_deliver(&cpu, EXCEPTION_NUM_EXTERNAL, EXCEPTION_VEC_EXTERNAL);
    EXPECT_EQ(cpu.pc, 0x80007000U);
    EXPECT_EQ(cpu.gpr[3], 4U);
    EXPECT_EQ(cpu.gpr[4], 0x80006000U);
    EXPECT_EQ(cpu.msr, 0x00000030U); /* EE e FP desligados, tradução ligada pelo rfi */
    EXPECT_EQ(cpu.cr >> 28, 0x4U);   /* rlwinm. com RI=1: GT */
    EXPECT_EQ(mem_read32(NULL, 0x8000600CU), 3U);
    EXPECT_EQ(mem_read32(NULL, 0x80006010U), 4U);
    EXPECT_EQ(mem_read32(NULL, 0x80006014U), 5U);
    EXPECT_EQ(mem_read32(NULL, 0x80006084U), 0x80004444U);
    EXPECT_EQ(mem_read32(NULL, 0x80006198U), 0x80001234U);
    EXPECT_EQ(mem_read32(NULL, 0x8000619CU), 0x0000A032U);
    EXPECT_EQ(mem_read16(NULL, 0x800061A2U), 2U);
    EXPECT_EQ(ppc_sys_spr(PPC_SPR_SPRG0), 4U);
    EXPECT_EQ(exception_delivered_count(), 1U);
}

TEST(system_call_restores_hid0_and_returns) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    ppc_sys_reset();
    init_layout();
    mem_write32(NULL, 0x80009000U, 0x11111111U);
    mem_write32(NULL, 0x80009004U, 0x22222222U);
    mem_write32(NULL, 0x80000C00U, 0x11111111U);
    mem_write32(NULL, 0x80000C04U, 0x22222222U);
    ppc_sys_set_spr(PPC_SPR_HID0, 0x0011C464U);
    cpu.msr = 0x2032U;
    ppc_system_call_exception(&cpu, 0x80001000U); /* o que o C gerado faz no "sc" */
    EXPECT_EQ(cpu.pc, 0x00000C00U);
    exception_system_call(&cpu);
    EXPECT_EQ(cpu.pc, 0x80001004U);
    EXPECT_EQ(cpu.msr, 0x2032U);
    EXPECT_EQ(cpu.exception, 0U);
    EXPECT_EQ(cpu.gpr[9], 0x0011C464U);
    EXPECT_EQ(cpu.gpr[10], 0x0011C46CU);
    EXPECT_EQ(ppc_sys_spr(PPC_SPR_HID0), 0x0011C464U);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(external_interrupt_reaches_os_handler);
    RUN_TEST(system_call_restores_hid0_and_returns);
    mem_shutdown();
    return am_test_finish();
}
