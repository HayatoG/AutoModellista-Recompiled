/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: o que o GX entrega à GPU (gfx/gx_gpu.c -> platform/platform_gpu.c). Só tipos simples: a camada de plataforma
 * inclui este header sem depender do resto da runtime.
 *
 * Um lote são os desenhos entre duas cópias do EFB: vértices já transformados pelo XF da CPU (em pixels do EFB),
 * um uniform por desenho com o estado do TEV/PE (GpuDrawUniforms = DrawState de runtime/shaders/gx_ubershader.metal,
 * mesma ordem de campos, só escalares de 32 bits) e as texturas novas para o buffer de texels (RGBA8 decodificado,
 * persistente entre lotes). A GPU desenha no EFB dela (cor empacotada e Z de 24 bits, como gx_efb.c) e devolve as
 * duas imagens para a CPU, que faz a cópia (XFB ou textura) com o código de sempre. */
#ifndef AM_GPU_BATCH_H
#define AM_GPU_BATCH_H

#include "am/types.h"

#define GPU_EFB_WIDTH  640U
#define GPU_EFB_HEIGHT 528U
#define GPU_STAGES     16U
#define GPU_TEXMAPS    8U
#define GPU_TEXCOORDS  8U
/* Buffer de texels decodificados (u32 RGBA cada): 32 Mi texels = 128 MiB; cheio, recomeça do zero. */
#define GPU_ARENA_TEXELS (32U * 1024U * 1024U)

typedef struct GpuVertex {
    f32 pos[4]; /* x, y em pixels do EFB (já com o deslocamento do scissor e o centro do pixel), z (24 bits), w */
    f32 color[2][4];
    f32 tex[GPU_TEXCOORDS][4]; /* s, t, q (já na escala do SU), 0 */
} GpuVertex;

typedef struct GpuDrawUniforms {
    u32 stages, texgens, ind_stages, colchans;
    u32 zmode, pe_control, blend, dst_alpha;
    u32 alpha_test, fog3, fog_color, fog_bmag;
    u32 fog_bshift, ztex2, ztex_bias, flags;
    f32 fog_a, fog_c, vp_wd, pad0;
    u32 fog_range[8];
    u32 cc[GPU_STAGES];
    u32 ac[GPU_STAGES];
    u32 order[GPU_STAGES]; /* TREF do estágio (12 bits) */
    u32 ksel[8];
    s32 regs[16];
    s32 konst[16];
    u32 tex_size[GPU_TEXMAPS]; /* largura - 1 | (altura - 1) << 16 */
    u32 tex_mode0[GPU_TEXMAPS];
    u32 tex_mode1[GPU_TEXMAPS];
    u32 tex_levels[GPU_TEXMAPS]; /* 0 = sem dados (amostra preta) */
    u32 tex_base[GPU_TEXMAPS];   /* índice do primeiro texel no buffer de texels; os níveis seguem em ordem */
} GpuDrawUniforms;

typedef struct GpuDraw {
    u32 first_vertex;
    u32 vertex_count;
    s32 scissor[4]; /* x, y, largura, altura no EFB */
    GpuDrawUniforms u;
} GpuDraw;

typedef struct GpuTexUpload {
    u32 offset; /* em texels */
    u32 texels;
    const u8 *rgba;
} GpuTexUpload;

typedef struct GpuBatch {
    const GpuVertex *vertices;
    u32 vertex_count;
    const GpuDraw *draws;
    u32 draw_count;
    const GpuTexUpload *uploads;
    u32 upload_count;
    /* Se não NULL, o EFB da GPU é substituído por estes antes de desenhar (a CPU limpou ou mudou o EFB). */
    const u32 *efb_color_in;
    const u32 *efb_depth_in;
    /* Destino da leitura do EFB depois dos desenhos (GPU_EFB_WIDTH * GPU_EFB_HEIGHT cada). NULL = lote parcial:
     * enviado sem esperar a GPU (os desenhos continuam no lote seguinte, na mesma ordem). */
    u32 *efb_color_out;
    u32 *efb_depth_out; /* NULL = sem ler o Z (a cópia é de cor) */
    u32 readback_top;   /* linhas [readback_top, readback_bottom) */
    u32 readback_bottom;
} GpuBatch;

/* GpuDrawUniforms.flags */
#define GPU_FLAG_CLEAR 1U /* limpeza do EFB: cor em regs[0..3], Z em ztex_bias, máscaras em blend/zmode */

#endif /* AM_GPU_BATCH_H */
