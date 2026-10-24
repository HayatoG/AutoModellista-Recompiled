/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vozes do AX (audio/ax_voice.c): PBs sintéticos na MEM1 e amostras na ARAM. Confere o avanço do endereço, o fim de
 * amostra (running = 0), o laço, o envelope de volume, as atualizações por milissegundo, a lista encadeada de PBs, o
 * conversor de taxa linear, a mixagem com volume e rampa, o filtro passa-baixa e a saturação do ADPCM. Valores
 * esperados calculados à mão pelas fórmulas do Dolphin (AXVoice.h, DSPAccelerator.cpp). */
#include <string.h>

#include "am/ax_voice.h"
#include "am/mem.h"

#include "am_test.h"

#define PB_A         0x00120000U /* físico */
#define PB_B         0x00120200U
#define UPDATES      0x00121000U
#define ARAM_START   0x100U  /* em amostras de 16 bits */
#define FORMAT_PCM16 0x000AU /* tamanho 2 (16 bits), decodificação 2 (PCM) */
#define FORMAT_ADPCM 0x0000U

static AxMixBuffers s_mix;

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
    ax_voice_process_list(PB_A, &s_mix);
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
    ax_voice_process_list(PB_A, &s_mix);
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
    ax_voice_process_list(PB_A, &s_mix);
    EXPECT_EQ(pb_read(PB_A, AX_PB_RUNNING), 1U);
    EXPECT_EQ(pb_read32(PB_A, AX_PB_ADDR_CUR_HI), ARAM_START + (3U * 32U));
    EXPECT_EQ(pb_read32(PB_B, AX_PB_ADDR_CUR_HI), ARAM_START + (5U * 32U));
    EXPECT_EQ(ax_voice_stats()->frames, 1U);
    EXPECT_EQ(ax_voice_stats()->voice_frames, 8U);
}

static s16 s_seq_next;

static s16 seq_source(void *user) {
    (void)user;
    s_seq_next = (s16)(s_seq_next + 100);
    return s_seq_next;
}

TEST(linear_resampler_has_three_sample_delay_and_interpolates) {
    s16 out[10];
    s16 last[4] = {0, 0, 0, 0};
    s_seq_next = 0;
    /* Razão 0,5: uma entrada a cada duas saídas (100, 200, 300...). A interpolação usa as duas amostras mais antigas
     * do anel: 3 amostras de atraso na taxa de entrada. */
    const u32 pos = ax_resample(seq_source, NULL, out, 10, last, 0, 0x8000U, AX_SRC_LINEAR);
    const s16 expected[10] = {0, 0, 0, 0, 0, 0, 50, 100, 150, 200};
    for (u32 i = 0; i < 10U; i++) {
        EXPECT_TRUE(out[i] == expected[i]);
    }
    EXPECT_EQ(pos, 0U);
    EXPECT_TRUE(last[0] == 200 && last[1] == 300 && last[2] == 400 && last[3] == 500);
    /* Polifásico sem coeficientes = linear, como no Dolphin sem dsp_coef.bin. */
    s16 out2[10];
    s16 last2[4] = {0, 0, 0, 0};
    s_seq_next = 0;
    (void)ax_resample(seq_source, NULL, out2, 10, last2, 0, 0x8000U, AX_SRC_POLYPHASE);
    EXPECT_EQ(memcmp(out, out2, sizeof(out)), 0);
    /* "Nearest": uma entrada por saída, sem atraso. */
    s_seq_next = 0;
    (void)ax_resample(seq_source, NULL, out, 4, last, 0, 0, AX_SRC_NEAREST);
    EXPECT_TRUE(out[0] == 100 && out[3] == 400 && last[3] == 400);
}

TEST(mix_add_volume_ramp_clamp_and_dpop) {
    s32 out[3] = {10, 10, 10};
    const s16 in[3] = {1000, 1000, 1000};
    u16 volume = 0x4000; /* 0,5 */
    s16 dpop = 0;
    ax_mix_add(out, in, 3, &volume, 0x2000U, &dpop, true);
    EXPECT_TRUE(out[0] == 510 && out[1] == 760 && out[2] == 1010);
    EXPECT_EQ(volume, 0xA000U);
    EXPECT_TRUE(dpop == 1000);
    volume = 0x4000;
    ax_mix_add(out, in, 3, &volume, 0x2000U, &dpop, false); /* sem rampa: o delta não conta */
    EXPECT_EQ(volume, 0x4000U);
    EXPECT_TRUE(out[2] == 1510);
    const s16 loud[1] = {32767};
    s32 one[1] = {0};
    volume = 0xFFFF; /* ~2,0: satura em 16 bits antes de somar */
    ax_mix_add(one, loud, 1, &volume, 0, &dpop, false);
    EXPECT_TRUE(one[0] == 32767 && dpop == 32767);
}

/* Voz PCM16 constante (4096) com envelope 0,5 e mixer_control L+R: L com volume 1,0 e R com 0,5. */
TEST(voice_is_mixed_into_main_buffers) {
    EXPECT_TRUE(mem_init());
    ax_voice_reset();
    memset(&s_mix, 0, sizeof(s_mix));
    make_voice(PB_A, 0, 1000, false, true);
    for (u32 i = 0; i < 1000U; i++) {
        mem_aram_write16((ARAM_START + i) * 2U, 0x1000U);
    }
    pb_write(PB_A, AX_PB_VOL_ENV, 0x4000);
    pb_write(PB_A, AX_PB_VOL_ENV + 1U, 0);
    pb_write(PB_A, AX_PB_MIXER_CONTROL, 0x0003);
    pb_write(PB_A, AX_PB_MIXER + 0U, 0x8000); /* L */
    pb_write(PB_A, AX_PB_MIXER + 2U, 0x4000); /* R */
    ax_voice_process_list(PB_A, &s_mix);
    EXPECT_TRUE(s_mix.ch[AX_MAIN_L][0] == 0 && s_mix.ch[AX_MAIN_L][2] == 0); /* atraso de 3 do conversor */
    EXPECT_TRUE(s_mix.ch[AX_MAIN_L][3] == 2048 && s_mix.ch[AX_MAIN_L][159] == 2048);
    EXPECT_TRUE(s_mix.ch[AX_MAIN_R][3] == 1024 && s_mix.ch[AX_MAIN_R][159] == 1024);
    EXPECT_TRUE(s_mix.ch[AX_AUXA_L][100] == 0 && s_mix.ch[AX_MAIN_S][100] == 0);
    EXPECT_EQ(pb_read(PB_A, AX_PB_DPOP + 0U), 2048U); /* dpop L */
    EXPECT_EQ(pb_read(PB_A, AX_PB_DPOP + 3U), 1024U); /* dpop R */
    /* Passa-baixa a0 = b0 = 0,5: y = (x + y_anterior) / 2. */
    memset(&s_mix, 0, sizeof(s_mix));
    make_voice(PB_A, 0, 1000, false, true);
    for (u32 i = 0; i < 1000U; i++) {
        mem_aram_write16((ARAM_START + i) * 2U, 0x1000U);
    }
    pb_write(PB_A, AX_PB_VOL_ENV, 0x4000);
    pb_write(PB_A, AX_PB_VOL_ENV + 1U, 0);
    pb_write(PB_A, AX_PB_MIXER_CONTROL, 0x0001);
    pb_write(PB_A, AX_PB_MIXER + 0U, 0x8000);
    pb_write(PB_A, AX_PB_LPF_ON, 1);
    pb_write(PB_A, AX_PB_LPF_ON + 2U, 0x4000);
    pb_write(PB_A, AX_PB_LPF_ON + 3U, 0x4000);
    ax_voice_process_list(PB_A, &s_mix);
    EXPECT_TRUE(s_mix.ch[AX_MAIN_L][2] == 0 && s_mix.ch[AX_MAIN_L][3] == 1024);
    EXPECT_TRUE(s_mix.ch[AX_MAIN_L][4] == 1536 && s_mix.ch[AX_MAIN_L][5] == 1792);
    EXPECT_EQ(pb_read(PB_A, AX_PB_LPF_ON + 1U), (u16)s_mix.ch[AX_MAIN_L][159]);
}

/* Bug do M5: o ADPCM saturava em -0x7FFF; o Dolphin (SaturatingCast<s16>) vai até -0x8000. Uma amostra só (fim =
 * início), escala 2^12, nibble -8 e yn1 = -100 com coeficiente 1,0: -32768 - 100 satura em -32768. */
TEST(adpcm_saturates_at_minus_32768) {
    EXPECT_TRUE(mem_init());
    ax_voice_reset();
    memset(&s_mix, 0, sizeof(s_mix));
    const u32 start = 0x1002U; /* endereço em nibbles: byte 0x801, nibble alto */
    make_voice(PB_A, 0, 1, false, true);
    pb_write(PB_A, AX_PB_ADDR_FORMAT, FORMAT_ADPCM);
    pb_write32(PB_A, AX_PB_ADDR_LOOP_HI, start);
    pb_write32(PB_A, AX_PB_ADDR_END_HI, start);
    pb_write32(PB_A, AX_PB_ADDR_CUR_HI, start);
    pb_write(PB_A, AX_PB_ADPCM_PS, 0x0C);
    pb_write(PB_A, AX_PB_ADPCM_YN1, 0xFF9CU); /* -100 */
    pb_write(PB_A, AX_PB_ADPCM_COEFS, 0x0800);
    pb_write(PB_A, AX_PB_SRC_TYPE, AX_SRC_NEAREST);
    mem_aram_write8(0x801U, 0x80U);
    ax_voice_process_list(PB_A, &s_mix);
    EXPECT_EQ(pb_read(PB_A, AX_PB_ADPCM_YN1), 0x8000U);
    EXPECT_EQ(pb_read(PB_A, AX_PB_RUNNING), 0U);
}

int main(void) {
    RUN_TEST(voice_without_loop_ends_and_stops);
    RUN_TEST(voice_with_loop_wraps_to_loop_start);
    RUN_TEST(update_in_later_ms_starts_voice_and_list_is_walked);
    RUN_TEST(linear_resampler_has_three_sample_delay_and_interpolates);
    RUN_TEST(mix_add_volume_ramp_clamp_and_dpop);
    RUN_TEST(voice_is_mixed_into_main_buffers);
    RUN_TEST(adpcm_saturates_at_minus_32768);
    return am_test_finish();
}
