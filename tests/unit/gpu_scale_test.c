/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9: contas puras da resolução interna (am/gpu_scale.h) e o backend de GPU do GX com um EFB grande de mentira. */
#include <stdlib.h>
#include <string.h>

#include "am/gpu_scale.h"
#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/gx_fifo.h"
#include "am/gx_gpu.h"
#include "am/mem.h"

#include "am_test.h"
#include "gx_test_util.h"

TEST(clamp_and_rect) {
    EXPECT_EQ(gpu_scale_clamp(-3), 1U);
    EXPECT_EQ(gpu_scale_clamp(0), 1U);
    EXPECT_EQ(gpu_scale_clamp(3), 3U);
    EXPECT_EQ(gpu_scale_clamp(9), 4U);
    const s32 in[4] = {10, 20, 30, 40};
    s32 out[4];
    gpu_scale_rect(in, 3U, out);
    EXPECT_EQ(out[0], 30);
    EXPECT_EQ(out[1], 60);
    EXPECT_EQ(out[2], 90);
    EXPECT_EQ(out[3], 120);
}

TEST(lod_bias) {
    const u32 m0 = 0x000001A5U; /* bias 0 */
    EXPECT_EQ(gpu_scale_lod_mode0(m0, 1U), m0);
    /* N = 2: +32/32 = 1 nível; N = 4: +2 níveis; N = 3: 51/32 = 1,59 (log2 3 = 1,585) */
    EXPECT_EQ((gpu_scale_lod_mode0(m0, 2U) >> 9) & 0xFFU, 32U);
    EXPECT_EQ((gpu_scale_lod_mode0(m0, 3U) >> 9) & 0xFFU, 51U);
    EXPECT_EQ((gpu_scale_lod_mode0(m0, 4U) >> 9) & 0xFFU, 64U);
    EXPECT_EQ(gpu_scale_lod_mode0(m0, 2U) & ~(0xFFU << 9), m0 & ~(0xFFU << 9)); /* o resto do registrador fica */
    /* bias negativo (-16 = 0xF0) + 32 = +16; e saturação em +127 */
    EXPECT_EQ((gpu_scale_lod_mode0(0xF0U << 9, 2U) >> 9) & 0xFFU, 16U);
    EXPECT_EQ((gpu_scale_lod_mode0(120U << 9, 4U) >> 9) & 0xFFU, 127U);
}

TEST(replicate) {
    const u32 src[6] = {1, 2, 3, 4, 5, 6}; /* 3 x 2 */
    u32 dst[6 * 9];
    gpu_scale_replicate(src, dst, 3U, 2U, 3U);
    for (u32 y = 0; y < 6U; y++) {
        for (u32 x = 0; x < 9U; x++) {
            EXPECT_EQ(dst[(y * 9U) + x], src[((y / 3U) * 3U) + (x / 3U)]);
        }
    }
}

TEST(reduce_rgb8_and_roundtrip) {
    /* replicar e reduzir devolve o original, em qualquer N e nos dois formatos */
    u32 src[4 * 3];
    for (u32 i = 0; i < 12U; i++) {
        src[i] = (((i * 37U) & 0xFFU) << 16) | (((i * 91U) & 0xFFU) << 8) | ((i * 5U) & 0xFFU);
    }
    for (u32 n = 1U; n <= GPU_SCALE_MAX; n++) {
        u32 *hi = malloc((size_t)12U * n * n * sizeof(u32));
        u32 back[12] = {0};
        gpu_scale_replicate(src, hi, 4U, 3U, n);
        gpu_scale_reduce_color(hi, back, 4U, 0U, 3U, n, false);
        EXPECT_TRUE(memcmp(src, back, sizeof(src)) == 0);
        free(hi);
    }
}

TEST(reduce_average) {
    /* 1 pixel nativo, N = 2: quatro amostras R = 0, 0, 255, 255 -> 128 (arredonda para cima); G = 10 -> 10 */
    const u32 hi[4] = {0x000A00U, 0x000A00U, 0xFF0A00U, 0xFF0A00U};
    u32 out = 0;
    gpu_scale_reduce_color(hi, &out, 1U, 0U, 1U, 2U, false);
    EXPECT_EQ(out, 0x800A00U);
    /* RGBA6: A (bits 5..0) = 0, 0, 0, 63 -> 16 (15,75); B (bits 11..6) = 63 nas quatro; R e G 0 */
    const u32 hi6[4] = {63U << 6, 63U << 6, 63U << 6, (63U << 6) | 63U};
    gpu_scale_reduce_color(hi6, &out, 1U, 0U, 1U, 2U, true);
    EXPECT_EQ(out, (63U << 6) | 16U);
    /* só as linhas pedidas mudam */
    u32 two[2] = {7U, 7U};
    const u32 hi2[8] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}; /* 1 x 2 pixels nativos, N = 2 */
    gpu_scale_reduce_color(hi2, two, 1U, 1U, 2U, 2U, false);
    EXPECT_EQ(two[0], 7U);
    EXPECT_EQ(two[1], 0xFFU);
}

TEST(reduce_depth_center) {
    /* N = 2: a amostra (1, 1) de cada bloco; N = 3: a (1, 1) também */
    const u32 hi[8] = {1, 2, 3, 4, 5, 6, 7, 8}; /* 2 x 1 nativo em N = 2: linhas de 4 */
    u32 out[2] = {0, 0};
    gpu_scale_reduce_depth(hi, out, 2U, 0U, 1U, 2U);
    EXPECT_EQ(out[0], 6U);
    EXPECT_EQ(out[1], 8U);
}

/* --- o backend de GPU com N = 2: o lote leva e traz o EFB grande, o scissor segue nativo --- */
static struct {
    u32 color_in_w; /* tamanho que a "GPU" esperaria: 640 * 2 * 528 * 2 */
    bool saw_in;
    bool saw_out;
    u32 top, bottom;
    s32 scissor[4];
} s_fake;

static bool fake_run(const GpuBatch *b) {
    if (b->efb_color_in != NULL) {
        s_fake.saw_in = true;
        /* o último pixel do EFB grande tem de existir (a CPU alocou o tamanho todo) e vir replicado */
        s_fake.color_in_w = b->efb_color_in[(GPU_EFB_WIDTH * 2U * GPU_EFB_HEIGHT * 2U) - 1U];
    }
    if (b->efb_color_out != NULL) {
        s_fake.saw_out = true;
        s_fake.top = b->readback_top;
        s_fake.bottom = b->readback_bottom;
        /* "desenha": pinta o EFB grande de 0x112233 nas linhas pedidas */
        for (u32 y = b->readback_top * 2U; y < b->readback_bottom * 2U; y++) {
            for (u32 x = 0; x < GPU_EFB_WIDTH * 2U; x++) {
                b->efb_color_out[((size_t)y * GPU_EFB_WIDTH * 2U) + x] = 0x112233U;
            }
        }
    }
    if (b->draw_count != 0U) {
        memcpy(s_fake.scissor, b->draws[0].scissor, sizeof(s_fake.scissor));
    }
    return true;
}

TEST(gx_gpu_scaled_batch) {
    gx_efb_reset();
    gx_gpu_reset();
    gx_gpu_set_scale(2U);
    EXPECT_EQ(gx_gpu_scale(), 2U);
    gx_efb_color_buffer()[(GPU_EFB_WIDTH * GPU_EFB_HEIGHT) - 1U] = 0xABCDEFU;
    gx_gpu_set_backend(fake_run);
    gx_gpu_sync();
    EXPECT_TRUE(s_fake.saw_in);
    EXPECT_EQ(s_fake.color_in_w, 0xABCDEFU); /* o pixel nativo foi replicado no canto do EFB grande */
    EXPECT_TRUE(s_fake.saw_out);
    EXPECT_EQ(s_fake.bottom, GPU_EFB_HEIGHT); /* as linhas continuam nativas */
    /* a média de caixa devolve o pixel nativo (RGB8: o formato 0 do PE) */
    EXPECT_EQ(gx_efb_color_buffer()[0], 0x112233U);
    EXPECT_EQ(gx_efb_color_buffer()[(GPU_EFB_WIDTH * GPU_EFB_HEIGHT) - 1U], 0x112233U);
    gx_gpu_set_backend(NULL);
    gx_gpu_reset();
    gx_gpu_set_scale(1U);
    EXPECT_EQ(gx_gpu_scale(), 1U);
}

/* --- etapa 1b: a cópia para o XFB também sai em N x N, em RGBA, guardada pelo endereço do XFB --- */
static bool paint_run(const GpuBatch *b) {
    if (b->efb_color_out != NULL) {
        /* EFB grande de 2x: as 4 primeiras colunas grandes (2 pixels nativos) brancas, o resto preto */
        for (u32 y = b->readback_top * 2U; y < b->readback_bottom * 2U; y++) {
            for (u32 x = 0; x < GPU_EFB_WIDTH * 2U; x++) {
                b->efb_color_out[((size_t)y * GPU_EFB_WIDTH * 2U) + x] = x < 4U ? 0xFFFFFFU : 0U;
            }
        }
    }
    return true;
}

TEST(hires_xfb_by_address) {
    gx_fifo_reset(NULL);
    gx_fifo_set_render(true);
    gx_gpu_reset();
    gx_gpu_set_scale(2U);
    gx_gpu_set_backend(paint_run);
    gxt_setup_2d();
    u32 w = 0;
    u32 h = 0;
    EXPECT_TRUE(gx_gpu_hires_xfb(0x80350000U, 4U, 2U, &w, &h) == NULL); /* ainda não houve cópia */
    gxt_bp(GX_BP_EFB_TL, 0);
    gxt_bp(GX_BP_EFB_WH, 3U | (1U << 10)); /* 4 x 2 */
    gxt_bp(GX_BP_EFB_ADDR, 0x00350000U >> 5);
    gxt_bp(GX_BP_EFB_STRIDE, 1);
    gxt_bp(GX_BP_COPYYSCALE, 256);
    gxt_bp(GX_BP_COPYFILTER0, (21U << 12) | (22U << 18)); /* filtro identidade: peso 64 na linha atual */
    gxt_bp(GX_BP_COPYFILTER1, 21U);
    gxt_bp(GX_BP_COPY_EXECUTE, 1U << 14);
    const u8 *img = gx_gpu_hires_xfb(0x80350000U, 4U, 2U, &w, &h);
    EXPECT_TRUE(img != NULL);
    EXPECT_EQ(w, 8U);
    EXPECT_EQ(h, 4U);
    if (img != NULL) {
        EXPECT_EQ(img[0], 255U); /* pixel grande (0, 0): branco, alfa cheio */
        EXPECT_EQ(img[3], 255U);
        EXPECT_EQ(img[(3U * 4U) + 0U], 255U); /* (3, 0) ainda branco */
        EXPECT_EQ(img[(4U * 4U) + 0U], 0U);   /* (4, 0) preto */
        EXPECT_EQ(img[((3U * 8U) + 3U) * 4U], 255U); /* última linha grande, mesmo recorte */
    }
    EXPECT_TRUE(gx_gpu_hires_xfb(0x80360000U, 4U, 2U, &w, &h) == NULL); /* outro endereço */
    EXPECT_TRUE(gx_gpu_hires_xfb(0x80350000U, 4U, 3U, &w, &h) == NULL); /* outro tamanho */
    EXPECT_EQ(mem_read32(NULL, 0x80350000U), 0xEB80EB80U); /* o XFB do guest segue nativo (YUV) */
    gx_gpu_set_backend(NULL);
    gx_gpu_reset();
    gx_gpu_set_scale(1U);
    EXPECT_TRUE(gx_gpu_hires_xfb(0x80350000U, 4U, 2U, &w, &h) == NULL);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(clamp_and_rect);
    RUN_TEST(lod_bias);
    RUN_TEST(replicate);
    RUN_TEST(reduce_rgb8_and_roundtrip);
    RUN_TEST(reduce_average);
    RUN_TEST(reduce_depth_center);
    RUN_TEST(gx_gpu_scaled_batch);
    RUN_TEST(hires_xfb_by_address);
    return am_test_finish();
}
