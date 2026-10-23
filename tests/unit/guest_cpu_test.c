/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/guest_cpu.h"
#include "am/mem.h"

#include "am_test.h"

TEST(create_binds_memory_and_every_callback) {
    EXPECT_TRUE(mem_init());
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    EXPECT_TRUE(cpu.ram == mem_mem1_data());
    EXPECT_EQ(cpu.ram_size, MEM_MEM1_SIZE);
    EXPECT_NOT_NULL(cpu.external_read);
    EXPECT_NOT_NULL(cpu.external_write);
    EXPECT_NOT_NULL(cpu.external_read32);
    EXPECT_NOT_NULL(cpu.external_write32);
    EXPECT_NOT_NULL(cpu.instruction_fallback);
    EXPECT_NOT_NULL(cpu.host_call);
    EXPECT_NOT_NULL(cpu.spr_read);
    EXPECT_NOT_NULL(cpu.spr_write);
    EXPECT_NOT_NULL(cpu.cache_control);
    guest_cpu_destroy(&cpu);
    EXPECT_NULL(cpu.ram);
}

TEST(pvr_comes_from_mfspr_287) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    EXPECT_EQ(ppc_mfspr(&cpu, 287U, 0x80003100U), PPC_GEKKO_PVR);
    EXPECT_EQ(cpu.program_exception, 0);
    guest_cpu_destroy(&cpu);
}

TEST(host_call_declines_every_address_for_now) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    EXPECT_TRUE(!ppc_host_call(&cpu, 0x800C25DCU));
    guest_cpu_destroy(&cpu);
}

TEST(cache_ops_are_harmless) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    ppc_cache_control(&cpu, PPC_CACHE_DCBF, 0x80001000U, 0x80003100U);
    ppc_cache_control(&cpu, PPC_CACHE_ICBI, 0x80001000U, 0x80003100U);
    EXPECT_EQ(cpu.exception, 0);
    guest_cpu_destroy(&cpu);
}

TEST(ppc_core_shares_the_runtime_memory) {
    /* lswx (cpu.c do DolRecomp) lê os bytes via cpu->ram; tem de enxergar o que mem_write* gravou. */
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    mem_write32(&cpu, 0x80007000U, 0x41424344U);
    cpu.xer = 4U;
    cpu.gpr[4] = 0x80007000U;
    cpu.gpr[5] = 0U;
    ppc_lswx(&cpu, 6U, 4U, 5U, 0x80003100U);
    EXPECT_EQ(cpu.gpr[6], 0x41424344U);
    guest_cpu_destroy(&cpu);
}

int main(void) {
    RUN_TEST(create_binds_memory_and_every_callback);
    RUN_TEST(pvr_comes_from_mfspr_287);
    RUN_TEST(host_call_declines_every_address_for_now);
    RUN_TEST(cache_ops_are_harmless);
    RUN_TEST(ppc_core_shares_the_runtime_memory);
    mem_shutdown();
    return am_test_finish();
}
