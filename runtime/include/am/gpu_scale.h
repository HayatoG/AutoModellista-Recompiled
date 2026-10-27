/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9: resolução interna maior (--internal-res N) no caminho --gfx gpu. A GPU desenha o EFB em N x N pixels por pixel
 * nativo (os vértices continuam em pixels nativos; o tamanho do alvo faz a escala, docs/M9_RESOLUCAO_INTERNA.md).
 * Aqui ficam as contas puras (sem SDL, sem estado) que a CPU faz nesse caminho: escala de retângulos, bias de LOD,
 * replicação do EFB nativo para o grande e redução por média de caixa de volta ao nativo. Só tipos simples: serve a
 * gfx/ e a platform/. Com N = 1 nada daqui roda. */
#ifndef AM_GPU_SCALE_H
#define AM_GPU_SCALE_H

#include "am/types.h"

#define GPU_SCALE_MAX 4U

/* Limita o pedido a 1..GPU_SCALE_MAX. */
static inline u32 gpu_scale_clamp(s32 n) {
    return n < 1 ? 1U : (n > (s32)GPU_SCALE_MAX ? GPU_SCALE_MAX : (u32)n);
}

/* Retângulo x, y, largura, altura do EFB nativo -> EFB grande. */
static inline void gpu_scale_rect(const s32 in[4], u32 n, s32 out[4]) {
    for (u32 i = 0; i < 4U; i++) {
        out[i] = in[i] * (s32)n;
    }
}

/* Bias de LOD do TEV em unidades do campo de 8 bits de tex_mode0 (bits 9-16, s2.5 com sinal: 1/32 de LOD) que repõe o
 * que a derivada perde com N: ddx/ddy caem a 1/N, o LOD cai log2(N), logo soma 32 * log2(N). Exato em N = 2 e 4;
 * em N = 3, 50,7 arredondado para 51 (erro de 0,01 de LOD). */
static inline s32 gpu_scale_lod_units(u32 n) {
    static const s32 UNITS[GPU_SCALE_MAX + 1U] = {0, 0, 32, 51, 64};
    return UNITS[n > GPU_SCALE_MAX ? GPU_SCALE_MAX : n];
}

/* Devolve tex_mode0 com o bias somado (saturado em -128..127, como o campo). */
static inline u32 gpu_scale_lod_mode0(u32 mode0, u32 n) {
    if (n <= 1U) {
        return mode0;
    }
    const u32 raw = (mode0 >> 9) & 0xFFU;
    s32 bias = raw >= 128U ? (s32)raw - 256 : (s32)raw;
    bias += gpu_scale_lod_units(n);
    bias = bias > 127 ? 127 : bias;
    return (mode0 & ~(0xFFU << 9)) | (((u32)bias & 0xFFU) << 9);
}

/* Replica cada pixel nativo NxN. src: w x h; dst: (w*n) x (h*n). */
static inline void gpu_scale_replicate(const u32 *src, u32 *dst, u32 w, u32 h, u32 n) {
    const size_t stride = (size_t)w * n;
    for (u32 y = 0; y < h; y++) {
        u32 *row = dst + ((size_t)y * n * stride);
        for (u32 x = 0; x < w; x++) {
            const u32 v = src[((size_t)y * w) + x];
            for (u32 k = 0; k < n; k++) {
                row[((size_t)x * n) + k] = v;
            }
        }
        for (u32 k = 1; k < n; k++) {
            for (size_t i = 0; i < stride; i++) {
                row[(k * stride) + i] = row[i];
            }
        }
    }
}

/* Média de caixa por canal das linhas nativas [y0, y1): hi tem (w*n) de largura e os pixels empacotados do EFB
 * (RGBA6: R,G,B,A de 6 bits em 23..0; senão RGB8 em 23..0). A média é no domínio do canal, arredondada para o mais
 * próximo. O resultado é um pixel válido do formato (quem lê o EFB nativo não percebe a diferença). */
static inline void gpu_scale_reduce_color(const u32 *hi, u32 *dst, u32 w, u32 y0, u32 y1, u32 n, bool rgba6) {
    const u32 bits = rgba6 ? 6U : 8U;
    const u32 chans = rgba6 ? 4U : 3U;
    const u32 mask = (1U << bits) - 1U;
    const u32 area = n * n;
    const size_t stride = (size_t)w * n;
    for (u32 y = y0; y < y1; y++) {
        for (u32 x = 0; x < w; x++) {
            u32 sum[4] = {0, 0, 0, 0};
            for (u32 j = 0; j < n; j++) {
                const u32 *p = hi + ((((size_t)y * n) + j) * stride) + ((size_t)x * n);
                for (u32 i = 0; i < n; i++) {
                    for (u32 c = 0; c < chans; c++) {
                        sum[c] += (p[i] >> (bits * (chans - 1U - c))) & mask;
                    }
                }
            }
            u32 v = 0;
            for (u32 c = 0; c < chans; c++) {
                v = (v << bits) | ((sum[c] + (area / 2U)) / area);
            }
            dst[((size_t)y * w) + x] = v;
        }
    }
}

/* Z: média não serve (mistura frente e fundo na borda); fica a amostra do centro do bloco. */
static inline void gpu_scale_reduce_depth(const u32 *hi, u32 *dst, u32 w, u32 y0, u32 y1, u32 n) {
    const size_t stride = (size_t)w * n;
    for (u32 y = y0; y < y1; y++) {
        const u32 *row = hi + ((((size_t)y * n) + (n / 2U)) * stride) + (n / 2U);
        for (u32 x = 0; x < w; x++) {
            dst[((size_t)y * w) + x] = row[(size_t)x * n];
        }
    }
}

#endif /* AM_GPU_SCALE_H */
