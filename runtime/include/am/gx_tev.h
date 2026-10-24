/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TEV do GX: até 16 estágios de combinação (cor e alfa) com texturas, cor rasterizada, registradores e konst, mais
 * texturas indiretas, teste de alfa, textura de Z, neblina e o Z tardio; o resultado vai ao EFB (gx_efb_blend).
 * Aritmética inteira como o hardware (entradas a/b/c de 8 bits, d de 11 bits com sinal, escala e bias).
 *
 * Tradução para C de Dolphin VideoBackends/Software/Tev.cpp (tag 2609a). */
#ifndef AM_GX_TEV_H
#define AM_GX_TEV_H

#include "am/gx_vtx.h"
#include "am/types.h"

#define GX_TEV_STAGES 16U

/* Um pixel saindo do rasterizador. */
typedef struct GxTevPixel {
    s32 x, y, z;
    u8 color[2][4];             /* RGBA dos dois canais */
    s32 uv[GX_TEXCOORD_MAX][2]; /* s17.7 */
    const s32 *lod;             /* [GX_TEV_STAGES], do bloco 2x2 */
    const bool *linear;         /* [GX_TEV_STAGES] */
    const s32 *ind_lod;         /* [4] */
    const bool *ind_linear;     /* [4] */
} GxTevPixel;

/* Decodifica do BP os estágios, as cores e os testes; chamar antes de cada desenho (depois de gx_tex_prepare). */
void gx_tev_begin_draw(void);

/* Z antecipado ligado (PEControl.early_ztest com teste de Z): o rasterizador testa antes do TEV. */
bool gx_tev_early_z(void);

/* Executa o TEV e as etapas seguintes até o EFB. */
void gx_tev_shade(const GxTevPixel *p);

#endif /* AM_GX_TEV_H */
