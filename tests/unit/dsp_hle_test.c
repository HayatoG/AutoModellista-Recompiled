/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Protocolo dos microcódigos do DSP em HLE (audio/dsp_hle.c) pelo barramento: boot pela ROM com troca por hash,
 * AX recebendo listas de comando, interrupções do DSP e DMA de áudio. */
#include "am/bus.h"
#include "am/dsp.h"
#include "am/dsp_hle.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/timebase.h"

#include "am_test.h"

#define IRAM_SRC 0x80100000U
#define CMDLIST  0x80110000U
#define AX_HASH  0x07F88145U /* um dos AX do UCodeFactory do Dolphin */

static void run_cycles(CPUState *cpu, s64 cycles) {
    timebase_begin_slice(cpu);
    cpu->downcount -= cycles;
    timebase_end_slice(cpu);
    bus_update();
}

static void send(u32 mail) {
    bus_write(0xCC005000U, mail >> 16, 2);
    bus_write(0xCC005002U, mail & 0xFFFFU, 2);
}

static u32 receive(void) {
    const u32 hi = (u32)bus_read(0xCC005004U, 2);
    return (hi << 16) | (u32)bus_read(0xCC005006U, 2);
}

/* O hash Ector é xor + rotação: dá para montar bytes com o hash desejado de trás para frente. */
static u32 make_iram_with_hash(GuestAddr addr, u32 target) {
    u8 bytes[16];
    u32 n = 0;
    u32 crc = target;
    while (crc != 0U && n < sizeof(bytes)) {
        const u32 r = (crc >> 3) | (crc << 29);
        bytes[n++] = (u8)r;
        crc = r & ~0xFFU;
    }
    for (u32 i = 0; i < n; i++) {
        mem_write8(NULL, addr + i, bytes[n - 1U - i]);
    }
    return n;
}

TEST(hash_ector_matches_definition) {
    mem_write8(NULL, IRAM_SRC, 0x01);
    mem_write8(NULL, IRAM_SRC + 1U, 0x02);
    EXPECT_EQ(dsp_hle_hash_ector(IRAM_SRC, 2), 0x50U); /* ((1 << 3) ^ 2) << 3 */
    const u32 len = make_iram_with_hash(IRAM_SRC, AX_HASH);
    EXPECT_EQ(dsp_hle_hash_ector(IRAM_SRC, len), AX_HASH);
}

TEST(rom_boots_ax_and_ax_answers_command_lists) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    pi_write(PI_REG_INTMR, PI_INT_DSP, 4);
    bus_write(0xCC00500AU, 0x0100U | 0x0800U, 2); /* tira o HALT, máscara da interrupção do DSP, DSPInit continua */
    EXPECT_EQ(receive(), 0x8071FEEDU);            /* ROM pronta */
    const u32 len = make_iram_with_hash(IRAM_SRC, AX_HASH);
    const u32 boot[] = {
        0x80F3A001U, IRAM_SRC & 0x03FFFFFFU, 0x80F3C002U, 0, 0x80F3A002U, len, 0x80F3B002U, 0, 0x80F3D001U, 0x10};
    for (u32 i = 0; i < sizeof(boot) / sizeof(boot[0]); i++) {
        send(boot[i]);
        EXPECT_EQ(bus_read(0xCC005000U, 2) & 0x8000U, 0U); /* bit 31 do mailbox apaga: "recebido" */
    }
    EXPECT_EQ(dsp_hle_kind(), DSP_HLE_KIND_AX);
    EXPECT_EQ(dsp_hle_stats()->ucode_crc, AX_HASH);
    run_cycles(&cpu, 10);
    EXPECT_TRUE(pi_external_pending()); /* DSP_INIT com interrupção */
    EXPECT_EQ(receive(), 0xDCD10000U);
    bus_write(0xCC00500AU, 0x0900U | 0x0080U, 2); /* apaga a interrupção do DSP (DSPInit continua 1) */
    EXPECT_TRUE(!pi_external_pending());
    /* Lista: PB_ADDR, PROCESS, END. */
    const u16 list[] = {0x0002, 0x8012, 0x3456, 0x0003, 0x000F};
    for (u32 i = 0; i < 5U; i++) {
        mem_write16(NULL, CMDLIST + (i * 2U), list[i]);
    }
    send(0xBABE0005U);
    send(CMDLIST & 0x03FFFFFFU);
    run_cycles(&cpu, 2499);
    EXPECT_TRUE(!pi_external_pending());
    run_cycles(&cpu, 1);
    EXPECT_TRUE(pi_external_pending());
    EXPECT_EQ(receive(), 0xDCD10002U); /* DSP_YIELD */
    EXPECT_EQ(dsp_hle_stats()->command_lists, 1U);
    EXPECT_EQ(dsp_hle_stats()->command_words, 5U);
    bus_write(0xCC00500AU, 0x0900U | 0x0080U, 2);
    send(0xCDD10003U); /* MAIL_CONTINUE: volta a esperar lista */
    send(0xBABE0005U);
    send(CMDLIST & 0x03FFFFFFU);
    EXPECT_EQ(dsp_hle_stats()->command_lists, 2U);
}

TEST(audio_dma_raises_aid_per_block_run) {
    CPUState cpu;
    EXPECT_TRUE(guest_cpu_create(&cpu));
    timebase_reset(0);
    bus_reset();
    bus_write(0xCC005030U, 0x0010U, 2); /* início 0x00100000 */
    bus_write(0xCC005032U, 0x0000U, 2);
    bus_write(0xCC005036U, 0x8000U | 4U, 2); /* 4 blocos de 32 bytes, habilitado */
    run_cycles(&cpu, 200);                   /* o Dolphin pede uma AID 200 ciclos depois de ligar */
    EXPECT_EQ(bus_read(0xCC00500AU, 2) & 0x0008U, 0x0008U);
    EXPECT_EQ(dsp_audio_dma_interrupts(), 1U);
    run_cycles(&cpu, (s64)4 * 121392); /* 4 blocos a 32 kHz */
    EXPECT_EQ(dsp_audio_dma_interrupts(), 2U);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(hash_ector_matches_definition);
    RUN_TEST(rom_boots_ax_and_ax_answers_command_lists);
    RUN_TEST(audio_dma_raises_aid_per_block_run);
    mem_shutdown();
    return am_test_finish();
}
