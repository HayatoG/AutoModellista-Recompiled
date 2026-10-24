/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Memory card do slot A (hw/memcard.c e hw/exi_card.c): formato do cartão novo, comandos de EXI e o tempo de fim de
 * comando e de DMA, pelos registradores como o SDK os usa (EXIGetID, EXIDma, __CARDEraseSector...). */
#include <string.h>

#include "am/bus.h"
#include "am/exi.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/memcard.h"
#include "am/pi.h"
#include "am/timebase.h"

#include "am_test.h"

#define EXI0    0xCC006800U
#define SR      (EXI0 + 0x00U)
#define DMA_ADR (EXI0 + 0x04U)
#define DMA_LEN (EXI0 + 0x08U)
#define CR      (EXI0 + 0x0CU)
#define IMM     (EXI0 + 0x10U)

static u32 r32(u32 a) {
    return (u32)bus_read(a, 4);
}

static void w32(u32 a, u32 v) {
    bus_write(a, v, 4);
}

static CPUState s_cpu;

static void run_cycles(s64 cycles) {
    timebase_begin_slice(&s_cpu);
    s_cpu.downcount -= cycles;
    timebase_end_slice(&s_cpu);
    bus_update();
}

static void new_machine(void) {
    EXPECT_TRUE(memcard_create(MEMCARD_DEFAULT_MBIT, 0));
    (void)memcard_take_dirty();
    timebase_reset(0);
    bus_reset();
}

/* Seleciona (cs = 1) ou solta (cs = 0) o aparelho 0 do canal 0, mantendo as máscaras de interrupção. */
static void cs(u32 on) {
    w32(SR, (r32(SR) & 0x0405U) | (on << 7));
}

static u32 imm(u32 data, u32 len, u32 rw) {
    w32(IMM, data);
    w32(CR, 1U | (rw << 2) | ((len - 1U) << 4));
    return r32(IMM);
}

static void command(u32 cmd_and_addr, u32 len) { /* um comando de escrita com endereço, como o SDK */
    imm(cmd_and_addr, len, 1);
}

static void read_array_setup(u32 addr) { /* 0x52, AD1..BA, depois 4 bytes de enchimento */
    cs(1);
    command(0x52000000U | ((addr >> 17) << 16) | (((addr >> 9) & 0xFFU) << 8) | ((addr >> 7) & 3U), 4);
    command(addr & 0x7FU, 1);
}

TEST(format_matches_dolphin_gcmemcard) {
    EXPECT_TRUE(memcard_size_valid(512U * 1024U));
    EXPECT_TRUE(!memcard_size_valid(512U * 1024U + 1U));
    EXPECT_TRUE(memcard_create(4, 0x12345678U));
    size_t size = 0;
    const u8 *m = memcard_image(&size);
    EXPECT_EQ(size, (size_t)(59U + 5U) * 0x2000U);
    EXPECT_EQ(m[0x22], 0U);
    EXPECT_EQ(m[0x23], 4U); /* tamanho em Mbit */
    EXPECT_EQ(m[0x13], 0x78U);
    EXPECT_EQ(m[0x24], 0U); /* Windows-1252 */
    EXPECT_EQ(m[0x30], 0xFFU);
    /* diretórios (blocos 1 e 2) vazios: contador 0 e soma 0xF003; BATs (3 e 4) com 59 blocos livres */
    EXPECT_EQ(m[0x2000 + 0x1FFC], 0xF0U);
    EXPECT_EQ(m[0x2000 + 0x1FFD], 0x03U);
    EXPECT_EQ(memcmp(m + 0x2000, m + 0x4000, 0x2000), 0);
    EXPECT_EQ(m[0x6000 + 6], 0U);
    EXPECT_EQ(m[0x6000 + 7], 59U);
    EXPECT_EQ(m[0x6000 + 9], 4U); /* último bloco alocado */
    EXPECT_EQ(memcmp(m + 0x6000, m + 0x8000, 0x2000), 0);
    EXPECT_EQ(m[0xA000], 0xFFU); /* dados */
    EXPECT_EQ(m[size - 1U], 0xFFU);
    /* mesmo horário = mesmo cartão; o serial muda com o horário */
    u8 again[64 * 0x2000];
    memcard_format(again, 4, (const u8 *)"DOLPHINSLOTA", 0x12345678U);
    EXPECT_EQ(memcmp(again, m, sizeof(again)), 0);
    memcard_format(again, 4, (const u8 *)"DOLPHINSLOTA", 0x12345679U);
    EXPECT_NE(memcmp(again, m, 12), 0);
    memcard_eject();
}

TEST(without_card_slot_a_is_empty) {
    memcard_eject();
    bus_reset();
    EXPECT_EQ(r32(SR) & 0x1000U, 0U); /* EXT */
    cs(1);
    EXPECT_EQ(imm(0, 4, 0), 0U);
}

TEST(card_present_id_and_flash_id) {
    new_machine();
    EXPECT_NE(r32(SR) & 0x1000U, 0U);
    EXPECT_EQ(r32(EXI0 + 0x14U + SR - EXI0) & 0x1000U, 0U); /* canal 1: slot B vazio */
    cs(1);
    imm(0x00000000U, 2, 1);      /* EXIGetID: comando 0x0000 */
    EXPECT_EQ(imm(0, 4, 0), 4U); /* cartão de 4 Mbit */
    cs(0);
    cs(1);
    imm(0x85000000U, 2, 1); /* ReadID */
    EXPECT_EQ(imm(0, 2, 0) >> 16, 0xC221U);
    cs(0);
    /* SRAM: o flash ID do slot A vem do cabeçalho do cartão (cartão formatado com horário 0 e "DOLPHINSLOTA") */
    const u8 *sram = exi_sram();
    u8 csum = 0;
    for (u32 i = 0; i < 12U; i++) {
        csum = (u8)(csum + sram[0x18U + i]);
    }
    EXPECT_EQ(sram[0x3E], (u8)(csum ^ 0xFFU));
    /* O flash ID da SRAM desfaz o embaralhamento do serial: com qualquer horário de formatação dá o mesmo ID */
    EXPECT_TRUE(memcard_create(MEMCARD_DEFAULT_MBIT, 0x5F3A91C7ULL));
    bus_reset();
    EXPECT_EQ(memcmp(&exi_sram()[0x18], "DOLPHINSLOTA", 12), 0);
    size_t size = 0;
    EXPECT_NE(memcmp(memcard_image(&size), "DOLPHINSLOTA", 12), 0); /* o serial no cartão é outro */
    memcard_eject();
}

TEST(read_array_reads_header_by_byte_and_by_dma) {
    new_machine();
    size_t size = 0;
    const u8 *m = memcard_image(&size);
    read_array_setup(0x2000);
    imm(0, 4, 0); /* 4 bytes de latência (o SDK manda 4) */
    EXPECT_EQ(imm(0, 4, 0), ((u32)m[0x2000] << 24) | ((u32)m[0x2001] << 16) | ((u32)m[0x2002] << 8) | m[0x2003]);
    cs(0);
    /* DMA: o endereço do cartão vem do comando; o TCINT só chega 926 ciclos por byte depois */
    read_array_setup(0x2000);
    imm(0, 4, 0);
    w32(SR, r32(SR) | 0x0004U); /* TCINTMASK */
    u8 *dst = mem_host_ptr(MEM_CACHED_BASE | 0x00100000U, 512);
    memset(dst, 0, 512);
    w32(DMA_ADR, 0x00100000U);
    w32(DMA_LEN, 512);
    w32(CR, 1U | 2U); /* TSTART | DMA | leitura */
    EXPECT_EQ(memcmp(dst, m + 0x2000, 512), 0);
    EXPECT_EQ(r32(CR) & 1U, 0U);
    EXPECT_EQ(r32(SR) & 0x8U, 0U);
    run_cycles(512 * 926 - 1000);
    EXPECT_EQ(r32(SR) & 0x8U, 0U);
    run_cycles(2000);
    EXPECT_NE(r32(SR) & 0x8U, 0U); /* TCINT */
    EXPECT_TRUE(pi_external_pending() || (r32(0xCC003000U) & PI_INT_EXI) != 0U);
    w32(SR, r32(SR) | 0x8U);
    cs(0);
}

TEST(erase_and_program_then_dma_write_timing) {
    new_machine();
    const u32 addr = 6U * 0x2000U; /* primeiro bloco de dados do jogo */
    /* SectorErase: F1 AD1 AD2, solta o CS -> ocupado até 5000 ciclos depois */
    cs(1);
    command(0xF1000000U | ((addr >> 17) << 16) | (((addr >> 9) & 0xFFU) << 8), 3);
    cs(0);
    cs(1);
    imm(0x83000000U, 2, 1);
    EXPECT_EQ((imm(0, 1, 0) >> 24) & 0xC1U, 0xC0U); /* ocupado, desbloqueado, não pronto */
    cs(0);
    run_cycles(5001);
    cs(1);
    imm(0x83000000U, 2, 1);
    EXPECT_EQ((imm(0, 1, 0) >> 24) & 0xC1U, 0x41U);
    cs(0);
    /* PageProgram de 128 bytes em duas escritas imediatas, como o SDK em __CARDWritePage */
    cs(1);
    command(0xF2000000U | ((addr >> 17) << 16) | (((addr >> 9) & 0xFFU) << 8) | ((addr >> 7) & 3U), 4);
    command(addr & 0x7FU, 1);
    for (u32 i = 0; i < 32U; i++) {
        command(0x41424344U + i * 0x01010101U, 4);
    }
    cs(0);
    size_t size = 0;
    const u8 *m = memcard_image(&size);
    EXPECT_EQ(m[addr], 0x41U);
    EXPECT_EQ(m[addr + 127U], 0x44U + 31U);
    EXPECT_TRUE(memcard_take_dirty());
    EXPECT_TRUE(!memcard_take_dirty());
    /* DMA de escrita: 4937 ciclos por byte */
    cs(1);
    command(0xF2000000U | ((addr >> 17) << 16) | (((addr >> 9) & 0xFFU) << 8) | ((addr >> 7) & 3U), 4);
    command((addr + 0x200U) & 0x7FU, 1);
    u8 *src = mem_host_ptr(MEM_CACHED_BASE | 0x00200000U, 128);
    memset(src, 0x5A, 128);
    w32(SR, r32(SR) | 0x0004U);
    w32(DMA_ADR, 0x00200000U);
    w32(DMA_LEN, 128);
    w32(CR, 1U | 2U | (1U << 2)); /* escrita por DMA */
    EXPECT_EQ(m[addr], 0x5AU);
    EXPECT_EQ(r32(SR) & 0x8U, 0U);
    run_cycles(128 * 4937 - 1);
    EXPECT_EQ(r32(SR) & 0x8U, 0U);
    run_cycles(2);
    EXPECT_NE(r32(SR) & 0x8U, 0U);
    cs(0);
    memcard_eject();
}

TEST(card_interrupt_follows_set_interrupt_switch) {
    new_machine();
    cs(1);
    imm(0x81010000U, 2, 1); /* SetInterrupt: liga */
    cs(0);
    w32(SR, r32(SR) | 0x1U); /* EXIINTMASK */
    EXPECT_EQ(r32(SR) & 0x2U, 0U);
    const u32 addr = 7U * 0x2000U;
    cs(1);
    command(0xF1000000U | ((addr >> 17) << 16) | (((addr >> 9) & 0xFFU) << 8), 3);
    cs(0); /* apagar setor: fim de comando 5000 ciclos depois levanta a interrupção do cartão */
    run_cycles(5001);
    EXPECT_NE(r32(SR) & 0x2U, 0U); /* EXIINT */
    cs(1);
    imm(0x89000000U, 1, 1); /* ClearStatus */
    cs(0);
    memcard_eject();
}

int main(void) {
    EXPECT_TRUE(mem_init());
    timebase_reset(0);
    RUN_TEST(format_matches_dolphin_gcmemcard);
    RUN_TEST(without_card_slot_a_is_empty);
    RUN_TEST(card_present_id_and_flash_id);
    RUN_TEST(read_array_reads_header_by_byte_and_by_dma);
    RUN_TEST(erase_and_program_then_dma_write_timing);
    RUN_TEST(card_interrupt_follows_set_interrupt_switch);
    mem_shutdown();
    return am_test_finish();
}
