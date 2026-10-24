/* SPDX-License-Identifier: GPL-3.0-or-later */
/* EFB e registradores de BP (gfx/gx_efb.c, gfx/gx_bp.c): teste de Z, formato RGBA6, operação lógica, cópia para
 * textura (layout de blocos), máscara de BP, cores do TEV e carga de TLUT. */
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/mem.h"

#include "am_test.h"

static void begin(u32 zmode, u32 blend, u32 format) {
    gx_bp_reset();
    gx_efb_reset();
    (void)gx_bp_write(GX_BP_ZMODE, zmode);
    (void)gx_bp_write(GX_BP_BLENDMODE, blend);
    (void)gx_bp_write(GX_BP_ZCOMPARE, format);
    gx_efb_begin_draw();
}

TEST(z_test_and_update) {
    begin(1U | (1U << 1) | (1U << 4), 0x18, GX_PIXEL_RGB8_Z24); /* LESS com escrita */
    EXPECT_TRUE(!gx_efb_z_test(5, 5, 0));                       /* Z inicial 0: 0 < 0 é falso */
    u8 c[4];
    u32 z;
    gx_efb_peek(5, 5, c, &z);
    EXPECT_EQ(z, 0U);
    begin(1U | (6U << 1) | (1U << 4), 0x18, GX_PIXEL_RGB8_Z24); /* GEQUAL com escrita */
    EXPECT_TRUE(gx_efb_z_test(5, 5, 0x123456));
    gx_efb_peek(5, 5, c, &z);
    EXPECT_EQ(z, 0x123456U);
    EXPECT_TRUE(!gx_efb_z_test(5, 5, 0x100000));
}

TEST(rgba6_quantizes_and_dithers_off) {
    begin(0, 0x18, GX_PIXEL_RGBA6_Z24);
    const u8 px[4] = {0xFF, 0x81, 0x03, 0x7F};
    gx_efb_blend(1, 1, px);
    u8 c[4];
    u32 z;
    gx_efb_peek(1, 1, c, &z);
    EXPECT_TRUE(c[0] == 0xFF && c[1] == 0x82 && c[2] == 0x00 && c[3] == 0x7D); /* 6 bits replicados */
}

TEST(logic_op_xor) {
    begin(0, 0x18U | 2U | (6U << 12), GX_PIXEL_RGB8_Z24); /* lógica XOR */
    const u8 a[4] = {0xF0, 0x0F, 0xFF, 0xFF};
    gx_efb_blend(0, 0, a);
    gx_efb_blend(0, 0, a);
    u8 c[4];
    u32 z;
    gx_efb_peek(0, 0, c, &z);
    EXPECT_TRUE(c[0] == 0 && c[1] == 0 && c[2] == 0);
}

TEST(copy_to_texture_rgb565_blocks) {
    begin(0, 0x18, GX_PIXEL_RGB8_Z24);
    const u8 red[4] = {0xFF, 0, 0, 0xFF};
    const u8 green[4] = {0, 0xFF, 0, 0xFF};
    gx_efb_blend(5, 0, red);   /* bloco 1 (x 4..7), texel (1, 0) */
    gx_efb_blend(0, 4, green); /* segunda linha de blocos */
    (void)gx_bp_write(GX_BP_EFB_TL, 0);
    (void)gx_bp_write(GX_BP_EFB_WH, 7U | (7U << 10)); /* 8 x 8 */
    (void)gx_bp_write(GX_BP_EFB_ADDR, 0x00360000U >> 5);
    (void)gx_bp_write(GX_BP_EFB_STRIDE, 2); /* 64 bytes por linha de blocos (2 blocos de 4x4 em 16 bits) */
    (void)gx_bp_write(GX_BP_COPYFILTER0, (21U << 12) | (22U << 18));
    (void)gx_bp_write(GX_BP_COPYFILTER1, 21U);
    (void)gx_bp_write(GX_BP_COPY_EXECUTE, 8U << 3); /* target 8 -> formato real 4 (RGB565) */
    gx_efb_copy();
    EXPECT_EQ(mem_read16(NULL, 0x80360000U + 32U + 2U), 0xF800U);
    EXPECT_EQ(mem_read16(NULL, 0x80360000U + 64U), 0x07E0U);
    EXPECT_EQ(mem_read16(NULL, 0x80360000U), 0U);
    EXPECT_EQ(gx_efb_stats()->texture_copies, 1U);
}

TEST(bp_mask_tev_colors_and_tlut) {
    gx_bp_reset();
    (void)gx_bp_write(0x01, 0xABCDEF);
    (void)gx_bp_write(GX_BP_MASK, 0x00FF00);
    (void)gx_bp_write(0x01, 0x123456);
    EXPECT_EQ(gx_bp_reg(0x01), 0xAB34EFU); /* só o byte do meio mudou */
    (void)gx_bp_write(0x01, 0x000000);
    EXPECT_EQ(gx_bp_reg(0x01), 0U);                                      /* a máscara vale para uma escrita */
    (void)gx_bp_write(GX_BP_TEV_COLOR_RA + 2U, 0x7FFU | (0x400U << 12)); /* C0: R = -1, A = -1024 */
    (void)gx_bp_write(GX_BP_TEV_COLOR_BG + 2U, 0x800000U | 0x0FFU | (0x080U << 12)); /* K1: B = 255, G = 128 */
    s16 c[4];
    gx_bp_tev_color(1, c);
    EXPECT_TRUE(c[0] == -1 && c[3] == -1024);
    gx_bp_tev_konst(1, c);
    EXPECT_TRUE(c[2] == 255 && c[1] == 128);
    for (u32 i = 0; i < 64U; i++) {
        mem_write8(NULL, 0x80370000U + i, (u8)(i + 1U));
    }
    (void)gx_bp_write(GX_BP_LOADTLUT0, 0x00370000U >> 5);
    (void)gx_bp_write(GX_BP_LOADTLUT1, 0x200U | (2U << 10)); /* TMEM 0x40000, duas linhas de 32 bytes */
    const u8 *t = gx_bp_tmem(0x200U << 9, 64);
    EXPECT_TRUE(t != NULL && t[0] == 1 && t[63] == 64);
    EXPECT_EQ(gx_bp_write(GX_BP_SETDRAWDONE, 0x02), GX_BP_EVENT_DRAW_DONE);
    EXPECT_EQ(gx_bp_write(GX_BP_PE_TOKEN_INT, 0x1234), GX_BP_EVENT_TOKEN_INT);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(z_test_and_update);
    RUN_TEST(rgba6_quantizes_and_dithers_off);
    RUN_TEST(logic_op_xor);
    RUN_TEST(copy_to_texture_rgb565_blocks);
    RUN_TEST(bp_mask_tev_colors_and_tlut);
    mem_shutdown();
    return am_test_finish();
}
