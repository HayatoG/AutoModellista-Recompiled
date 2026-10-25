/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9 parte 1b: os dois ubershaders da GPU (platform/platform_gpu.c) dão o mesmo EFB, bit a bit. O mesmo lote
 * sintético vai para o MSL à mão do M8 (framebuffer fetch) e para o HLSL (EFB em storage gravável na ordem da
 * rasterização): EFB inicial vindo da CPU, uma limpeza parcial e centenas de triângulos grandes que se sobrepõem, com
 * teste de Z antes e depois do TEV, blend com alfa, subtração, operações lógicas, e RGBA6 com dither. Cada pixel é
 * escrito por dezenas de fragmentos e o resultado depende da ordem, então a igualdade também mostra que o caminho do
 * HLSL respeita a ordem da API (o do framebuffer fetch respeita por construção). docs/M9_PARTE1B_HLSL.md.
 *
 * Sai com 77 (pulado no CTest) sem GPU ou sem um dos dois caminhos (outra plataforma, SDL sem patches/sdl). */
#include <stdio.h>
#include <string.h>

#include "am/platform_gpu.h"

#include "am_test.h"

#define GST_PIXELS (GPU_EFB_WIDTH * GPU_EFB_HEIGHT)
#define GST_TRIS   320U
#define GST_DRAWS  8U /* desenhos de triângulos; mais um de limpeza no começo */
#define GST_SKIP   77

static struct {
    GpuVertex verts[GST_TRIS * 3U];
    GpuDraw draws[GST_DRAWS + 1U];
    u32 color_in[GST_PIXELS];
    u32 depth_in[GST_PIXELS];
    u32 color_out[2][GST_PIXELS];
    u32 depth_out[2][GST_PIXELS];
    u32 seed;
} s_t;

static u32 next(void) {
    s_t.seed = s_t.seed * 1664525U + 1013904223U;
    return s_t.seed >> 8;
}

/* TEV de um estágio: cor e alfa rasterizados (canal 0), sem textura. gx_tev.c: d + lerp(a, b, c), com a = b = c =
 * zero e d = RASC/RASA, clamp ligado. */
static void tev_passthrough(GpuDrawUniforms *u) {
    u->stages = 1;
    u->colchans = 1;
    u->cc[0] = (15U << 12) | (15U << 8) | (15U << 4) | 10U | (1U << 19);
    u->ac[0] = (7U << 13) | (7U << 10) | (7U << 7) | (5U << 4) | (1U << 19);
    u->ksel[0] = 1U << 2; /* tabela de troca 0 = identidade (r, g, b, a) */
    u->ksel[1] = 2U | (3U << 2);
    u->alpha_test = (7U << 16) | (7U << 19); /* sempre passa */
}

/* Estado do PE de cada desenho: formatos, Z antes/depois, blend, subtração, lógica, dither. */
static void pe_state(GpuDrawUniforms *u, u32 d) {
    static const u32 blends[GST_DRAWS] = {
        1U | (1U << 3) | (1U << 4) | (5U << 5) | (4U << 8),             /* src alfa, 1 - src alfa */
        1U | (1U << 3) | (1U << 4) | (1U << 5) | (1U << 8),             /* soma */
        1U | (1U << 11) | (1U << 3) | (1U << 4),                        /* subtração */
        2U | (6U << 12) | (1U << 3) | (1U << 4),                        /* XOR */
        1U | (1U << 3) | (7U << 5) | (6U << 8),                         /* alfa do destino */
        (1U << 3) | (1U << 4),                                          /* cópia */
        1U | (1U << 2) | (1U << 3) | (1U << 4) | (5U << 5) | (4U << 8), /* com dither */
        2U | (13U << 12) | (1U << 3),                                   /* ~s | d */
    };
    u->blend = blends[d];
    u->pe_control = (d >= 4U ? 1U : 0U) | ((d & 1U) != 0U ? (1U << 6) : 0U); /* RGBA6 na segunda metade; Z antes */
    u->zmode = 1U | ((d % 3U == 2U ? 7U : 3U) << 1) | ((d != 5U) ? (1U << 4) : 0U); /* LEQUAL ou ALWAYS */
    u->dst_alpha = d == 6U ? (1U << 8) | 0x40U : 0U;
}

static void build_batch(GpuBatch *b) {
    memset(&s_t.verts, 0, sizeof(s_t.verts));
    memset(&s_t.draws, 0, sizeof(s_t.draws));
    s_t.seed = 12345U;
    for (u32 i = 0; i < GST_PIXELS; i++) {
        s_t.color_in[i] = next() & 0xFFFFFFU;
        s_t.depth_in[i] = next() & 0xFFFFFFU;
    }
    for (u32 v = 0; v < GST_TRIS * 3U; v++) {
        GpuVertex *x = &s_t.verts[v];
        x->pos[0] = (f32)(next() % 1280U) - 320.0F; /* triângulos grandes, que passam das bordas */
        x->pos[1] = (f32)(next() % 1056U) - 264.0F;
        x->pos[2] = (f32)(next() & 0xFFFFFFU);
        x->pos[3] = 1.0F;
        for (u32 c = 0; c < 4U; c++) {
            x->color[0][c] = (f32)(next() % 256U);
        }
    }
    GpuDraw *clear = &s_t.draws[0]; /* limpeza de um retângulo: cor em regs, Z em ztex_bias */
    clear->first_vertex = 0;
    clear->vertex_count = 6;
    clear->scissor[0] = 100;
    clear->scissor[1] = 80;
    clear->scissor[2] = 300;
    clear->scissor[3] = 200;
    clear->u.flags = GPU_FLAG_CLEAR;
    clear->u.blend = (1U << 3) | (1U << 4);
    clear->u.zmode = 1U << 4;
    clear->u.regs[0] = 0x12;
    clear->u.regs[1] = 0x34;
    clear->u.regs[2] = 0x56;
    clear->u.regs[3] = 0x78;
    clear->u.ztex_bias = 0x800000U;
    const u32 per_draw = GST_TRIS / GST_DRAWS;
    for (u32 d = 0; d < GST_DRAWS; d++) {
        GpuDraw *dr = &s_t.draws[d + 1U];
        dr->first_vertex = d * per_draw * 3U;
        dr->vertex_count = per_draw * 3U;
        dr->scissor[2] = (s32)GPU_EFB_WIDTH;
        dr->scissor[3] = (s32)GPU_EFB_HEIGHT;
        tev_passthrough(&dr->u);
        pe_state(&dr->u, d);
    }
    *b = (GpuBatch){.vertices = s_t.verts,
                    .vertex_count = GST_TRIS * 3U,
                    .draws = s_t.draws,
                    .draw_count = GST_DRAWS + 1U,
                    .efb_color_in = s_t.color_in,
                    .efb_depth_in = s_t.depth_in,
                    .readback_top = 0,
                    .readback_bottom = GPU_EFB_HEIGHT};
}

typedef enum RunResult { RUN_OK, RUN_UNAVAILABLE, RUN_FAILED } RunResult;

/* Desenha o lote com um shader e lê o EFB em color_out/depth_out[k]. */
static RunResult run_with(PlatformGpuShader shader, u32 k) {
    if (!platform_gpu_init(shader)) {
        (void)printf("  shader %s indisponível: %s\n", shader == PLATFORM_GPU_SHADER_HAND ? "hand" : "hlsl",
                     platform_gpu_error());
        return RUN_UNAVAILABLE;
    }
    (void)printf("  %s\n", platform_gpu_name());
    GpuBatch b;
    build_batch(&b);
    b.efb_color_out = s_t.color_out[k];
    b.efb_depth_out = s_t.depth_out[k];
    const bool ok = platform_gpu_run(&b);
    if (!ok) {
        (void)printf("  platform_gpu_run: %s\n", platform_gpu_error());
    }
    platform_gpu_shutdown();
    return ok ? RUN_OK : RUN_FAILED;
}

static u32 count_diff(const u32 *a, const u32 *b) {
    u32 n = 0;
    for (u32 i = 0; i < GST_PIXELS; i++) {
        n += a[i] != b[i] ? 1U : 0U;
    }
    return n;
}

TEST(hlsl_igual_ao_msl_a_mao) {
    const u32 dc = count_diff(s_t.color_out[0], s_t.color_out[1]);
    const u32 dz = count_diff(s_t.depth_out[0], s_t.depth_out[1]);
    (void)printf("  %u triângulos em %u desenhos: cor %u e Z %u de %u pixels diferentes\n", GST_TRIS, GST_DRAWS, dc, dz,
                 GST_PIXELS);
    EXPECT_EQ(dc, 0);
    EXPECT_EQ(dz, 0);
    /* O lote mexeu no EFB (não é uma comparação de dois EFBs intocados). */
    EXPECT_TRUE(count_diff(s_t.color_out[0], s_t.color_in) > GST_PIXELS / 2U);
    EXPECT_TRUE(count_diff(s_t.depth_out[0], s_t.depth_in) > GST_PIXELS / 4U);
}

int main(void) {
    const RunResult hand = run_with(PLATFORM_GPU_SHADER_HAND, 0);
    const RunResult hlsl = hand == RUN_OK ? run_with(PLATFORM_GPU_SHADER_HLSL, 1) : hand;
    if (hand == RUN_UNAVAILABLE || hlsl == RUN_UNAVAILABLE) {
        (void)printf("pulado: precisa dos dois ubershaders (Metal em GPU da Apple e SDL com patches/sdl)\n");
        return GST_SKIP;
    }
    if (hlsl != RUN_OK) {
        return 1;
    }
    RUN_TEST(hlsl_igual_ao_msl_a_mao);
    return am_test_finish();
}
