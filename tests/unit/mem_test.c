/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/guest_cpu.h"
#include "am/mem.h"

#include "am_test.h"

TEST(write32_is_big_endian_in_host_memory) {
    EXPECT_TRUE(mem_init());
    mem_write32(NULL, 0x80001000U, 0x11223344U);
    const u8 *raw = mem_host_ptr(0x80001000U, 4U);
    EXPECT_NOT_NULL(raw);
    EXPECT_EQ(raw[0], 0x11);
    EXPECT_EQ(raw[1], 0x22);
    EXPECT_EQ(raw[2], 0x33);
    EXPECT_EQ(raw[3], 0x44);
    EXPECT_EQ(mem_read32(NULL, 0x80001000U), 0x11223344U);
}

TEST(sub_word_reads_follow_big_endian_order) {
    mem_write32(NULL, 0x80002000U, 0xAABBCCDDU);
    EXPECT_EQ(mem_read8(NULL, 0x80002000U), 0xAA);
    EXPECT_EQ(mem_read8(NULL, 0x80002003U), 0xDD);
    EXPECT_EQ(mem_read16(NULL, 0x80002000U), 0xAABB);
    EXPECT_EQ(mem_read16(NULL, 0x80002002U), 0xCCDD);
}

TEST(write64_and_read64_roundtrip) {
    mem_write64(NULL, 0x80003000U, 0x0102030405060708ULL);
    EXPECT_EQ(mem_read64(NULL, 0x80003000U), 0x0102030405060708ULL);
    EXPECT_EQ(mem_read32(NULL, 0x80003000U), 0x01020304U);
    EXPECT_EQ(mem_read32(NULL, 0x80003004U), 0x05060708U);
    EXPECT_EQ(mem_read8(NULL, 0x80003007U), 0x08);
}

TEST(unaligned_access_is_supported) {
    mem_write32(NULL, 0x80004001U, 0xCAFEBABEU);
    EXPECT_EQ(mem_read32(NULL, 0x80004001U), 0xCAFEBABEU);
    EXPECT_EQ(mem_read8(NULL, 0x80004001U), 0xCA);
    EXPECT_EQ(mem_read8(NULL, 0x80004000U), 0x00);
}

TEST(cached_and_uncached_mirrors_alias_the_same_memory) {
    mem_write32(NULL, 0x80005000U, 0xDEADBEEFU);
    EXPECT_EQ(mem_read32(NULL, 0xC0005000U), 0xDEADBEEFU);
    mem_write16(NULL, 0xC0005004U, 0x1234U);
    EXPECT_EQ(mem_read16(NULL, 0x80005004U), 0x1234);
    EXPECT_TRUE(mem_host_ptr(0x80005000U, 4U) == mem_host_ptr(0xC0005000U, 4U));
}

TEST(last_bytes_of_mem1_are_addressable) {
    mem_write32(NULL, 0x80000000U + MEM_MEM1_SIZE - 4U, 0x01020304U);
    EXPECT_EQ(mem_read32(NULL, 0xC0000000U + MEM_MEM1_SIZE - 4U), 0x01020304U);
    EXPECT_NOT_NULL(mem_host_ptr(0x80000000U + MEM_MEM1_SIZE - 1U, 1U));
}

TEST(host_ptr_rejects_addresses_outside_mem1) {
    EXPECT_NULL(mem_host_ptr(0x80000000U + MEM_MEM1_SIZE, 1U));
    EXPECT_NULL(mem_host_ptr(0x80000000U + MEM_MEM1_SIZE - 2U, 4U)); /* cruza o fim */
    EXPECT_NULL(mem_host_ptr(0xCC002000U, 4U));                      /* MMIO */
    EXPECT_NULL(mem_host_ptr(0x00001000U, 4U));                      /* sem tradução: não é MEM1 */
}

TEST(write_cancels_matching_reservation) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    cpu.reserve_valid = true;
    cpu.reserve_addr = 0x80006000U;
    mem_write32(&cpu, 0x80006100U, 1U); /* outro bloco de 32 bytes: mantém */
    EXPECT_TRUE(cpu.reserve_valid);
    mem_write8(&cpu, 0xC0006010U, 1U); /* mesmo bloco, pelo espelho: cancela */
    EXPECT_TRUE(!cpu.reserve_valid);
    guest_cpu_destroy(&cpu);
}

TEST(aram_is_separate_and_big_endian) {
    mem_aram_write32(0x100U, 0x0A0B0C0DU);
    EXPECT_EQ(mem_aram_read32(0x100U), 0x0A0B0C0DU);
    EXPECT_EQ(mem_aram_read16(0x102U), 0x0C0D);
    EXPECT_EQ(mem_aram_read8(0x100U), 0x0A);
    mem_aram_write8(MEM_ARAM_SIZE - 1U, 0x7F);
    EXPECT_EQ(mem_aram_read8(MEM_ARAM_SIZE - 1U), 0x7F);
    EXPECT_EQ(mem_read32(NULL, 0x80000100U), 0U); /* MEM1 não foi tocada */
}

int main(void) {
    RUN_TEST(write32_is_big_endian_in_host_memory);
    RUN_TEST(sub_word_reads_follow_big_endian_order);
    RUN_TEST(write64_and_read64_roundtrip);
    RUN_TEST(unaligned_access_is_supported);
    RUN_TEST(cached_and_uncached_mirrors_alias_the_same_memory);
    RUN_TEST(last_bytes_of_mem1_are_addressable);
    RUN_TEST(host_ptr_rejects_addresses_outside_mem1);
    RUN_TEST(write_cancels_matching_reservation);
    RUN_TEST(aram_is_separate_and_big_endian);
    mem_shutdown();
    return am_test_finish();
}
