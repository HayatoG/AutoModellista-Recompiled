/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vertex loader (gfx/gx_vtx.c): formatos diretos e indexados, frações, cores e o NBT com três índices. */
#include <math.h>
#include <string.h>

#include "am/gx_vtx.h"
#include "am/mem.h"

#include "am_test.h"

static bool near_f(f32 a, f32 b) {
    return fabsf(a - b) < 1e-6F;
}

TEST(direct_and_indexed_attributes) {
    gx_vtx_reset();
    /* VCD: PosMtxIdx, posição direta, normal direta, cor 0 índice de 8 bits, cor 1 direta; tex0 direta. */
    gx_vtx_load_cp(GX_CP_VCD_LO, 1U | (1U << 9) | (1U << 11) | (2U << 13) | (1U << 15));
    gx_vtx_load_cp(GX_CP_VCD_HI, 1U);
    /* VAT 2: posição xyz s16 com 4 bits de fração; normal s8; cor 0 RGB565; cor 1 RGBA4444; tex0 st u8 frac 7. */
    gx_vtx_load_cp(GX_CP_VAT_A + 2U,
                   1U | (3U << 1) | (4U << 4) | (1U << 10) | (0U << 14) | (3U << 18) | (1U << 21) | (7U << 25));
    gx_vtx_load_cp(GX_CP_ARRAY_BASE + 2U, 0x00300000U); /* array da cor 0 */
    gx_vtx_load_cp(GX_CP_ARRAY_STRIDE + 2U, 2U);
    mem_write16(NULL, 0x80300000U + (5U * 2U), 0x07E0); /* elemento 5: verde */
    EXPECT_EQ(gx_vtx_size(2), 1U + 6U + 3U + 1U + 2U + 2U);
    const u8 v[] = {0x0C,                               /* PosMtxIdx: linha 12 */
                    0x00, 0x18, 0xFF, 0xF0, 0x01, 0x00, /* 1.5, -1, 16 */
                    0x40, 0xC0, 0x00,                   /* normal 1, -1, 0 */
                    5,                                  /* índice da cor 0 */
                    0x12, 0x3F,                         /* RGBA4444 */
                    0x80, 0x40};                        /* s = 1, t = 0.5 */
    GxVertex out;
    memset(&out, 0, sizeof(out));
    gx_vtx_decode(2, v, &out);
    EXPECT_EQ(out.pos_mtx, 12U);
    EXPECT_TRUE(near_f(out.pos[0], 1.5F) && near_f(out.pos[1], -1.0F) && near_f(out.pos[2], 16.0F));
    EXPECT_TRUE(near_f(out.nrm[0][0], 1.0F) && near_f(out.nrm[0][1], -1.0F) && near_f(out.nrm[0][2], 0.0F));
    EXPECT_TRUE(out.color[0][0] == 0 && out.color[0][1] == 255 && out.color[0][2] == 0 && out.color[0][3] == 255);
    EXPECT_TRUE(out.color[1][0] == 0x11 && out.color[1][1] == 0x22 && out.color[1][2] == 0x33 &&
                out.color[1][3] == 0xFF);
    EXPECT_TRUE(near_f(out.tex[0][0], 1.0F) && near_f(out.tex[0][1], 0.5F));
    EXPECT_TRUE(!out.skip);
}

TEST(missing_color_is_white_and_null_index_skips) {
    gx_vtx_reset();
    gx_vtx_load_cp(GX_CP_VCD_LO, 2U << 9); /* posição por índice de 8 bits, sem cor */
    gx_vtx_load_cp(GX_CP_VCD_HI, 0);
    gx_vtx_load_cp(GX_CP_VAT_A, 0U | (4U << 1)); /* xy f32 */
    gx_vtx_load_cp(GX_CP_ARRAY_BASE, 0x00310000U);
    gx_vtx_load_cp(GX_CP_ARRAY_STRIDE, 8U);
    mem_write32(NULL, 0x80310008U, 0x40000000U); /* elemento 1: (2, -2) */
    mem_write32(NULL, 0x8031000CU, 0xC0000000U);
    GxVertex out;
    memset(&out, 0, sizeof(out));
    const u8 one = 1;
    gx_vtx_decode(0, &one, &out);
    EXPECT_TRUE(near_f(out.pos[0], 2.0F) && near_f(out.pos[1], -2.0F) && near_f(out.pos[2], 0.0F));
    EXPECT_TRUE(out.color[0][0] == 255 && out.color[0][3] == 255 && out.color[1][2] == 255);
    const u8 null_index = 0xFF;
    gx_vtx_decode(0, &null_index, &out);
    EXPECT_TRUE(out.skip);
}

TEST(nbt_with_three_indices) {
    gx_vtx_reset();
    gx_vtx_load_cp(GX_CP_VCD_LO, (1U << 9) | (2U << 11)); /* posição direta, normal índice de 8 bits */
    gx_vtx_load_cp(GX_CP_VAT_A + 1U, 1U | (4U << 1) | (1U << 9) | (1U << 10) | (1U << 31)); /* NBT s8, índice 3 */
    gx_vtx_load_cp(GX_CP_ARRAY_BASE + 1U, 0x00320000U);
    gx_vtx_load_cp(GX_CP_ARRAY_STRIDE + 1U, 9U);
    EXPECT_EQ(gx_vtx_size(1), 12U + 3U);
    for (u32 e = 0; e < 3U; e++) { /* elemento e: 9 componentes s8 com valor 64 * (e + 1) / 64 nos três vetores */
        for (u32 k = 0; k < 9U; k++) {
            mem_write8(NULL, 0x80320000U + (e * 9U) + k, (u8)((k / 3U) == e ? 64U : 0U));
        }
    }
    u8 v[15] = {0};
    v[12] = 0; /* normal do elemento 0, tangente do 1, binormal do 2 (cada um no seu terço) */
    v[13] = 1;
    v[14] = 2;
    GxVertex out;
    memset(&out, 0, sizeof(out));
    gx_vtx_decode(1, v, &out);
    EXPECT_TRUE(near_f(out.nrm[0][0], 1.0F) && near_f(out.nrm[1][0], 1.0F) && near_f(out.nrm[2][0], 1.0F));
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(direct_and_indexed_attributes);
    RUN_TEST(missing_color_is_white_and_null_index_skips);
    RUN_TEST(nbt_with_three_indices);
    mem_shutdown();
    return am_test_finish();
}
