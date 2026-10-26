/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Conversão do XFB (YUV 4:2:2 na memória do guest) para RGBA da janela (gfx/gx_efb.c: gx_efb_xfb_to_rgba) contra a
 * fórmula original em double, bit a bit: as 16.777.216 combinações de Y, U e V, um XFB aleatório com passo maior que a
 * largura, larguras ímpares e pequenas, e linhas que a leitura direta da MEM1 não cobre (cruzando o fim da MEM1 para a
 * janela dos RELs), que continuam pelo mem_read32. */
#include <stdlib.h>
#include <string.h>

#include "am/gx_efb.h"
#include "am/gx_util.h"
#include "am/mem.h"
#include "am/rel_map.h"

#include "am_test.h"

/* A conversão original, palavra por palavra e em double, copiada do gx_efb.c antes da otimização. */
static u8 ref_channel(f64 v) {
    const s32 i = (s32)v;
    return (u8)gx_clamp_s32(i, 0, 255);
}

static void ref_xfb_to_rgba(GuestAddr addr, u32 width, u32 height, u32 stride, u8 *rgba) {
    for (u32 y = 0; y < height; y++) {
        const GuestAddr line = MEM_CACHED_BASE | ((addr + (y * stride)) & 0x01FFFFFFU);
        for (u32 x = 0; x + 1U < width + 1U; x += 2U) {
            const u32 w = mem_read32(NULL, line + (x * 2U));
            const f64 u = (f64)((w >> 16U) & 0xFFU) - 128.0;
            const f64 v = (f64)(w & 0xFFU) - 128.0;
            const u32 ys[2] = {w >> 24U, (w >> 8U) & 0xFFU};
            for (u32 k = 0; k < 2U && x + k < width; k++) {
                const f64 c = 1.164 * ((f64)ys[k] - 16.0);
                u8 *px = &rgba[(((size_t)y * width) + x + k) * 4U];
                px[0] = ref_channel(c + (1.596 * v));
                px[1] = ref_channel(c - (0.392 * u) - (0.813 * v));
                px[2] = ref_channel(c + (2.017 * u));
                px[3] = 0xFF;
            }
        }
    }
}

#define GUARD 16U /* bytes depois da imagem: a conversão nova não pode escrever além do que a antiga escrevia */

/* Converte com as duas e exige saídas idênticas, inclusive a guarda. */
static void expect_same(GuestAddr addr, u32 width, u32 height, u32 stride) {
    const size_t bytes = ((size_t)width * height * 4U) + GUARD;
    u8 *fast = malloc(bytes);
    u8 *ref = malloc(bytes);
    EXPECT_TRUE(fast != NULL && ref != NULL);
    if (fast == NULL || ref == NULL) {
        free(fast);
        free(ref);
        return;
    }
    memset(fast, 0xA5, bytes);
    memset(ref, 0xA5, bytes);
    gx_efb_xfb_to_rgba(addr, width, height, stride, fast);
    ref_xfb_to_rgba(addr, width, height, stride, ref);
    if (memcmp(fast, ref, bytes) != 0) {
        size_t at = 0;
        while (fast[at] == ref[at]) {
            at++;
        }
        AM_TEST_FAIL("%ux%u passo %u em 0x%08X: primeiro byte diferente em %zu (novo 0x%02X, original 0x%02X)", width,
                     height, stride, addr, at, fast[at], ref[at]);
    }
    EXPECT_TRUE(memcmp(fast, ref, bytes) == 0);
    free(fast);
    free(ref);
}

static u32 s_seed = 0x2468ACE1U;

static u32 next_rand(void) {
    s_seed = (s_seed * 1664525U) + 1013904223U;
    return s_seed;
}

static void fill_random(GuestAddr base, u32 bytes) {
    for (u32 i = 0; i < bytes; i += 4U) {
        mem_write32(NULL, base + i, next_rand());
    }
}

TEST(known_values) {
    mem_write32(NULL, 0x80100000U, (16U << 24U) | (128U << 16U) | (235U << 8U) | 128U); /* preto, branco */
    u8 px[8];
    gx_efb_xfb_to_rgba(0x80100000U, 2, 1, 4, px);
    EXPECT_TRUE(px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 0xFF);
    EXPECT_TRUE(px[4] == 254 && px[5] == 254 && px[6] == 254 && px[7] == 0xFF); /* 1.164 * 219 = 254.9 */
}

/* Para cada U (256) uma imagem 256x256: a linha é o V, e cada palavra tem Y = 2k e 2k+1; juntos cobrem todos os Y. */
TEST(all_yuv_combinations_match_the_original_formula) {
    enum { WIDTH = 256, HEIGHT = 256, STRIDE = WIDTH * 2 };
    static u8 s_fast[WIDTH * HEIGHT * 4];
    static u8 s_ref[WIDTH * HEIGHT * 4];
    u32 bad = 0;
    for (u32 u = 0; u < 256U; u++) {
        for (u32 v = 0; v < 256U; v++) {
            for (u32 k = 0; k < (u32)WIDTH / 2U; k++) {
                mem_write32(NULL, 0x80100000U + (v * (u32)STRIDE) + (k * 4U),
                            ((2U * k) << 24U) | (u << 16U) | (((2U * k) + 1U) << 8U) | v);
            }
        }
        gx_efb_xfb_to_rgba(0x80100000U, WIDTH, HEIGHT, STRIDE, s_fast);
        ref_xfb_to_rgba(0x80100000U, WIDTH, HEIGHT, STRIDE, s_ref);
        if (memcmp(s_fast, s_ref, sizeof(s_fast)) != 0) {
            size_t at = 0;
            while (s_fast[at] == s_ref[at]) {
                at++;
            }
            AM_TEST_FAIL("U=%u: primeiro byte diferente em %zu (novo 0x%02X, original 0x%02X)", u, at, s_fast[at],
                         s_ref[at]);
            bad++;
        }
    }
    EXPECT_EQ(bad, 0U);
}

TEST(random_xfb_with_wide_stride_and_odd_widths) {
    fill_random(0x80200000U, (480U * 1400U) + 8U);
    expect_same(0x80200000U, 640, 480, 1400); /* passo maior que a largura * 2 */
    expect_same(0x80200000U, 640, 480, 1280); /* o passo do jogo */
    expect_same(0x80200000U, 641, 3, 1400);   /* largura ímpar: a última palavra traz um pixel só */
    expect_same(0x80200000U, 7, 5, 24);
    expect_same(0x80200000U, 1, 3, 8);
    expect_same(0x80200000U, 2, 1, 4);
    expect_same(0x80200000U, 0, 3, 8); /* sem pixels: nada escrito */
    expect_same(0x80200000U, 8, 0, 16);
}

/* Uma linha que cruza o fim da MEM1 (24 MiB) para a janela dos RELs não tem ponteiro direto: cada palavra vai pelo
 * mem_read32, e a linha seguinte, toda dentro da janela, vai pelo caminho direto. */
TEST(row_across_mem1_end_falls_back_to_word_reads) {
    rel_map_reset();
    EXPECT_TRUE(rel_map_add(1, 0x81800000U, 0x1000U, 0x80380000U));
    EXPECT_NULL(mem_host_ptr(0x817FFFF0U, 32U));
    EXPECT_NOT_NULL(mem_host_ptr(0x81800010U, 32U));
    fill_random(0x817FFFF0U, 64U);
    expect_same(0x817FFFF0U, 16, 2, 32);
    rel_map_reset();
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(known_values);
    RUN_TEST(all_yuv_combinations_match_the_original_formula);
    RUN_TEST(random_xfb_with_wide_stride_and_odd_widths);
    RUN_TEST(row_across_mem1_end_falls_back_to_word_reads);
    mem_shutdown();
    return am_test_finish();
}
