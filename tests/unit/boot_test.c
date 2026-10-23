/* SPDX-License-Identifier: GPL-3.0-or-later */
/* boot.bin/bi2.bin/fst.bin sintéticos: só os campos que o boot lê. */
#include <string.h>

#include "am/boot.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/ppc_sys.h"
#include "am/timebase.h"

#include "am_test.h"

static u8 s_boot[0x440];
static u8 s_bi2[0x2000];
static u8 s_fst[0x40];

static void put32(u8 *p, u32 off, u32 v) {
    p[off] = (u8)(v >> 24);
    p[off + 1U] = (u8)(v >> 16);
    p[off + 2U] = (u8)(v >> 8);
    p[off + 3U] = (u8)v;
}

static BootFiles files(void) {
    memset(s_boot, 0, sizeof(s_boot));
    memset(s_bi2, 0, sizeof(s_bi2));
    memcpy(s_boot, "TEST01", 6);
    put32(s_boot, 0x42C, 0x40); /* tamanho máximo do FST */
    put32(s_bi2, 0x04, 0x01800000U);
    put32(s_bi2, 0x18, 1U); /* país: EUA */
    for (u32 i = 0; i < sizeof(s_fst); i++) {
        s_fst[i] = (u8)(i + 1U);
    }
    return (BootFiles){s_boot, sizeof(s_boot), s_bi2, sizeof(s_bi2), s_fst, sizeof(s_fst)};
}

TEST(low_memory_globals) {
    EXPECT_TRUE(mem_init());
    const BootFiles f = files();
    const BootConfig cfg = {.console_type = BOOT_CONSOLE_LATEST_DEVKIT, .rtc_seconds = 2};
    BootLayout layout;
    EXPECT_EQ(boot_prepare_memory(&f, &cfg, &layout), BOOT_OK);
    EXPECT_EQ(layout.fst_addr, 0x817FFFC0U); /* (0x81800000 - 0x40) & ~31 */
    EXPECT_EQ(layout.bi2_addr, 0x817FDFC0U);
    EXPECT_EQ(mem_read32(NULL, 0x80000000U), 0x54455354U); /* "TEST" */
    EXPECT_EQ(mem_read32(NULL, 0x80000020U), 0x0D15EA5EU);
    EXPECT_EQ(mem_read32(NULL, 0x80000028U), 0x01800000U);
    EXPECT_EQ(mem_read32(NULL, 0x8000002CU), 0x10000006U);
    EXPECT_EQ(mem_read32(NULL, 0x80000034U), 0x817FFFC0U);
    EXPECT_EQ(mem_read32(NULL, 0x8000003CU), 0x40U);
    EXPECT_EQ(mem_read32(NULL, 0x800000CCU), 0U);
    EXPECT_EQ(mem_read32(NULL, 0x800000ECU), 0x81800000U);
    EXPECT_EQ(mem_read32(NULL, 0x800000F4U), 0x817FDFC0U);
    EXPECT_EQ(mem_read32(NULL, 0x80000C00U), 0x4C000064U);
    EXPECT_EQ(mem_read64(NULL, 0x800030D8U), 81000000U);
    EXPECT_EQ(mem_read8(NULL, 0x817FFFC0U), 1U);
    EXPECT_EQ(mem_read32(NULL, 0x817FDFC4U), 0x01800000U);
}

TEST(rejects_inconsistent_files) {
    BootFiles f = files();
    const BootConfig cfg = {.console_type = BOOT_CONSOLE_RETAIL};
    BootLayout layout;
    f.bi2_size = 0x100;
    EXPECT_EQ(boot_prepare_memory(&f, &cfg, &layout), BOOT_ERR_BI2);
    f = files();
    put32(s_boot, 0x42C, 0x10);
    EXPECT_EQ(boot_prepare_memory(&f, &cfg, &layout), BOOT_ERR_FST);
    f = files();
    put32(s_bi2, 0x04, 0x03000000U);
    EXPECT_EQ(boot_prepare_memory(&f, &cfg, &layout), BOOT_ERR_MEMSIZE);
    f = files();
    f.boot_bin_size = 0x20;
    EXPECT_EQ(boot_prepare_memory(&f, &cfg, &layout), BOOT_ERR_BOOT_BIN);
}

TEST(cpu_state_of_the_bs2) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    ppc_sys_reset();
    const BootLayout layout = {.time_base = 77U};
    mem_write32(NULL, 0x800000CCU, 0U);
    boot_prepare_cpu(&cpu, 0x80003154U, &layout);
    EXPECT_EQ(cpu.pc, 0x80003154U);
    EXPECT_EQ(cpu.gpr[3], 0x80003154U);
    EXPECT_EQ(cpu.gpr[1], 0x81566550U);
    EXPECT_EQ(cpu.msr, 0x00002032U);
    EXPECT_EQ(cpu.hid2, 0xE0000000U);
    EXPECT_EQ(ppc_sys_spr(PPC_SPR_HID0), 0x0011C464U);
    EXPECT_EQ(ppc_sys_spr(PPC_SPR_IBAT0U + 10U), 0xC0001FFFU);
    EXPECT_EQ(timebase_read(&cpu), 77U);
}

int main(void) {
    RUN_TEST(low_memory_globals);
    RUN_TEST(rejects_inconsistent_files);
    RUN_TEST(cpu_state_of_the_bs2);
    mem_shutdown();
    return am_test_finish();
}
