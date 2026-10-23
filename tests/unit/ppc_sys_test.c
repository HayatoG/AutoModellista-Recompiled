/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/ppc_sys.h"
#include "am/timebase.h"

#include "am_test.h"

/* Codificações montadas à mão: mfspr rD,SPR = 0x7C0002A6 | rD<<21 | spr_lo<<16 | spr_hi<<11. */
static u32 mfspr(u32 rd, u32 spr) {
    return 0x7C0002A6U | (rd << 21) | ((spr & 0x1FU) << 16) | ((spr >> 5) << 11);
}

static u32 mtspr(u32 rs, u32 spr) {
    return 0x7C0003A6U | (rs << 21) | ((spr & 0x1FU) << 16) | ((spr >> 5) << 11);
}

static void setup(CPUState *cpu) {
    EXPECT_TRUE(guest_cpu_create(cpu));
    ppc_sys_reset();
    timebase_reset(0);
}

TEST(fallback_advances_pc_past_the_instruction) {
    CPUState cpu;
    setup(&cpu);
    cpu.pc = 0x80003000U;
    cpu.gpr[3] = 0x12345678U;
    ppc_sys_fallback(&cpu, mtspr(3, 272), 0x80003000U); /* mtsprg0 r3 */
    EXPECT_EQ(cpu.pc, 0x80003004U);
    ppc_sys_fallback(&cpu, mfspr(4, 272), 0x80003004U);
    EXPECT_EQ(cpu.gpr[4], 0x12345678U);
    EXPECT_EQ(cpu.pc, 0x80003008U);
}

TEST(hid0_icfi_self_clears_and_hid2_masks) {
    CPUState cpu;
    setup(&cpu);
    ppc_sys_mtspr(&cpu, PPC_SPR_HID0, 0x0011C464U | PPC_HID0_ICFI, 0);
    EXPECT_EQ(ppc_sys_mfspr(&cpu, PPC_SPR_HID0, 0), 0x0011C464U);
    cpu.hid2 = 0xE0000000U;
    ppc_sys_mtspr(&cpu, PPC_SPR_HID2, 0xFFFFFFFFU, 0);
    EXPECT_EQ(cpu.hid2, 0xF0FF0000U);
    ppc_sys_mtspr(&cpu, PPC_SPR_GQR0 + 2U, 0x00040004U, 0);
    EXPECT_EQ(cpu.gqr[2], 0x00040004U);
    EXPECT_EQ(ppc_sys_mfspr(&cpu, PPC_SPR_PVR, 0), PPC_GEKKO_PVR);
}

TEST(dec_and_tb_go_through_timebase) {
    CPUState cpu;
    setup(&cpu);
    ppc_sys_mtspr(&cpu, PPC_SPR_TBL_W, 100U, 0);
    EXPECT_EQ(ppc_sys_mfspr(&cpu, PPC_SPR_TBL, 0), 100U);
    ppc_sys_mtspr(&cpu, PPC_SPR_DEC, 0x7FFFFFFFU, 0); /* DEC estava negativo: sem exceção */
    EXPECT_EQ(ppc_sys_mfspr(&cpu, PPC_SPR_DEC, 0), 0x7FFFFFFFU);
}

TEST(bat_remap_that_keeps_translation_is_accepted) {
    CPUState cpu;
    setup(&cpu);
    /* __OSInitMemoryProtection, configuração de 24 MiB: DBAT2 de 8 MiB em 0x81000000 -> físico 0x01000000. */
    ppc_sys_mtspr(&cpu, PPC_SPR_IBAT0U + 12U, 0U, 0);
    ppc_sys_mtspr(&cpu, PPC_SPR_IBAT0U + 13U, 0x01000002U, 0);
    ppc_sys_mtspr(&cpu, PPC_SPR_IBAT0U + 12U, 0x810000FFU, 0);
    EXPECT_EQ(ppc_sys_mfspr(&cpu, PPC_SPR_IBAT0U + 12U, 0), 0x810000FFU);
}

TEST(icbi_over_intact_code_is_counted) {
    CPUState cpu;
    setup(&cpu);
    static const u8 s_original[64] = {0x60, 0, 0, 0}; /* nop + zeros */
    for (u32 i = 0; i < 64U; i++) {
        mem_write8(NULL, 0x80004000U + i, s_original[i]);
    }
    EXPECT_TRUE(ppc_sys_protect_code(0x80004000U, 64U, s_original));
    cpu.gpr[3] = 0x80004020U;
    ppc_sys_fallback(&cpu, 0x7C001FACU, 0x80003000U); /* icbi 0,r3 */
    cpu.gpr[3] = 0x80000100U;
    ppc_sys_fallback(&cpu, 0x7C001FACU, 0x80003004U);
    ppc_sys_fallback(&cpu, 0x7C0018ACU, 0x80003008U); /* dcbf 0,r3 */
    const PpcSysStats *st = ppc_sys_stats();
    EXPECT_EQ(st->cache_ops[PPC_SYS_CACHE_ICBI], 2U);
    EXPECT_EQ(st->cache_ops[PPC_SYS_CACHE_DCBF], 1U);
    EXPECT_EQ(st->icbi_in_code, 1U);
    EXPECT_EQ(st->icbi_elsewhere, 1U);
    EXPECT_EQ(st->icbi_first[1], 0x80000100U);
}

TEST(segment_registers) {
    CPUState cpu;
    setup(&cpu);
    cpu.gpr[5] = 0x80000001U;
    ppc_sys_fallback(&cpu, 0x7CA301A4U, 0); /* mtsr 3,r5 */
    EXPECT_EQ(cpu.sr[3], 0x80000001U);
    ppc_sys_fallback(&cpu, 0x7CC304A6U, 0); /* mfsr r6,3 */
    EXPECT_EQ(cpu.gpr[6], 0x80000001U);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(fallback_advances_pc_past_the_instruction);
    RUN_TEST(hid0_icfi_self_clears_and_hid2_masks);
    RUN_TEST(dec_and_tb_go_through_timebase);
    RUN_TEST(bat_remap_that_keeps_translation_is_accepted);
    RUN_TEST(icbi_over_intact_code_is_counted);
    RUN_TEST(segment_registers);
    mem_shutdown();
    return am_test_finish();
}
