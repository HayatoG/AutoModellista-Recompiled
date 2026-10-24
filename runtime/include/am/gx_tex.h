/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Texturas do GX: decodificação de um texel em qualquer formato do GameCube (I4, I8, IA4, IA8, RGB565, RGB5A3,
 * RGBA8, C4, C8, C14X2 com TLUT IA8/RGB565/RGB5A3, e CMPR) e a amostragem de uma unidade de textura (wrap, filtro
 * bilinear, mipmaps) como o TEV pede. A textura é lida direto da memória do guest (ou da TMEM, quando o jogo a
 * gerencia), sempre em big-endian.
 *
 * Fontes: YAGCD 17 (formatos); Dolphin VideoCommon/TextureDecoder_Common.cpp (TexDecoder_DecodeTexel),
 * TextureDecoder_Generic.cpp, LookUpTables.h e VideoBackends/Software/TextureSampler.cpp (tag 2609a). */
#ifndef AM_GX_TEX_H
#define AM_GX_TEX_H

#include "am/types.h"

#define GX_TF_I4     0x0U
#define GX_TF_I8     0x1U
#define GX_TF_IA4    0x2U
#define GX_TF_IA8    0x3U
#define GX_TF_RGB565 0x4U
#define GX_TF_RGB5A3 0x5U
#define GX_TF_RGBA8  0x6U
#define GX_TF_C4     0x8U
#define GX_TF_C8     0x9U
#define GX_TF_C14X2  0xAU
#define GX_TF_CMPR   0xEU

#define GX_TLUT_IA8    0U
#define GX_TLUT_RGB565 1U
#define GX_TLUT_RGB5A3 2U

/* Fonte de um texel: bytes da textura e da TLUT com os tamanhos válidos (leitura fora deles dá zero). */
typedef struct GxTexSource {
    const u8 *data;
    u32 size;
    const u8 *tlut;
    u32 tlut_size;
    u32 tlut_format;
} GxTexSource;

/* Texel (s, t) de uma textura de largura `width_minus_1 + 1`, em RGBA. */
void gx_tex_decode_texel(u8 *rgba, const GxTexSource *src, u32 s, u32 t, u32 width_minus_1, u32 format);

/* Bytes de um nível de `width` x `height` texels (arredondado aos blocos do formato). */
u32 gx_tex_level_size(u32 format, u32 width, u32 height);

/* Lê das unidades de textura do BP os parâmetros que gx_tex_sample usa; chamar antes de cada desenho. Com o cache
 * ligado, os níveis das texturas que o desenho usa são decodificados (ou achados) aqui, antes de rasterizar. */
void gx_tex_prepare(void);

/* Cache de texturas decodificadas (M7): ligado por padrão; desligar volta à decodificação texel a texel (a imagem é a
 * mesma; tests/unit/gx_tex_test.c confere). */
#define GX_TEX_CACHE_LEVELS 11U
typedef struct GxTexCacheStats {
    u64 hits;
    u64 misses;
    u64 decoded_texels;
} GxTexCacheStats;
void gx_tex_set_cache(bool on);
void gx_tex_cache_clear(void);
const GxTexCacheStats *gx_tex_cache_stats(void);

/* Amostra a unidade `texmap` em (s, t) no formato s17.7 do TEV, com LOD s28.4 e filtro linear ou não. */
void gx_tex_sample(s32 s, s32 t, s32 lod, bool linear, u32 texmap, u8 *rgba);

#endif /* AM_GX_TEX_H */
