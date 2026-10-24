/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Decodificadores de textura (gfx/gx_tex.c) com blocos montados à mão e o amostrador (wrap, filtro bilinear). */
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_tex.h"
#include "am/mem.h"

#include "am_test.h"

static void texel(const u8 *data, u32 size, u32 s, u32 t, u32 width, u32 fmt, u8 *out) {
    const GxTexSource src = {data, size, NULL, 0, 0};
    gx_tex_decode_texel(out, &src, s, t, width - 1U, fmt);
}

static bool rgba_is(const u8 *c, u32 r, u32 g, u32 b, u32 a) {
    return c[0] == r && c[1] == g && c[2] == b && c[3] == a;
}

TEST(i4_i8_ia4_ia8_blocks) {
    u8 i4[32];
    for (u32 i = 0; i < 32U; i++) {
        i4[i] = (u8)((((2U * i) & 15U) << 4) | ((2U * i + 1U) & 15U)); /* texel n = n & 15 */
    }
    u8 c[4];
    texel(i4, sizeof(i4), 3, 2, 8, GX_TF_I4, c); /* n = 2 * 8 + 3 = 19 -> 3 */
    EXPECT_TRUE(rgba_is(c, 0x33, 0x33, 0x33, 0x33));
    u8 i8[64]; /* 16x4: dois blocos de 8x4 lado a lado */
    for (u32 i = 0; i < 64U; i++) {
        i8[i] = (u8)i;
    }
    texel(i8, sizeof(i8), 9, 2, 16, GX_TF_I8, c); /* bloco 1, (1, 2) -> 32 + 2 * 8 + 1 */
    EXPECT_TRUE(rgba_is(c, 49, 49, 49, 49));
    const u8 ia4[32] = {[5] = 0xA3};
    texel(ia4, sizeof(ia4), 5, 0, 8, GX_TF_IA4, c);
    EXPECT_TRUE(rgba_is(c, 0x33, 0x33, 0x33, 0xAA)); /* alfa no nibble alto */
    u8 ia8[32] = {0};
    const size_t k = 18; /* (1, 2): texel 2 * 4 + 1, dois bytes cada */
    ia8[k] = 0x80;       /* alfa */
    ia8[k + 1U] = 0x40;  /* intensidade */
    texel(ia8, sizeof(ia8), 1, 2, 4, GX_TF_IA8, c);
    EXPECT_TRUE(rgba_is(c, 0x40, 0x40, 0x40, 0x80));
}

TEST(rgb565_rgb5a3_rgba8) {
    u8 b[64] = {0};
    b[0] = 0xF8; /* (0, 0) RGB565 vermelho puro */
    b[1] = 0x00;
    b[2] = 0x07; /* (1, 0) verde: 0x07E0 */
    b[3] = 0xE0;
    u8 c[4];
    texel(b, sizeof(b), 0, 0, 4, GX_TF_RGB565, c);
    EXPECT_TRUE(rgba_is(c, 255, 0, 0, 255));
    texel(b, sizeof(b), 1, 0, 4, GX_TF_RGB565, c);
    EXPECT_TRUE(rgba_is(c, 0, 255, 0, 255));
    b[4] = 0x80 | (0x1FU << 2); /* (2, 0) RGB5A3 opaco: R = 31 */
    b[5] = 0x00;
    b[6] = 0x3A; /* (3, 0) 4443: alfa 3, R = 0xA, G = 0x5, B = 0xC */
    b[7] = 0x5C;
    texel(b, sizeof(b), 2, 0, 4, GX_TF_RGB5A3, c);
    EXPECT_TRUE(rgba_is(c, 255, 0, 0, 255));
    texel(b, sizeof(b), 3, 0, 4, GX_TF_RGB5A3, c);
    EXPECT_TRUE(rgba_is(c, 0xAA, 0x55, 0xCC, (3U << 5) | (3U << 2) | 1U));
    u8 rgba8[64] = {0};
    const size_t n = 12;   /* (2, 1): texel 1 * 4 + 2, dois bytes cada */
    rgba8[n] = 0x11;       /* A */
    rgba8[n + 1U] = 0x22;  /* R */
    rgba8[32U + n] = 0x33; /* G */
    rgba8[33U + n] = 0x44; /* B */
    texel(rgba8, sizeof(rgba8), 2, 1, 4, GX_TF_RGBA8, c);
    EXPECT_TRUE(rgba_is(c, 0x22, 0x33, 0x44, 0x11));
}

TEST(paletted_formats_use_tlut) {
    u8 tlut[32] = {0};
    tlut[6] = 0xF8; /* entrada 3: RGB565 vermelho */
    tlut[7] = 0x00;
    tlut[30] = 0x7F; /* entrada 15: IA8 (alfa 0x7F, intensidade 0x20) */
    tlut[31] = 0x20;
    u8 c4[32] = {0};
    c4[0] = 0x3F; /* (0, 0) = 3, (1, 0) = 15 */
    u8 c[4];
    GxTexSource src = {c4, sizeof(c4), tlut, sizeof(tlut), GX_TLUT_RGB565};
    gx_tex_decode_texel(c, &src, 0, 0, 7, GX_TF_C4);
    EXPECT_TRUE(rgba_is(c, 255, 0, 0, 255));
    src.tlut_format = GX_TLUT_IA8;
    gx_tex_decode_texel(c, &src, 1, 0, 7, GX_TF_C4);
    EXPECT_TRUE(rgba_is(c, 0x20, 0x20, 0x20, 0x7F));
    u8 c14[32] = {0};
    c14[2] = 0xC0; /* (1, 0): 0xC003 & 0x3FFF = 3 */
    c14[3] = 0x03;
    src = (GxTexSource){c14, sizeof(c14), tlut, sizeof(tlut), GX_TLUT_RGB565};
    gx_tex_decode_texel(c, &src, 1, 0, 3, GX_TF_C14X2);
    EXPECT_TRUE(rgba_is(c, 255, 0, 0, 255));
}

TEST(cmpr_both_palette_modes) {
    u8 b[32] = {0}; /* 8x8: quatro sub-blocos de 4x4 */
    /* Sub-bloco 0: c1 = branco > c2 = preto; linha 0 = índices 0, 1, 2, 3. */
    b[0] = 0xFF;
    b[1] = 0xFF;
    b[4] = 0x1B; /* 00 01 10 11 */
    /* Sub-bloco 1 (s 4..7): c1 = preto <= c2 = branco; índices 2 e 3 na linha 1. */
    b[10] = 0xFF;
    b[11] = 0xFF;
    b[13] = 0xAF; /* 10 10 11 11 */
    u8 c[4];
    texel(b, sizeof(b), 0, 0, 8, GX_TF_CMPR, c);
    EXPECT_TRUE(rgba_is(c, 255, 255, 255, 255));
    texel(b, sizeof(b), 1, 0, 8, GX_TF_CMPR, c);
    EXPECT_TRUE(rgba_is(c, 0, 0, 0, 255));
    texel(b, sizeof(b), 2, 0, 8, GX_TF_CMPR, c); /* (c2 * 3 + c1 * 5) / 8 */
    EXPECT_TRUE(rgba_is(c, 159, 159, 159, 255));
    texel(b, sizeof(b), 3, 0, 8, GX_TF_CMPR, c);
    EXPECT_TRUE(rgba_is(c, 95, 95, 95, 255));
    texel(b, sizeof(b), 4, 1, 8, GX_TF_CMPR, c); /* média */
    EXPECT_TRUE(rgba_is(c, 127, 127, 127, 255));
    texel(b, sizeof(b), 6, 1, 8, GX_TF_CMPR, c); /* média transparente */
    EXPECT_TRUE(rgba_is(c, 127, 127, 127, 0));
}

TEST(level_sizes_round_to_blocks) {
    EXPECT_EQ(gx_tex_level_size(GX_TF_I4, 10, 3), 16U * 8U / 2U);
    EXPECT_EQ(gx_tex_level_size(GX_TF_RGBA8, 4, 4), 64U);
    EXPECT_EQ(gx_tex_level_size(GX_TF_CMPR, 640, 448), 640U * 448U / 2U);
}

/* Textura RGBA8 4x4 em 0x80200000: texel (s, t) com R = 16 s, G = 16 t, B = 0, A = 255. */
static void setup_rgba8(u32 wrap_s, u32 wrap_t, bool linear) {
    for (u32 t = 0; t < 4U; t++) {
        for (u32 s = 0; s < 4U; s++) {
            const u32 n = (t * 4U) + s;
            mem_write8(NULL, 0x80200000U + (n * 2U), 0xFF);
            mem_write8(NULL, 0x80200000U + (n * 2U) + 1U, (u8)(16U * s));
            mem_write8(NULL, 0x80200020U + (n * 2U), (u8)(16U * t));
            mem_write8(NULL, 0x80200020U + (n * 2U) + 1U, 0);
        }
    }
    gx_bp_reset();
    (void)gx_bp_write(GX_BP_TX_SETMODE0, wrap_s | (wrap_t << 2) | (linear ? (1U << 4) : 0U));
    (void)gx_bp_write(GX_BP_TX_SETIMAGE0, 3U | (3U << 10) | (GX_TF_RGBA8 << 20));
    (void)gx_bp_write(GX_BP_TX_SETIMAGE3, 0x00200000U >> 5);
    gx_tex_prepare();
}

TEST(sampler_wrap_and_bilinear) {
    u8 c[4];
    setup_rgba8(0, 0, false); /* clamp, nearest */
    gx_tex_sample((2 * 128) + 64, (1 * 128) + 64, 0, false, 0, c);
    EXPECT_TRUE(rgba_is(c, 32, 16, 0, 255));
    gx_tex_sample(-300, 9 * 128, 0, false, 0, c); /* fora: borda */
    EXPECT_TRUE(rgba_is(c, 0, 48, 0, 255));
    setup_rgba8(1, 2, false);                                    /* repeat em s, mirror em t */
    gx_tex_sample((5 * 128) + 1, (5 * 128) + 1, 0, false, 0, c); /* s 5 -> 1; t 5 -> espelho de 1 = 2 */
    EXPECT_TRUE(rgba_is(c, 16, 32, 0, 255));
    setup_rgba8(0, 0, true);
    gx_tex_sample(128, 64, 0, true, 0, c); /* entre os texels 0 e 1 de s, centro de t = 0 */
    EXPECT_TRUE(rgba_is(c, 8, 0, 0, 255));
}

/* Cache de texturas decodificadas: com o estágio 0 usando a unidade 0, as amostras têm de ser as mesmas com e sem
 * cache, inclusive depois de o jogo reescrever a textura (contadores de página) ou trocar a TLUT (hash). */
static void sample_grid(u8 *out) {
    for (s32 i = 0; i < 64; i++) {
        gx_tex_sample((i * 37) - 300, (i * 53) - 200, 0, (i & 1) != 0, 0, out + (i * 4));
    }
}

TEST(decoded_cache_matches_texel_decoding) {
    u8 with_cache[64 * 4];
    u8 without[64 * 4];
    for (u32 pass = 0; pass < 2U; pass++) {
        gx_tex_set_cache(pass == 0U);
        setup_rgba8(1, 2, true);
        (void)gx_bp_write(GX_BP_TREF, 1U << 6); /* estágio 0: textura ligada, unidade 0, coordenada 0 */
        gx_tex_prepare();
        sample_grid(pass == 0U ? with_cache : without);
    }
    EXPECT_EQ(memcmp(with_cache, without, sizeof(without)), 0);
    gx_tex_set_cache(true);
    setup_rgba8(0, 0, false);
    (void)gx_bp_write(GX_BP_TREF, 1U << 6);
    gx_tex_prepare();
    const u64 misses = gx_tex_cache_stats()->misses;
    gx_tex_prepare();
    EXPECT_EQ(gx_tex_cache_stats()->misses, misses); /* nada mudou: acerto */
    mem_write8(NULL, 0x80200001U, 0x77);             /* R do texel (0, 0) */
    gx_tex_prepare();
    EXPECT_EQ(gx_tex_cache_stats()->misses, misses + 1U);
    u8 c[4];
    gx_tex_sample(64, 64, 0, false, 0, c);
    EXPECT_EQ(c[0], 0x77U);
}

int main(void) {
    EXPECT_TRUE(mem_init());
    RUN_TEST(i4_i8_ia4_ia8_blocks);
    RUN_TEST(rgb565_rgb5a3_rgba8);
    RUN_TEST(paletted_formats_use_tlut);
    RUN_TEST(cmpr_both_palette_modes);
    RUN_TEST(level_sizes_round_to_blocks);
    RUN_TEST(sampler_wrap_and_bilinear);
    RUN_TEST(decoded_cache_matches_texel_decoding);
    mem_shutdown();
    return am_test_finish();
}
