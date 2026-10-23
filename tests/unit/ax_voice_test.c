/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vozes do AX sem mixagem (audio/ax_voice.c): PBs sintéticos na MEM1 e amostras PCM16 na ARAM. Confere o avanço do
 * endereço, o fim de amostra (running = 0), o laço, o envelope de volume, as atualizações por milissegundo e a lista
 * encadeada de PBs. */
#include "am/ax_voice.h"
#include "am/mem.h"

#include "am_test.h"

#define PB_A         0x00120000U /* físico */
#define PB_B         0x00120200U
#define UPDATES      0x00121000U
#define ARAM_START   0x100U  /* em amostras de 16 bits */
#define FORMAT_PCM16 0x000AU /* tamanho 2 (16 bits), decodificação 2 (PCM) */

static void pb_write(u32 pb, u32 word, u16 value) {
    mem_write16(NULL, MEM_CACHED_BASE | (pb + (word * 2U)), value);
}

static u16 pb_read(u32 pb, u32 word) {
    return mem_read16(NULL, MEM_CACHED_BASE | (pb + (word * 2U)));
}

static void pb_write32(u32 pb, u32 word, u32 value) {
    pb_write(pb, word, (u16)(value >> 16));
    pb_write(pb, word + 1U, (u16)value);
}

static u32 pb_read32(u32 pb, u32 word) {
    return ((u32)pb_read(pb, word) << 16) | pb_read(pb, word + 1U);
}

/* Voz PCM16 de `samples` amostras a partir de ARAM_START, razão 1,0 (uma amostra por saída), conversor linear. */
static void make_voice(u32 pb, u32 next, u32 samples, bool looping, bool running) {
    for (u32 i = 0; i < AX_PB_WORDS; i++) {
        pb_write(pb, i, 0);
    }
    pb_write32(pb, AX_PB_NEXT_HI, next);
    pb_write(pb, AX_PB_SRC_TYPE, 1);
    pb_write(pb, AX_PB_RUNNING, running ? 1U : 0U);
    pb_write32(pb, AX_PB_UPDATES + 5U, UPDATES);
    pb_write(pb, AX_PB_VOL_ENV + 1U, 1); /* delta 1 por amostra */
    pb_write(pb, AX_PB_ADDR_LOOPING, looping ? 1U : 0U);
    pb_write(pb, AX_PB_ADDR_FORMAT, FORMAT_PCM16);
    pb_write(pb, AX_PB_ADPCM_GAIN, 0x0800); /* ganho 1,0 com o deslocamento de 11 bits do formato */
    pb_write32(pb, AX_PB_ADDR_LOOP_HI, ARAM_START);
    pb_write32(pb, AX_PB_ADDR_END_HI, ARAM_START + samples - 1U);
    pb_write32(pb, AX_PB_ADDR_CUR_HI, ARAM_START);
    pb_write32(pb, AX_PB_SRC_RATIO_HI, 0x00010000U);
    for (u32 i = 0; i < samples; i++) {
        mem_aram_write16((ARAM_START + i) * 2U, (u16)(i + 1U));
    }
}

TEST(voice_without_loop_ends_and_stops) {
    EXPECT_TRUE(mem_init());
    ax_voice_reset();
    make_voice(PB_A, 0, 100, false, true);
    ax_voice_process_list(PB_A);
    /* 5 ms x 32 amostras = 160 saídas; a amostra acaba na 100ª, durante o 4º milissegundo. */
    EXPECT_EQ(pb_read(PB_A, AX_PB_RUNNING), 0U);
    EXPECT_EQ(pb_read32(PB_A, AX_PB_ADDR_CUR_HI), ARAM_START);
    EXPECT_EQ(pb_read(PB_A, AX_PB_VOL_ENV), 4U * 32U);
    EXPECT_EQ(pb_read(PB_A, AX_PB_ADPCM_YN1), 100U); /* última amostra lida */
    EXPECT_EQ(ax_voice_stats()->samples, 100U);
    EXPECT_EQ(ax_voice_stats()->ended, 1U);
    EXPECT_EQ(ax_voice_stats()->voice_frames, 4U);
}

TEST(voice_with_loop_wraps_to_loop_start) {
    EXPECT_TRUE(mem_init());
    ax_voice_reset();
    make_voice(PB_A, 0, 100, true, true);
    ax_voice_process_list(PB_A);
    EXPECT_EQ(pb_read(PB_A, AX_PB_RUNNING), 1U);
    EXPECT_EQ(pb_read32(PB_A, AX_PB_ADDR_CUR_HI), ARAM_START + 60U);
    EXPECT_EQ(pb_read(PB_A, AX_PB_VOL_ENV), 5U * 32U);
    EXPECT_EQ(ax_voice_stats()->samples, 160U);
    EXPECT_EQ(ax_voice_stats()->loops, 1U);
    EXPECT_EQ(ax_voice_stats()->ended, 0U);
}

TEST(update_in_later_ms_starts_voice_and_list_is_walked) {
    EXPECT_TRUE(mem_init());
    ax_voice_reset();
    make_voice(PB_A, PB_B, 1000, false, false);
    make_voice(PB_B, 0, 1000, false, true);
    /* PB_A: nenhuma atualização no ms 0 e 1, uma no ms 2 (running = 1). */
    pb_write(PB_A, AX_PB_UPDATES + 2U, 1);
    mem_write16(NULL, MEM_CACHED_BASE | UPDATES, AX_PB_RUNNING);
    mem_write16(NULL, MEM_CACHED_BASE | (UPDATES + 2U), 1);
    ax_voice_process_list(PB_A);
    EXPECT_EQ(pb_read(PB_A, AX_PB_RUNNING), 1U);
    EXPECT_EQ(pb_read32(PB_A, AX_PB_ADDR_CUR_HI), ARAM_START + (3U * 32U));
    EXPECT_EQ(pb_read32(PB_B, AX_PB_ADDR_CUR_HI), ARAM_START + (5U * 32U));
    EXPECT_EQ(ax_voice_stats()->frames, 1U);
    EXPECT_EQ(ax_voice_stats()->voice_frames, 8U);
}

int main(void) {
    RUN_TEST(voice_without_loop_ends_and_stops);
    RUN_TEST(voice_with_loop_wraps_to_loop_start);
    RUN_TEST(update_in_later_ms_starts_voice_and_list_is_walked);
    return am_test_finish();
}
