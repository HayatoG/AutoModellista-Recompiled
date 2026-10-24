/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Rasterização de triângulos do GX em software: coordenadas em ponto fixo 28.4, regra de preenchimento, recorte
 * pelos retângulos de scissor, travessia em blocos de 2x2 (o LOD de textura sai das diferenças dentro do bloco) e
 * interpolação com correção de perspectiva de cores, texcoords e Z; cada pixel coberto vai ao TEV.
 *
 * Tradução para C de Dolphin VideoBackends/Software/Rasterizer.cpp e VideoCommon/BPFunctions.cpp
 * (ComputeScissorRects), tag 2609a. */
#ifndef AM_GX_RASTER_H
#define AM_GX_RASTER_H

#include "am/gx_xf.h"
#include "am/types.h"

typedef struct GxRasterStats {
    u64 triangles; /* triângulos rasterizados (depois de recorte e descarte) */
    u64 pixels;    /* pixels que entraram no TEV */
} GxRasterStats;

void gx_raster_reset(void);

/* Lê do BP o scissor, os contadores do GENMODE e as escalas de textura; chamar antes de cada desenho. */
void gx_raster_begin_draw(void);

/* Triângulo de frente (já recortado, com screen[] calculado). */
void gx_raster_triangle(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2);

/* Atualiza o declive de Z sem desenhar (triângulo descartado pela face): o zfreeze depende dele. */
void gx_raster_update_z_slope(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2);

const GxRasterStats *gx_raster_stats(void);

#endif /* AM_GX_RASTER_H */
