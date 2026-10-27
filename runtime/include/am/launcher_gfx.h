/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Contas da tela inicial sem nada de interface (testáveis): fator de escala do layout, enquadramento "cover" do fundo e
 * a miniatura do vidro fosco (redução por média de caixa na CPU, esticada com filtro linear por trás dos painéis). */
#ifndef AM_LAUNCHER_GFX_H
#define AM_LAUNCHER_GFX_H

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tamanho de referência do design (work/launcher-bg/design_Main.dc.html). */
#define LAUNCHER_DESIGN_W 1280.0F
#define LAUNCHER_DESIGN_H 760.0F

typedef struct LauncherRect {
    float x, y, w, h;
} LauncherRect;

/* min(largura/1280, altura/760), limitado a [0.4, 8]: fontes e espaçamentos da janela de qualquer tamanho. */
float launcher_scale(float width, float height);

/* Retângulo, em coordenadas da janela (0,0 = canto), que a imagem img_w x img_h ocupa no modo cover: preenche a janela
 * inteira mantendo a proporção, centralizada, e o excesso é cortado. */
LauncherRect launcher_cover(float view_w, float view_h, u32 img_w, u32 img_h);

/* Largura da miniatura do vidro (16:9) para a intensidade (4..48 px de desfoque do design): menos pixels, mais
 * desfoque. */
u32 launcher_glass_width(u32 blur);

/* Média de caixa com cobertura fracionária (área exata de cada pixel de origem): RGBA 8 bits, src sw x sh para dst
 * dw x dh (qualquer razão, inclusive ampliando). */
void launcher_box_reduce(const u8 *src, u32 sw, u32 sh, u8 *dst, u32 dw, u32 dh);

/* Quantos passes de 3x3 (pesos 1-2-1) imitam o desfoque gaussiano de `blur` px do design numa miniatura de `width`
 * texels de largura (sigma = 0,6 x blur px do design; cada passe soma ~0,71 texel de sigma). */
u32 launcher_glass_passes(u32 blur, u32 width);

/* Suaviza com `passes` passes de 3x3 (bordas repetidas) e satura (1 = igual; o design usa saturate(170%)). Altera
 * `rgba` (w x h) no lugar. */
void launcher_glass_finish(u8 *rgba, u32 w, u32 h, u32 passes, float saturation);

#ifdef __cplusplus
}
#endif

#endif /* AM_LAUNCHER_GFX_H */
