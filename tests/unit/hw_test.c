/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Dispositivos MMIO (hw/) pelo barramento, como o jogo os vê em 0xCC00xxxx. Sequências sintéticas. */
#include <string.h>

#include "am/bus.h"
#include "am/exi.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/timebase.h"

#include "am_test.h"

static u32 r32(u32 a) {
    return (u32)bus_read(a, 4);
}

static u16 r16(u32 a) {
    return (u16)bus_read(a, 2);
}

static void w32(u32 a, u32 v) {
    bus_write(a, v, 4);
}

static void w16(u32 a, u16 v) {
    bus_write(a, v, 2);
}

/* Faz o tempo passar como se a CPU tivesse rodado `cycles` ciclos. */
static void run_cycles(CPUState *cpu, s64 cycles) {
    timebase_begin_slice(cpu);
    cpu->downcount -= cycles;
    timebase_end_slice(cpu);
    bus_update();
}

TEST(pi_cause_mask_and_ack) {
    bus_reset();
    EXPECT_EQ(r32(0xCC003000U), PI_INT_RST_BUTTON | PI_INT_VI);
    EXPECT_TRUE(!pi_external_pending());
    w32(0xCC003004U, PI_INT_VI);
    EXPECT_TRUE(pi_external_pending());
    w32(0xCC003000U, PI_INT_VI);
    EXPECT_TRUE(!pi_external_pending());
    EXPECT_EQ(r32(0xCC00302CU), PI_FLIPPER_REV_C);
    EXPECT_EQ(r16(0xCC003002U), (u16)(PI_INT_RST_BUTTON & 0xFFFFU));
}

TEST(mi_is_storage_with_32bit_split) {
    bus_reset();
    w32(0xCC004010U, 0x12345678U);
    EXPECT_EQ(r16(0xCC004010U), 0x1234U);
    EXPECT_EQ(r16(0xCC004012U), 0x5678U);
    w16(0xCC00401CU, 0x00FFU);
    EXPECT_EQ(r16(0xCC00401CU), 0x00FFU);
}

TEST(dsp_reset_init_ucode_and_aram_dma) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    EXPECT_EQ(r16(0xCC00500AU) & 0x0C07U, 0x0804U); /* HALT | INIT */
    w16(0xCC00500AU, 0x08AD);                       /* reset, ainda parado */
    EXPECT_EQ(r16(0xCC00500AU) & 1U, 0U);           /* RESET se apaga */
    /* ARAM DMA de 32 bytes, MRAM -> ARAM */
    mem_write32(NULL, 0x81000000U, 0xCAFEBABEU);
    w32(0xCC005020U, 0x01000000U);
    w32(0xCC005024U, 0x00000000U);
    w32(0xCC005028U, 32U);
    EXPECT_EQ(mem_aram_read32(0), 0xCAFEBABEU);
    EXPECT_EQ(r16(0xCC00500AU) & 0x0220U, 0x0200U); /* DMA em andamento */
    run_cycles(&cpu, 246);
    EXPECT_EQ(r16(0xCC00500AU) & 0x0220U, 0x0020U); /* fim: interrupção ARAM */
    w16(0xCC00500AU, (u16)((r16(0xCC00500AU) & ~0x0C07U) | 0x0004U));
    EXPECT_EQ(r16(0xCC00500AU) & 0x0020U, 0U); /* escrever 1 apaga */
    /* tira o HALT e o DSPInit: o microcódigo de inicialização manda 0x80544348 */
    w16(0xCC00500AU, 0x0000);
    const u32 mail = ((u32)r16(0xCC005004U) << 16) | r16(0xCC005006U);
    EXPECT_EQ(mail, 0x80544348U);
    EXPECT_EQ(r16(0xCC005004U) & 0x8000U, 0U); /* lido: bit 31 apaga */
}

static void exi_imm(u32 ch, u32 data, u32 len, u32 rw) {
    w32(0xCC006810U + (ch * 0x14U), data);
    w32(0xCC00680CU + (ch * 0x14U), 1U | (rw << 2) | ((len - 1U) << 4));
}

TEST(exi_ipl_sram_uart_and_empty_slots) {
    bus_reset();
    exi_set_rtc(0x01020304U);
    EXPECT_EQ(r32(0xCC006800U) & 0x1000U, 0U); /* EXT: slot A vazio */
    w32(0xCC006800U, 2U << 7);                 /* CS do IPL */
    exi_imm(0, 0x20000000U, 4, 1);             /* comando: ler SRAM (endereço 0x800000) a partir do byte 0 */
    EXPECT_EQ(r32(0xCC00680CU) & 1U, 0U);
    EXPECT_EQ(r32(0xCC006800U) & 0x8U, 0x8U); /* TCINT */
    exi_imm(0, 0, 4, 0);
    EXPECT_EQ(r32(0xCC006810U), 0x01020304U); /* RTC nos 4 primeiros bytes */
    exi_imm(0, 0, 4, 0);
    EXPECT_EQ(r32(0xCC006810U) >> 16, (u32)((exi_sram()[4] << 8) | exi_sram()[5]));
    w32(0xCC006800U, 0);
    /* UART: comando de escrita em 0x800400, depois "ok\r" */
    w32(0xCC006800U, 2U << 7);
    exi_imm(0, 0xA0010000U, 4, 1);
    exi_imm(0, 0x6F6B0D00U, 4, 1);
    w32(0xCC006800U, 0);
    EXPECT_TRUE(strncmp(exi_uart_text(), "ok\r", 3) == 0);
    /* canal 1 sem aparelho: leitura imediata dá 0 */
    w32(0xCC006814U, 1U << 7);
    exi_imm(1, 0xFFFFFFFFU, 4, 0);
    EXPECT_EQ(r32(0xCC006824U), 0U);
}

TEST(si_get_type_port1_controller_port2_empty) {
    bus_reset();
    w32(0xCC006480U, 0x00000000U);                          /* comando 0x00 (status/tipo) */
    w32(0xCC006434U, 0x40000001U | (1U << 16) | (3U << 8)); /* TCINTMSK, canal 0, out 1, in 3, TSTART */
    EXPECT_EQ(r32(0xCC006434U) & 1U, 0U);
    EXPECT_EQ(r32(0xCC006480U) >> 8, 0x00090000U);
    EXPECT_TRUE((pi_cause() & PI_INT_SI) != 0U);
    w32(0xCC006434U, 0x80000000U); /* apaga TCINT */
    EXPECT_TRUE((pi_cause() & PI_INT_SI) == 0U);
    w32(0xCC006434U, 0x00000003U | (1U << 16) | (3U << 8)); /* canal 1 */
    EXPECT_EQ(r32(0xCC006434U) & 0x20000000U, 0x20000000U); /* COMERR */
    EXPECT_EQ(r32(0xCC006438U) & 0x00080000U, 0x00080000U); /* NOREP1 */
}

TEST(di_inquiry_completes_after_latency) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    w32(0xCC006000U, 0x08U); /* TCINTMASK */
    w32(0xCC006008U, 0x12000000U);
    w32(0xCC006014U, 0x00002000U);
    w32(0xCC006018U, 0x20U);
    w32(0xCC00601CU, 3U);
    EXPECT_EQ(mem_read32(NULL, 0x80002004U), 0x20060526U);
    EXPECT_EQ(r32(0xCC006000U) & 0x10U, 0U);
    run_cycles(&cpu, 145800);
    EXPECT_EQ(r32(0xCC006000U) & 0x10U, 0x10U);
    EXPECT_EQ(r32(0xCC00601CU) & 1U, 0U);
    EXPECT_TRUE((pi_cause() & PI_INT_DI) != 0U);
    EXPECT_EQ(r32(0xCC006024U), 1U);
    EXPECT_EQ(r32(0xCC006004U), 0U); /* tampa fechada */
}

TEST(ai_and_vi_minimal) {
    bus_reset();
    w32(0xCC006C00U, 0x04U | 0x08U);
    EXPECT_EQ(r32(0xCC006C00U), 0x04U); /* AIINT=1 escrito apaga */
    EXPECT_EQ(r16(0xCC00206CU), 1U);    /* VI_CLOCK: 54 MHz (NTSC) */
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(pi_cause_mask_and_ack);
    RUN_TEST(mi_is_storage_with_32bit_split);
    RUN_TEST(dsp_reset_init_ucode_and_aram_dma);
    RUN_TEST(exi_ipl_sram_uart_and_empty_slots);
    RUN_TEST(si_get_type_port1_controller_port2_empty);
    RUN_TEST(di_inquiry_completes_after_latency);
    RUN_TEST(ai_and_vi_minimal);
    mem_shutdown();
    return am_test_finish();
}
