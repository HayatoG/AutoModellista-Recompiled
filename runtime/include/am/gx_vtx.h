/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Vertex loader do GX: registradores do CP que o fluxo de comandos carrega (VCD, VAT, bases e passos dos arrays,
 * índices de matriz) e a leitura de um vértice no formato do jogo para floats.
 *
 * Um vértice no fluxo é a sequência PosMtxIdx, Tex0..7MtxIdx, posição, normal(is), cor 0, cor 1, Tex0..7; cada
 * atributo é ausente, direto ou um índice de 8/16 bits num array da memória (base e passo nos registradores 0xA0 e
 * 0xB0 do CP). Fontes: YAGCD 5.11; Dolphin VideoCommon/CPMemory.h (TVtxDesc, VAT_group0/1/2),
 * VertexLoader_{Position,Normal,Color,TextCoord}.cpp e VideoBackends/Software/SWVertexLoader.cpp (tag 2609a). */
#ifndef AM_GX_VTX_H
#define AM_GX_VTX_H

#include "am/types.h"

#define GX_VAT_COUNT     8U
#define GX_TEXCOORD_MAX  8U
#define GX_ARRAY_COUNT   16U /* 0 posição, 1 normal, 2-3 cores, 4-11 texcoords, 12-15 cargas indexadas do XF */
#define GX_ARRAY_XF_BASE 12U

/* Registradores do CP no fluxo (Dolphin CPMemory.h: MATINDEX_A/B, VCD, VAT, ARRAY_BASE, ARRAY_STRIDE). */
#define GX_CP_MATINDEX_A   0x30U
#define GX_CP_MATINDEX_B   0x40U
#define GX_CP_VCD_LO       0x50U
#define GX_CP_VCD_HI       0x60U
#define GX_CP_VAT_A        0x70U
#define GX_CP_VAT_B        0x80U
#define GX_CP_VAT_C        0x90U
#define GX_CP_ARRAY_BASE   0xA0U
#define GX_CP_ARRAY_STRIDE 0xB0U

/* Vértice lido, antes do XF. Cores em RGBA (como o SWVertexLoader); matrizes como índice de linha (0..63). */
typedef struct GxVertex {
    u8 pos_mtx;
    u8 tex_mtx[GX_TEXCOORD_MAX];
    f32 pos[3];
    f32 nrm[3][3]; /* normal, tangente, binormal */
    u8 color[2][4];
    f32 tex[GX_TEXCOORD_MAX][2];
    bool skip; /* índice de posição 0xFF/0xFFFF: o vértice sai da primitiva (VertexLoader m_vertexSkip) */
} GxVertex;

void gx_vtx_reset(void);
void gx_vtx_load_cp(u8 reg, u32 value);
u32 gx_vtx_cp(u8 reg);

/* Tamanho em bytes de um vértice com o VCD atual e o formato `vat` (0..7). */
u32 gx_vtx_size(u32 vat);

/* Lê um vértice de `gx_vtx_size(vat)` bytes. `out` chega com os índices de matriz padrão (XF MatrixIndexA/B), que
 * os índices por vértice substituem. Atributos indexados são lidos da memória do guest. */
void gx_vtx_decode(u32 vat, const u8 *data, GxVertex *out);

/* Endereço físico do elemento `index` do array `array` (base + índice * passo). */
u32 gx_vtx_array_addr(u32 array, u32 index);

#endif /* AM_GX_VTX_H */
