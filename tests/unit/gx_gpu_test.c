/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: o backend de GPU do GX com uma GPU de mentira: o que o GX entrega (vértices em pixels, uniform do desenho,
 * limpeza como desenho, linhas lidas de volta) e a cópia para o XFB feita na CPU com o EFB que a "GPU" devolve. */
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/gx_fifo.h"
#include "am/gx_gpu.h"
#include "am/mem.h"

#include "am_test.h"
#include "gx_test_util.h"

static struct {
    u32 runs;
    u32 draws;
    GpuDraw first;
    GpuVertex verts[16];
    u32 vert_count;
    GpuDraw clear;
    bool saw_clear;
    u32 top, bottom;
    bool depth;
} s_fake;

/* "Desenha" pintando de branco os pixels cobertos pelo primeiro desenho (dois na linha 0) e guarda o lote. */
static bool fake_run(const GpuBatch *b) {
    s_fake.runs++;
    s_fake.draws += b->draw_count;
    for (u32 i = 0; i < b->draw_count; i++) {
        const GpuDraw *d = &b->draws[i];
        if ((d->u.flags & GPU_FLAG_CLEAR) != 0U) {
            s_fake.clear = *d;
            s_fake.saw_clear = true;
        } else if (s_fake.vert_count == 0U) {
            s_fake.first = *d;
            s_fake.vert_count = d->vertex_count < 16U ? d->vertex_count : 16U;
            memcpy(s_fake.verts, b->vertices + d->first_vertex, s_fake.vert_count * sizeof(GpuVertex));
        }
    }
    if (b->efb_color_out != NULL) {
        s_fake.top = b->readback_top;
        s_fake.bottom = b->readback_bottom;
        s_fake.depth = b->efb_depth_out != NULL;
        b->efb_color_out[0] = 0xFFFFFFU;
        b->efb_color_out[1] = 0xFFFFFFU;
    }
    return true;
}

static void start(void) {
    memset(&s_fake, 0, sizeof(s_fake));
    gx_fifo_reset(NULL);
    gx_fifo_set_render(true);
    gx_gpu_reset();
    gx_gpu_set_backend(fake_run);
    gxt_setup_2d();
}

static void copy_to_xfb_with_clear(void) {
    gxt_bp(GX_BP_EFB_TL, 0);
    gxt_bp(GX_BP_EFB_WH, 3U | (1U << 10)); /* 4 x 2 */
    gxt_bp(GX_BP_EFB_ADDR, 0x00350000U >> 5);
    gxt_bp(GX_BP_EFB_STRIDE, 1);
    gxt_bp(GX_BP_COPYYSCALE, 256);
    gxt_bp(GX_BP_COPYFILTER0, (21U << 12) | (22U << 18));
    gxt_bp(GX_BP_COPYFILTER1, 21U);
    gxt_bp(GX_BP_CLEAR_AR, 0xFF10);
    gxt_bp(GX_BP_CLEAR_GB, 0x2030);
    gxt_bp(GX_BP_COPY_EXECUTE, (1U << 14) | (1U << 11));
}

TEST(draw_goes_to_the_gpu_and_copy_uses_the_readback) {
    start();
    gxt_quad(10.0F, 20.0F, 14.0F, 23.0F, 0xFF8000FFU);
    EXPECT_EQ(s_fake.runs, 0U); /* nada vai para a GPU antes da cópia */
    copy_to_xfb_with_clear();
    EXPECT_EQ(s_fake.runs, 1U);
    EXPECT_EQ(s_fake.first.vertex_count, 6U); /* o quad vira dois triângulos */
    EXPECT_EQ(s_fake.first.u.stages, 1U);
    EXPECT_EQ(s_fake.first.u.blend, gx_bp_reg(GX_BP_BLENDMODE));
    bool left = false;
    bool right = false;
    for (u32 i = 0; i < s_fake.vert_count; i++) {
        left = left || s_fake.verts[i].pos[0] == 10.0F - (1.0F / 16.0F);
        right = right || s_fake.verts[i].pos[0] == 14.0F - (1.0F / 16.0F);
        EXPECT_TRUE(s_fake.verts[i].color[0][0] == 255.0F && s_fake.verts[i].color[0][1] == 128.0F);
    }
    EXPECT_TRUE(left && right);
    EXPECT_EQ(s_fake.top, 0U); /* a cópia lê as linhas 0 e 1, mais uma abaixo para o filtro */
    EXPECT_EQ(s_fake.bottom, 3U);
    EXPECT_TRUE(!s_fake.depth);
    /* A cópia na CPU usou o EFB devolvido: dois pixels brancos, o resto preto. */
    EXPECT_EQ(mem_read32(NULL, 0x80350000U), 0xEB80EB80U);
    EXPECT_EQ(mem_read32(NULL, 0x80350004U), 0x10801080U);
    EXPECT_EQ(gx_gpu_stats()->copies, 1U);
}

TEST(clear_becomes_a_gpu_draw) {
    start();
    copy_to_xfb_with_clear();
    EXPECT_TRUE(!s_fake.saw_clear); /* a limpeza entra no lote seguinte */
    copy_to_xfb_with_clear();
    EXPECT_TRUE(s_fake.saw_clear);
    EXPECT_EQ(s_fake.clear.vertex_count, 6U);
    EXPECT_EQ(s_fake.clear.u.regs[0], 0x10);
    EXPECT_EQ(s_fake.clear.u.regs[1], 0x20);
    EXPECT_EQ(s_fake.clear.u.regs[2], 0x30);
    EXPECT_EQ(s_fake.clear.scissor[2], 4);
    EXPECT_EQ(s_fake.clear.scissor[3], 2);
    gx_gpu_set_backend(NULL);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(draw_goes_to_the_gpu_and_copy_uses_the_readback);
    RUN_TEST(clear_becomes_a_gpu_draw);
    return am_test_finish();
}
