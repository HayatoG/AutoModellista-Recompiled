/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Transform unit (XF) do GX: memória de matrizes e luzes, registradores (canais de cor, texgen, viewport, projeção)
 * e a transformação de um vértice: posição para espaço de recorte, normais, iluminação dos dois canais de cor e
 * geração de coordenadas de textura.
 *
 * Endereços do XF (palavras de 32 bits): 0x000-0x0FF matrizes de posição/textura (linhas de 4), 0x400-0x45F matrizes
 * de normal (3x3), 0x500-0x5FF matrizes pós-transformação, 0x600-0x67F luzes (16 palavras cada), 0x1000-0x1057
 * registradores. Fontes: YAGCD 5.15; Dolphin VideoCommon/XFMemory.h, XFStructs.cpp e
 * VideoBackends/Software/TransformUnit.cpp (tag 2609a), do qual a transformação é uma tradução para C. */
#ifndef AM_GX_XF_H
#define AM_GX_XF_H

#include "am/gx_vtx.h"
#include "am/types.h"

#define GX_XF_REGS          0x1000U
#define GX_XF_END           0x1058U
#define GX_XF_CLIPDISABLE   0x1005U
#define GX_XF_NUMCHAN       0x1009U
#define GX_XF_AMBCOLOR0     0x100AU
#define GX_XF_MATCOLOR0     0x100CU
#define GX_XF_COLOR0        0x100EU
#define GX_XF_ALPHA0        0x1010U
#define GX_XF_DUALTEX       0x1012U
#define GX_XF_MATRIXINDEX_A 0x1018U
#define GX_XF_MATRIXINDEX_B 0x1019U
#define GX_XF_VIEWPORT      0x101AU /* wd, ht, zRange, xOrig, yOrig, farZ (f32) */
#define GX_XF_PROJECTION    0x1020U /* 6 f32 + tipo (0x1026: 0 perspectiva, 1 ortográfica) */
#define GX_XF_NUMTEXGENS    0x103FU
#define GX_XF_TEXMTXINFO    0x1040U
#define GX_XF_POSTMTXINFO   0x1050U

typedef struct GxViewport {
    f32 wd, ht, z_range, x_orig, y_orig, far_z;
} GxViewport;

/* Vértice depois do XF (OutputVertexData do Dolphin): cores em RGBA, texcoords já multiplicadas pelo tamanho da
 * textura (BP SU_SSIZE/TSIZE) pelo rasterizador. */
typedef struct GxXfVertex {
    f32 mv[3];     /* posição no espaço da câmera */
    f32 proj[4];   /* espaço de recorte (x, y, z, w) */
    f32 screen[3]; /* depois da divisão por w e do viewport (coordenadas com o deslocamento 342 do GX) */
    f32 nrm[3][3];
    u8 color[2][4];
    f32 tex[GX_TEXCOORD_MAX][3];
} GxXfVertex;

void gx_xf_reset(void);
void gx_xf_write(u32 addr, u32 value);
u32 gx_xf_read(u32 addr);

/* LOAD_INDX_A..D: `size` palavras do elemento `index` do array 12..15 para o endereço `addr` do XF. */
void gx_xf_load_indexed(u32 array, u32 index, u32 addr, u32 size);

/* Índices de matriz padrão (MatrixIndexA/B), que o vertex loader substitui pelos do vértice. */
void gx_xf_default_matrices(GxVertex *v);

GxViewport gx_xf_viewport(void);
u32 gx_xf_num_texgens(void);

/* Posição, normais, cores e texcoords (sem a escala de textura do BP). */
void gx_xf_transform(const GxVertex *in, GxXfVertex *out);

/* Divisão perspectiva e viewport (Clipper::PerspectiveDivide). */
void gx_xf_perspective_divide(GxXfVertex *v);

#endif /* AM_GX_XF_H */
