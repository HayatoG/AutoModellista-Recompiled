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
    u64 triangles;      /* triângulos rasterizados (depois de recorte e descarte) */
    u64 pixels;         /* pixels que entraram no TEV */
    u64 parallel_draws; /* desenhos divididos em faixas entre os trabalhadores */
} GxRasterStats;

void gx_raster_reset(void);

/* Paralelismo (M7): `run(jobs, job, user)` executa job(índice, trabalhador, user) para índice em [0, jobs), cada
 * trabalhador (0 .. workers - 1) com um índice por vez, e só volta quando todos terminaram. Com ele registrado, os
 * triângulos de um desenho vão para uma fila e gx_raster_flush os rasteriza em faixas intercaladas de linhas de
 * blocos 2x2: cada pixel é tocado por uma faixa só, na ordem dos triângulos, então a imagem é a mesma com qualquer
 * número de trabalhadores. NULL (ou workers <= 1) volta ao modo direto, sem fila. */
#define GX_RASTER_MAX_WORKERS 16U
typedef void (*GxRasterJobFn)(u32 index, u32 worker, void *user);
typedef void (*GxRasterParallelFn)(u32 jobs, GxRasterJobFn job, void *user);
void gx_raster_set_parallel(GxRasterParallelFn run, u32 workers);
/* Executa job(0 .. jobs - 1) pelos trabalhadores registrados (ou em sequência, sem eles). Para outros laços do GX
 * cujo resultado não depende da ordem (a cópia EFB -> XFB, linha a linha). */
void gx_raster_parallel_for(u32 jobs, GxRasterJobFn job, void *user);
/* Rasteriza a fila do desenho atual (fim do desenho; sem fila, não faz nada). */
void gx_raster_flush(void);

/* Lê do BP o scissor, os contadores do GENMODE e as escalas de textura; chamar antes de cada desenho. */
void gx_raster_begin_draw(void);

/* Triângulo de frente (já recortado, com screen[] calculado). */
void gx_raster_triangle(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2);

/* Atualiza o declive de Z sem desenhar (triângulo descartado pela face): o zfreeze depende dele. */
void gx_raster_update_z_slope(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2);

const GxRasterStats *gx_raster_stats(void);
/* M8: retângulos de scissor do desenho atual (deslocamento x/y, left, top, right, bottom), para o backend de GPU. */
#define GX_RASTER_MAX_RECTS 81U
u32 gx_raster_rects(s32 (*out)[6], u32 max);

#endif /* AM_GX_RASTER_H */
