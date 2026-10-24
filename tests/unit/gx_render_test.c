/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pipeline inteiro pelo fluxo de comandos: vertex loader -> XF -> montagem/recorte -> rasterizador -> TEV -> EFB e a
 * cópia EFB -> XFB na memória. Cenas sintéticas de poucos pixels. */
#include <string.h>

#include "am/gx_efb.h"
#include "am/gx_fifo.h"
#include "am/gx_raster.h"
#include "am/gx_setup.h"
#include "am/mem.h"

#include "am_test.h"
#include "gx_test_util.h"

static u32 peek_rgb(u32 x, u32 y) {
    u8 c[4];
    u32 z;
    gx_efb_peek(x, y, c, &z);
    return ((u32)c[0] << 16) | ((u32)c[1] << 8) | c[2];
}

static void start(void) {
    gx_fifo_reset(NULL);
    gx_fifo_set_render(true);
    gxt_setup_2d();
}

TEST(quad_covers_pixel_centers_only) {
    start();
    gxt_quad(10.0F, 20.0F, 14.0F, 23.0F, 0xFF8000FFU);
    EXPECT_EQ(peek_rgb(10, 20), 0xFF8000U);
    EXPECT_EQ(peek_rgb(13, 22), 0xFF8000U);
    EXPECT_EQ(peek_rgb(14, 22), 0U); /* borda direita e de baixo ficam de fora */
    EXPECT_EQ(peek_rgb(13, 23), 0U);
    EXPECT_EQ(peek_rgb(9, 20), 0U);
    EXPECT_EQ(gx_raster_stats()->pixels, 4U * 3U);
}

TEST(gouraud_and_triangle_strip) {
    start();
    /* Faixa de 4 vértices = 2 triângulos cobrindo 0..8 x 0..2, vermelho à esquerda, azul à direita. */
    u8 cmd[3 + (4 * 16)] = {GX_CMD_DRAW_FIRST | (3U << 3), 0, 4};
    u32 n = 3;
    const f32 xy[4][2] = {{0, 0}, {0, 2}, {8, 0}, {8, 2}};
    const u32 col[4] = {0xFF0000FFU, 0xFF0000FFU, 0x0000FFFFU, 0x0000FFFFU};
    for (u32 v = 0; v < 4U; v++) {
        n += gxt_put_f32(&cmd[n], xy[v][0]);
        n += gxt_put_f32(&cmd[n], xy[v][1]);
        n += gxt_put_f32(&cmd[n], 0.0F);
        cmd[n++] = (u8)(col[v] >> 24);
        cmd[n++] = (u8)(col[v] >> 16);
        cmd[n++] = (u8)(col[v] >> 8);
        cmd[n++] = (u8)col[v];
    }
    gxt_feed(cmd, n);
    const u32 left = peek_rgb(0, 1);
    const u32 right = peek_rgb(7, 1);
    EXPECT_TRUE((left >> 16) > 200U && (left & 0xFFU) < 40U);
    EXPECT_TRUE((right >> 16) < 40U && (right & 0xFFU) > 200U);
    EXPECT_EQ(gx_raster_stats()->triangles, 2U);
}

TEST(backface_cull_and_clip) {
    start();
    gxt_bp(GX_BP_GENMODE, (1U << 4) | (3U << 14)); /* descarta frente e trás */
    gxt_quad(0.0F, 0.0F, 4.0F, 4.0F, 0xFFFFFFFFU);
    EXPECT_EQ(peek_rgb(1, 1), 0U);
    EXPECT_EQ(gx_setup_stats()->culled, 2U);
    gxt_bp(GX_BP_GENMODE, 1U << 4);
    gxt_quad(-100.0F, -100.0F, 2.0F, 2.0F, 0x00FF00FFU); /* recortado contra x = 0 e y = 0 da projeção */
    EXPECT_EQ(peek_rgb(0, 0), 0x00FF00U);
    EXPECT_EQ(peek_rgb(1, 1), 0x00FF00U);
    EXPECT_EQ(peek_rgb(2, 2), 0U);
}

TEST(textured_modulate_and_alpha_test) {
    start();
    /* Textura I8 8x4 em 0x80340000: intensidade 0x80 nas colunas pares, 0x00 nas ímpares. */
    for (u32 i = 0; i < 32U; i++) {
        mem_write8(NULL, 0x80340000U + i, (u8)((i & 1U) == 0U ? 0x80U : 0x00U));
    }
    gxt_xf1(GX_XF_NUMTEXGENS, 1);
    gxt_xf1(GX_XF_TEXMTXINFO, 5U << 7);
    gxt_identity(60);
    gxt_bp(GX_BP_GENMODE, 1U | (1U << 4));
    gxt_bp(GX_BP_SU_SSIZE, 7);      /* texcoords normalizadas x 8 */
    gxt_bp(GX_BP_SU_SSIZE + 1U, 3); /* x 4 */
    gxt_bp(GX_BP_TX_SETMODE0, 0);
    gxt_bp(GX_BP_TX_SETIMAGE0, 7U | (3U << 10) | (1U << 20)); /* 8x4, I8 */
    gxt_bp(GX_BP_TX_SETIMAGE3, 0x00340000U >> 5);
    gxt_bp(GX_BP_TREF, 1U << 6); /* textura 0, coordenada 0, canal 0 */
    gxt_bp(GX_BP_TEV_COLOR_ENV, (15U << 12) | (8U << 8) | (10U << 4) | 15U | (1U << 19)); /* tex * ras */
    gxt_bp(GX_BP_TEV_ALPHA_ENV, (7U << 13) | (4U << 10) | (5U << 7) | (7U << 4) | (1U << 19));
    gxt_bp(GX_BP_ALPHACOMPARE, 0x40U | (4U << 16) | (7U << 19)); /* alfa > 0x40 (E sempre) */
    gxt_cp(GX_CP_VCD_HI, 1U);                                    /* tex0 direta */
    gxt_cp(GX_CP_VAT_A, 1U | (4U << 1) | (5U << 14) | (1U << 21) | (4U << 22));
    u8 cmd[3 + (4 * 24)] = {GX_CMD_DRAW_FIRST, 0, 4};
    u32 n = 3;
    const f32 v[4][4] = {{0, 0, 0, 0}, {8, 0, 1, 0}, {8, 4, 1, 1}, {0, 4, 0, 1}};
    for (u32 i = 0; i < 4U; i++) {
        n += gxt_put_f32(&cmd[n], v[i][0]);
        n += gxt_put_f32(&cmd[n], v[i][1]);
        n += gxt_put_f32(&cmd[n], 0.0F);
        cmd[n++] = 0xFF; /* cor branca */
        cmd[n++] = 0x80;
        cmd[n++] = 0xFF;
        cmd[n++] = 0xFF;
        n += gxt_put_f32(&cmd[n], v[i][2]);
        n += gxt_put_f32(&cmd[n], v[i][3]);
    }
    gxt_feed(cmd, n);
    /* Coluna par: tex * ras com c' = c + (c >> 7) e arredondamento +128: R = (0x80 * 256 + 128) >> 8 = 0x80,
     * G = (0x80 * 129 + 128) >> 8 = 0x41. */
    EXPECT_EQ(peek_rgb(0, 0), 0x804180U);
    EXPECT_EQ(peek_rgb(1, 0), 0U); /* coluna ímpar: alfa 0 reprovado no teste de alfa */
}

TEST(blend_src_alpha) {
    start();
    gxt_quad(0.0F, 0.0F, 2.0F, 2.0F, 0x0000FFFFU);
    gxt_bp(GX_BP_BLENDMODE, 0x18U | 1U | (4U << 8) | (5U << 5)); /* fonte * alfa + destino * (1 - alfa) */
    gxt_quad(0.0F, 0.0F, 2.0F, 2.0F, 0xFF000080U);
    /* fator 0x80 + 1 = 0x81 e 0x7F + 0 = 0x7F: R = 255 * 129 >> 8 = 128, B = 255 * 127 >> 8 = 126 */
    EXPECT_EQ(peek_rgb(0, 0), (128U << 16) | 126U);
}

TEST(copy_to_xfb_and_clear) {
    start();
    gxt_quad(0.0F, 0.0F, 2.0F, 1.0F, 0xFFFFFFFFU); /* dois pixels brancos, o resto preto */
    gxt_bp(GX_BP_EFB_TL, 0);
    gxt_bp(GX_BP_EFB_WH, 3U | (1U << 10)); /* 4 x 2 */
    gxt_bp(GX_BP_EFB_ADDR, 0x00350000U >> 5);
    gxt_bp(GX_BP_EFB_STRIDE, 1); /* 32 bytes por linha */
    gxt_bp(GX_BP_COPYYSCALE, 256);
    gxt_bp(GX_BP_COPYFILTER0, (21U << 12) | (22U << 18)); /* filtro vertical padrão: só a linha atual (64) */
    gxt_bp(GX_BP_COPYFILTER1, 21U);
    gxt_bp(GX_BP_CLEAR_AR, 0xFF10); /* limpa para RGB (0x10, 0x20, 0x30) */
    gxt_bp(GX_BP_CLEAR_GB, 0x2030);
    gxt_bp(GX_BP_COPY_EXECUTE, (1U << 14) | (1U << 11));   /* para o XFB, com limpeza */
    EXPECT_EQ(mem_read32(NULL, 0x80350000U), 0xEB80EB80U); /* branco: Y 235, U = V = 128 */
    EXPECT_EQ(mem_read32(NULL, 0x80350004U), 0x10801080U); /* preto: Y 16 */
    EXPECT_EQ(mem_read32(NULL, 0x80350020U), 0x10801080U); /* segunda linha, no passo de 32 bytes */
    EXPECT_EQ(peek_rgb(0, 0), 0x102030U);
    EXPECT_EQ(peek_rgb(3, 1), 0x102030U);
    EXPECT_EQ(peek_rgb(4, 0), 0U); /* fora do retângulo da cópia */
    EXPECT_EQ(gx_efb_stats()->xfb_copies, 1U);
    u8 rgba[4 * 4];
    gx_efb_xfb_to_rgba(0x00350000U, 4, 1, 32, rgba);
    EXPECT_TRUE(rgba[0] == 254 && rgba[1] == 254 && rgba[2] == 254 && rgba[3] == 255); /* 1,164 * 219 = 254,9 */
    EXPECT_TRUE(rgba[8] == 0 && rgba[9] == 0 && rgba[10] == 0);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(quad_covers_pixel_centers_only);
    RUN_TEST(gouraud_and_triangle_strip);
    RUN_TEST(backface_cull_and_clip);
    RUN_TEST(textured_modulate_and_alpha_test);
    RUN_TEST(blend_src_alpha);
    RUN_TEST(copy_to_xfb_and_clear);
    mem_shutdown();
    return am_test_finish();
}
