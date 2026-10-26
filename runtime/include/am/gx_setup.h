/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Montagem de primitivas e recorte do GX: cada vértice do fluxo passa pelo vertex loader e pelo XF; quads,
 * triângulos, faixas, leques, linhas e pontos viram triângulos, são recortados contra o volume de visão (Sutherland-
 * Hodgman em espaço de recorte), descartados pela face e entregues ao rasterizador.
 *
 * Tradução para C de Dolphin VideoBackends/Software/{SetupUnit,Clipper}.cpp e SWVertexLoader.cpp (tag 2609a). */
#ifndef AM_GX_SETUP_H
#define AM_GX_SETUP_H

#include "am/types.h"

/* Primitivas: bits 3-6 do opcode de desenho (Dolphin OpcodeDecoding.h: Primitive). */
#define GX_PRIM_QUADS      0U
#define GX_PRIM_QUADS_2    1U
#define GX_PRIM_TRIANGLES  2U
#define GX_PRIM_TRI_STRIP  3U
#define GX_PRIM_TRI_FAN    4U
#define GX_PRIM_LINES      5U
#define GX_PRIM_LINE_STRIP 6U
#define GX_PRIM_POINTS     7U

typedef struct GxSetupStats {
    u64 draws;
    u64 state_reuses; /* M9 parte 6: desenhos que reaproveitaram texturas e estado do anterior (nenhuma escrita no meio) */
    u64 vertices;
    u64 culled;   /* triângulos descartados pela face */
    u64 rejected; /* triângulos inteiramente fora do volume de visão */
    /* Recursos do TEV por desenho (estudo do cel-shading, PLANO_PORT_C.md §7). */
    u64 ztex_draws;     /* textura de Z ligada */
    u64 fog_draws;      /* neblina ligada */
    u64 indirect_draws; /* com estágios indiretos */
    u32 max_stages;     /* maior número de estágios do TEV num desenho */
    /* M7: o que os menus usam. */
    u32 tex_formats;    /* bit n: textura no formato n (GX_TF_*) amostrada por algum estágio */
    u32 tlut_formats;   /* bit n: TLUT no formato n nas texturas C4/C8/C14X2 */
    u64 blend_draws;    /* BLENDMODE com blend ligado */
    u64 logic_draws;    /* BLENDMODE com operação lógica */
    u64 subtract_draws; /* blend por subtração */
    u32 alpha_tests;    /* bit n: operação de comparação n (0..7) usada no teste de alfa (comp0 ou comp1) */
    u32 max_texgens;    /* maior número de coordenadas de textura */
    u32 fog_types;      /* bit n: neblina do tipo n (FOGPARAM3.fsel; 2 linear, 4..7 exponenciais) */
    u32 stage_counts;   /* bit n: desenhos com n + 1 estágios de TEV */
} GxSetupStats;

void gx_setup_reset(void);

/* Começa um desenho (lê o estado de BP/XF de todo o pipeline) com a primitiva e o VAT do opcode. */
void gx_setup_begin(u32 primitive, u32 vat);
/* Fim do desenho: rasteriza o que ficou na fila (am/gx_raster.h, modo paralelo). */
void gx_setup_end(void);
/* Um vértice com gx_vtx_size(vat) bytes, no formato do fluxo. */
void gx_setup_vertex(const u8 *data);

const GxSetupStats *gx_setup_stats(void);

#endif /* AM_GX_SETUP_H */
