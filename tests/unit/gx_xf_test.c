/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Transform unit (gfx/gx_xf.c): projeção, viewport, iluminação de um canal, texgen e cargas indexadas pela FIFO. */
#include <math.h>
#include <string.h>

#include "am/gx_fifo.h"
#include "am/gx_xf.h"
#include "am/mem.h"

#include "am_test.h"
#include "gx_test_util.h"

static bool near_f(f32 a, f32 b) {
    return fabsf(a - b) < 1e-4F;
}

static void write_floats(u32 addr, const f32 *f, u32 n) {
    for (u32 i = 0; i < n; i++) {
        gx_xf_write(addr + i, gxt_f2u(f[i]));
    }
}

TEST(perspective_projection_and_viewport) {
    gx_xf_reset();
    const f32 mtx[12] = {1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0}; /* translação x + 10 na linha 3 */
    write_floats(3U * 4U, mtx, 12);
    const f32 proj[6] = {2.0F, 0.0F, 3.0F, 0.0F, -1.0F, -2.0F};
    write_floats(GX_XF_PROJECTION, proj, 6);
    gx_xf_write(GX_XF_PROJECTION + 6U, 0); /* perspectiva */
    const f32 vp[6] = {100.0F, -50.0F, 1000.0F, 400.0F, 300.0F, 2000.0F};
    write_floats(GX_XF_VIEWPORT, vp, 6);
    GxVertex in;
    memset(&in, 0, sizeof(in));
    in.pos_mtx = 3;
    in.pos[0] = -8.0F;
    in.pos[1] = 1.0F;
    in.pos[2] = -4.0F;
    GxXfVertex out;
    gx_xf_transform(&in, &out);
    EXPECT_TRUE(near_f(out.mv[0], 2.0F));
    EXPECT_TRUE(near_f(out.proj[0], 4.0F) && near_f(out.proj[1], 3.0F) && near_f(out.proj[3], 4.0F));
    gx_xf_perspective_divide(&out);
    EXPECT_TRUE(near_f(out.screen[0], 400.0F + 100.0F) && near_f(out.screen[1], 300.0F - 37.5F));
}

TEST(directional_light_clamped_diffuse) {
    gx_xf_reset();
    const f32 id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    write_floats(0, id, 12);
    const f32 nid[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    write_floats(0x400, nid, 9);
    gx_xf_write(GX_XF_PROJECTION + 6U, 1);
    gx_xf_write(GX_XF_MATCOLOR0, 0xFF804020U); /* material do registrador */
    gx_xf_write(GX_XF_AMBCOLOR0, 0x20202000U); /* ambiente */
    /* Luz 0: cor branca, longe no +z (difusa = cos 60 graus = 0,5 com a normal (0, sqrt(3)/2, 1/2)). */
    gx_xf_write(0x600 + 3U, 0xFFFFFFFFU);
    const f32 pos[3] = {0.0F, 0.0F, 1e6F};
    write_floats(0x600 + 10U, pos, 3);
    /* Canal 0: material e ambiente do registrador, luz ligada, máscara da luz 0, difusa clamp, sem atenuação. */
    gx_xf_write(GX_XF_COLOR0, (1U << 1) | (1U << 2) | (2U << 7) | (2U << 9));
    gx_xf_write(GX_XF_ALPHA0, 0); /* alfa do registrador, sem luz */
    GxVertex in;
    memset(&in, 0, sizeof(in));
    in.nrm[0][1] = 0.8660254F;
    in.nrm[0][2] = 0.5F;
    GxXfVertex out;
    gx_xf_transform(&in, &out);
    /* luz = ambiente 32 + 255 * 0,5 = 159 (truncado); cor = mat * (l + (l >> 7)) >> 8 */
    EXPECT_EQ(out.color[0][0], (0xFFU * (159U + 1U)) >> 8);
    EXPECT_EQ(out.color[0][1], (0x80U * (159U + 1U)) >> 8);
    EXPECT_EQ(out.color[0][2], (0x40U * (159U + 1U)) >> 8);
    EXPECT_EQ(out.color[0][3], 0x20U);
}

TEST(regular_texgen_from_tex0) {
    gx_xf_reset();
    const f32 m[8] = {2, 0, 0, 0.5F, 0, -1, 0, 1}; /* s' = 2 s + 0,5; t' = 1 - t */
    write_floats(60U * 4U, m, 8);
    gx_xf_write(GX_XF_NUMTEXGENS, 1);
    gx_xf_write(GX_XF_TEXMTXINFO, 5U << 7); /* ST, AB11, regular, origem Tex0 */
    GxVertex in;
    memset(&in, 0, sizeof(in));
    in.tex_mtx[0] = 60;
    in.tex[0][0] = 0.25F;
    in.tex[0][1] = 0.75F;
    GxXfVertex out;
    gx_xf_transform(&in, &out);
    EXPECT_TRUE(near_f(out.tex[0][0], 1.0F) && near_f(out.tex[0][1], 0.25F) && near_f(out.tex[0][2], 1.0F));
}

TEST(indexed_load_and_default_matrices) {
    gx_fifo_reset(NULL);
    gxt_cp(GX_CP_ARRAY_BASE + 12U, 0x00330000U); /* LOAD_INDX_A usa o array 12 */
    gxt_cp(GX_CP_ARRAY_STRIDE + 12U, 0x30U);
    mem_write32(NULL, 0x80330000U + (2U * 0x30U), 0x3F800000U); /* elemento 2: 1.0, 2.0 */
    mem_write32(NULL, 0x80330000U + (2U * 0x30U) + 4U, 0x40000000U);
    const u8 cmd[5] = {GX_CMD_LOAD_INDX_A, 0x00, 0x02, 0x10, 0x24}; /* índice 2, 2 palavras, XF 0x024 */
    gxt_feed(cmd, sizeof(cmd));
    EXPECT_EQ(gx_xf_read(0x24), 0x3F800000U);
    EXPECT_EQ(gx_xf_read(0x25), 0x40000000U);
    gxt_xf1(GX_XF_MATRIXINDEX_A, 9U | (30U << 6) | (33U << 24));
    gxt_xf1(GX_XF_MATRIXINDEX_B, 42U << 18);
    GxVertex v;
    gx_xf_default_matrices(&v);
    EXPECT_EQ(v.pos_mtx, 9U);
    EXPECT_EQ(v.tex_mtx[0], 30U);
    EXPECT_EQ(v.tex_mtx[3], 33U);
    EXPECT_EQ(v.tex_mtx[7], 42U);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(perspective_projection_and_viewport);
    RUN_TEST(directional_light_clamped_diffuse);
    RUN_TEST(regular_texgen_from_tex0);
    RUN_TEST(indexed_load_and_default_matrices);
    mem_shutdown();
    return am_test_finish();
}
