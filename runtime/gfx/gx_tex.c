/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Decodificação e amostragem de texturas. Tradução para C de Dolphin TexDecoder_DecodeTexel e
 * TextureSampler::Sample/SampleMip (GPL-2.0-or-later, Dolphin Emulator Project). */
#include "am/gx_tex.h"

#include <stdlib.h>
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_util.h"
#include "am/log.h"
#include "am/mem.h"

#define WRAP_CLAMP  0U
#define WRAP_REPEAT 1U
#define WRAP_MIRROR 2U
#define MIP_NONE    0U
#define MIP_POINT   1U
#define MIP_LINEAR  2U

/* LookUpTables.h: replicação de bits. */
static u8 expand3(u32 v) {
    return (u8)((v << 5) | (v << 2) | (v >> 1));
}

static u8 expand4(u32 v) {
    return (u8)((v << 4) | v);
}

static u8 expand5(u32 v) {
    return (u8)((v << 3) | (v >> 2));
}

static u8 expand6(u32 v) {
    return (u8)((v << 2) | (v >> 4));
}

static u8 rd8(const u8 *p, u32 size, u32 off) {
    return off < size ? p[off] : 0U;
}

static u32 rd16(const u8 *p, u32 size, u32 off) {
    return ((u32)rd8(p, size, off) << 8) | rd8(p, size, off + 1U);
}

static void set_rgba(u8 *out, u32 r, u32 g, u32 b, u32 a) {
    out[0] = (u8)r;
    out[1] = (u8)g;
    out[2] = (u8)b;
    out[3] = (u8)a;
}

static void decode_rgb565(u8 *out, u32 v) {
    set_rgba(out, expand5(v >> 11), expand6((v >> 5) & 63U), expand5(v & 31U), 0xFFU);
}

static void decode_rgb5a3(u8 *out, u32 v) {
    if ((v & 0x8000U) != 0U) {
        set_rgba(out, expand5((v >> 10) & 31U), expand5((v >> 5) & 31U), expand5(v & 31U), 0xFFU);
    } else {
        set_rgba(out, expand4((v >> 8) & 15U), expand4((v >> 4) & 15U), expand4(v & 15U), expand3((v >> 12) & 7U));
    }
}

/* IA8: primeiro byte alfa, segundo intensidade (DecodePixel_IA8 sobre a leitura nativa). */
static void decode_ia8(u8 *out, u32 v) {
    const u32 i = v & 0xFFU;
    set_rgba(out, i, i, i, v >> 8);
}

static void decode_paletted(u8 *out, const GxTexSource *src, u32 index) {
    const u32 v = rd16(src->tlut, src->tlut_size, index * 2U);
    switch (src->tlut_format) {
    case GX_TLUT_IA8:
        decode_ia8(out, v);
        return;
    case GX_TLUT_RGB565:
        decode_rgb565(out, v);
        return;
    case GX_TLUT_RGB5A3:
        decode_rgb5a3(out, v);
        return;
    default:
        set_rgba(out, 0, 0, 0, 0); /* formato 3 de TLUT: Dolphin devolve 0 */
        return;
    }
}

/* Deslocamento do texel num formato de blocos `bw` x `bh` (log2) com `bytes_per_block` bytes por bloco. */
static u32 block_offset(u32 s, u32 t, u32 width_minus_1, u32 bw_log2, u32 bh_log2, u32 bytes_log2) {
    const u32 width_blocks = (width_minus_1 >> bw_log2) + 1U;
    const u32 base = ((t >> bh_log2) * width_blocks + (s >> bw_log2)) << bytes_log2;
    return base;
}

static u32 nibble(const GxTexSource *src, u32 s, u32 t, u32 width_minus_1) {
    const u32 base = block_offset(s, t, width_minus_1, 3U, 3U, 5U);
    const u32 off = ((t & 7U) << 3) + (s & 7U);
    return (u32)(rd8(src->data, src->size, base + (off >> 1)) >> ((off & 1U) != 0U ? 0U : 4U)) & 0xFU;
}

static u32 byte8(const GxTexSource *src, u32 s, u32 t, u32 width_minus_1) {
    const u32 base = block_offset(s, t, width_minus_1, 3U, 2U, 5U);
    return rd8(src->data, src->size, base + ((t & 3U) << 3) + (s & 7U));
}

static u32 offset16(u32 s, u32 t, u32 width_minus_1) {
    return block_offset(s, t, width_minus_1, 2U, 2U, 5U) + ((((t & 3U) << 2) + (s & 3U)) << 1);
}

/* DXTBlend: 3/8, perto de 1/3. */
static u32 dxt_blend(u32 v1, u32 v2) {
    return ((v1 * 3U) + (v2 * 5U)) >> 3;
}

static void decode_cmpr(u8 *out, const GxTexSource *src, u32 s, u32 t, u32 width_minus_1) {
    const u32 sd = s >> 2;
    const u32 td = t >> 2;
    const u32 base = ((td >> 1) * ((width_minus_1 >> 3) + 1U) + (sd >> 1)) << 2;
    const u32 off = (base + ((td & 1U) << 1) + (sd & 1U)) << 3;
    const u32 c1 = rd16(src->data, src->size, off);
    const u32 c2 = rd16(src->data, src->size, off + 2U);
    const u32 r1 = expand5(c1 >> 11);
    const u32 g1 = expand6((c1 >> 5) & 63U);
    const u32 b1 = expand5(c1 & 31U);
    const u32 r2 = expand5(c2 >> 11);
    const u32 g2 = expand6((c2 >> 5) & 63U);
    const u32 b2 = expand5(c2 & 31U);
    const u32 line = rd8(src->data, src->size, off + 4U + (t & 3U));
    u32 sel = (line >> (6U - ((s & 3U) << 1))) & 3U;
    sel |= c1 > c2 ? 0U : 4U;
    switch (sel) {
    case 0U:
    case 4U:
        set_rgba(out, r1, g1, b1, 0xFFU);
        return;
    case 1U:
    case 5U:
        set_rgba(out, r2, g2, b2, 0xFFU);
        return;
    case 2U:
        set_rgba(out, dxt_blend(r2, r1), dxt_blend(g2, g1), dxt_blend(b2, b1), 0xFFU);
        return;
    case 3U:
        set_rgba(out, dxt_blend(r1, r2), dxt_blend(g1, g2), dxt_blend(b1, b2), 0xFFU);
        return;
    case 6U:
        set_rgba(out, (r1 + r2) / 2U, (g1 + g2) / 2U, (b1 + b2) / 2U, 0xFFU);
        return;
    default: /* 7: média transparente (diferente do DXT1, que dá preto) */
        set_rgba(out, (r1 + r2) / 2U, (g1 + g2) / 2U, (b1 + b2) / 2U, 0U);
        return;
    }
}

void gx_tex_decode_texel(u8 *rgba, const GxTexSource *src, u32 s, u32 t, u32 width_minus_1, u32 format) {
    switch (format) {
    case GX_TF_I4: {
        const u8 v = expand4(nibble(src, s, t, width_minus_1));
        set_rgba(rgba, v, v, v, v);
        return;
    }
    case GX_TF_C4:
        decode_paletted(rgba, src, nibble(src, s, t, width_minus_1));
        return;
    case GX_TF_I8: {
        const u32 v = byte8(src, s, t, width_minus_1);
        set_rgba(rgba, v, v, v, v);
        return;
    }
    case GX_TF_C8:
        decode_paletted(rgba, src, byte8(src, s, t, width_minus_1));
        return;
    case GX_TF_IA4: {
        const u32 v = byte8(src, s, t, width_minus_1);
        const u8 l = expand4(v & 15U);
        set_rgba(rgba, l, l, l, expand4(v >> 4));
        return;
    }
    case GX_TF_IA8:
        decode_ia8(rgba, rd16(src->data, src->size, offset16(s, t, width_minus_1)));
        return;
    case GX_TF_C14X2:
        decode_paletted(rgba, src, rd16(src->data, src->size, offset16(s, t, width_minus_1)) & 0x3FFFU);
        return;
    case GX_TF_RGB565:
        decode_rgb565(rgba, rd16(src->data, src->size, offset16(s, t, width_minus_1)));
        return;
    case GX_TF_RGB5A3:
        decode_rgb5a3(rgba, rd16(src->data, src->size, offset16(s, t, width_minus_1)));
        return;
    case GX_TF_RGBA8: {
        /* Bloco de 64 bytes: 32 de AR e 32 de GB ("shift by 5 is correct" no Dolphin: base em unidades de 32). */
        const u32 base = block_offset(s, t, width_minus_1, 2U, 2U, 5U);
        const u32 off = (base + (((t & 3U) << 2) + (s & 3U))) << 1;
        set_rgba(rgba, rd8(src->data, src->size, off + 1U), rd8(src->data, src->size, off + 32U),
                 rd8(src->data, src->size, off + 33U), rd8(src->data, src->size, off));
        return;
    }
    case GX_TF_CMPR:
        decode_cmpr(rgba, src, s, t, width_minus_1);
        return;
    default:
        am_fatal("GX: formato de textura 0x%X inválido", format);
    }
}

typedef struct FormatInfo {
    u8 block_w;
    u8 block_h;
    u8 nibbles; /* tamanho do texel em nibbles */
} FormatInfo;

static FormatInfo format_info(u32 format) {
    switch (format) {
    case GX_TF_I4:
    case GX_TF_C4:
    case GX_TF_CMPR:
        return (FormatInfo){8, 8, 1};
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
        return (FormatInfo){8, 4, 2};
    case GX_TF_RGBA8:
        return (FormatInfo){4, 4, 8};
    default:
        return (FormatInfo){4, 4, 4};
    }
}

u32 gx_tex_level_size(u32 format, u32 width, u32 height) {
    const FormatInfo f = format_info(format);
    const u32 w = (width + f.block_w - 1U) / f.block_w * f.block_w;
    const u32 h = (height + f.block_h - 1U) / f.block_h * f.block_h;
    return (w * h * f.nibbles) >> 1;
}

static s32 wrap(s32 coord, u32 mode, s32 size) {
    switch (mode) {
    case WRAP_REPEAT:
        return coord & (size - 1); /* tamanho potência de 2 (YAGCD TX_SETMODE1) */
    case WRAP_MIRROR:
        if ((coord & size) != 0) {
            coord = ~coord;
        }
        return coord & (size - 1);
    default: /* clamp; o modo 3 se comporta como clamp no hardware */
        return gx_clamp_s32(coord, 0, size - 1);
    }
}

/* Unidade de textura decodificada dos registradores de BP para uma amostra. */
typedef struct TexUnit {
    u32 mode0;
    u32 width_minus_1;
    u32 height_minus_1;
    u32 format;
    bool tmem;       /* cache_manually_managed: dados já na TMEM */
    GxTexSource src; /* nível 0 */
    const u8 *odd;   /* RGBA8 na TMEM: banco GB */
    /* Níveis já decodificados em RGBA (cache abaixo); NULL = decodificar texel a texel, como o Dolphin. */
    const u8 *cached[GX_TEX_CACHE_LEVELS];
    u32 cached_levels;
} TexUnit;

static TexUnit tex_unit(u32 texmap) {
    TexUnit u;
    memset(&u, 0, sizeof(u));
    u.mode0 = gx_bp_tex_reg(GX_BP_TX_SETMODE0, texmap);
    const u32 img0 = gx_bp_tex_reg(GX_BP_TX_SETIMAGE0, texmap);
    const u32 img1 = gx_bp_tex_reg(GX_BP_TX_SETIMAGE1, texmap);
    const u32 tlut = gx_bp_tex_reg(GX_BP_TX_SETTLUT, texmap);
    u.width_minus_1 = img0 & 0x3FFU;
    u.height_minus_1 = (img0 >> 10) & 0x3FFU;
    u.format = (img0 >> 20) & 0xFU;
    u.tmem = ((img1 >> 21) & 1U) != 0U;
    if (u.tmem) {
        const u32 even = (img1 & 0x7FFFU) * GX_TMEM_LINE;
        u.src.size = even < GX_TMEM_SIZE ? GX_TMEM_SIZE - even : 0U;
        u.src.data = gx_bp_tmem(even, u.src.size);
        const u32 odd = (gx_bp_tex_reg(GX_BP_TX_SETIMAGE2, texmap) & 0x7FFFU) * GX_TMEM_LINE;
        u.odd = gx_bp_tmem(odd, odd < GX_TMEM_SIZE ? GX_TMEM_SIZE - odd : 0U);
    } else {
        const u32 addr = (gx_bp_tex_reg(GX_BP_TX_SETIMAGE3, texmap) << 5) & 0x01FFFFFFU;
        u.src.size = addr < MEM_MEM1_SIZE ? MEM_MEM1_SIZE - addr : 0U;
        u.src.data = u.src.size != 0U ? mem_host_ptr(MEM_CACHED_BASE | addr, u.src.size) : NULL;
        if (u.src.data == NULL) {
            u.src.size = 0; /* fora da MEM1: o Dolphin lê zeros (span vazio) */
        }
    }
    const u32 tlut_off = (tlut & 0x3FFU) << 9;
    u.src.tlut_size = tlut_off < GX_TMEM_SIZE ? GX_TMEM_SIZE - tlut_off : 0U;
    u.src.tlut = gx_bp_tmem(tlut_off, u.src.tlut_size);
    u.src.tlut_format = (tlut >> 10) & 3U;
    return u;
}

static void texel(const TexUnit *u, const GxTexSource *src, s32 s, s32 t, u32 width_minus_1, u8 *out) {
    if (u->format == GX_TF_RGBA8 && u->tmem) { /* TexDecoder_DecodeTexelRGBA8FromTmem */
        const u32 base = block_offset((u32)s, (u32)t, width_minus_1, 2U, 2U, 4U);
        const u32 off = (base + (((u32)t & 3U) << 2) + ((u32)s & 3U)) << 1;
        const u32 odd_size = u->odd != NULL ? GX_TMEM_SIZE : 0U;
        set_rgba(out, rd8(src->data, src->size, off + 1U), rd8(u->odd, odd_size, off), rd8(u->odd, odd_size, off + 1U),
                 rd8(src->data, src->size, off));
        return;
    }
    gx_tex_decode_texel(out, src, (u32)s, (u32)t, width_minus_1, u->format);
}

/* Fonte e tamanho do nível `mip` (TextureSampler::SampleMip: os níveis seguem o nível 0, cada um arredondado aos
 * blocos do formato; se passar do fim, lê zeros). */
static void level_source(const TexUnit *u, u32 mip, GxTexSource *src, u32 *wm1, u32 *hm1) {
    *src = u->src;
    *wm1 = u->width_minus_1;
    *hm1 = u->height_minus_1;
    if (mip == 0U) {
        return;
    }
    const FormatInfo f = format_info(u->format);
    u32 mw = *wm1 + 1U;
    u32 mh = *hm1 + 1U;
    *wm1 >>= mip;
    *hm1 >>= mip;
    for (u32 level = mip; level > 0U; level--) {
        mw = mw > f.block_w ? mw : f.block_w;
        mh = mh > f.block_h ? mh : f.block_h;
        const u32 size = (mw * mh * f.nibbles) >> 1;
        src->data = size < src->size ? src->data + size : src->data;
        src->size = size < src->size ? src->size - size : 0U;
        mw >>= 1;
        mh >>= 1;
    }
}

static void fetch(const TexUnit *u, const u8 *cached, const GxTexSource *src, s32 s, s32 t, u32 wm1, u8 *out) {
    if (cached != NULL) {
        memcpy(out, cached + ((((size_t)t * (wm1 + 1U)) + (size_t)s) * 4U), 4U);
        return;
    }
    texel(u, src, s, t, wm1, out);
}

static void sample_mip(const TexUnit *u, s32 s, s32 t, u32 mip, bool linear, u8 *out) {
    GxTexSource src;
    u32 wm1;
    u32 hm1;
    level_source(u, mip, &src, &wm1, &hm1);
    const u8 *cached = mip < u->cached_levels ? u->cached[mip] : NULL;
    const u32 wrap_s = u->mode0 & 3U;
    const u32 wrap_t = (u->mode0 >> 2) & 3U;
    const s32 w = (s32)wm1 + 1;
    const s32 h = (s32)hm1 + 1;
    if (!linear) {
        fetch(u, cached, &src, wrap(s >> 7, wrap_s, w), wrap(t >> 7, wrap_t, h), wm1, out);
        return;
    }
    s -= 64;
    t -= 64;
    const s32 s0 = wrap(s >> 7, wrap_s, w);
    const s32 t0 = wrap(t >> 7, wrap_t, h);
    const s32 s1 = wrap((s >> 7) + 1, wrap_s, w);
    const s32 t1 = wrap((t >> 7) + 1, wrap_t, h);
    const u32 fs = (u32)s & 0x7FU;
    const u32 ft = (u32)t & 0x7FU;
    u8 c[4][4];
    fetch(u, cached, &src, s0, t0, wm1, c[0]);
    fetch(u, cached, &src, s1, t0, wm1, c[1]);
    fetch(u, cached, &src, s0, t1, wm1, c[2]);
    fetch(u, cached, &src, s1, t1, wm1, c[3]);
    for (u32 i = 0; i < 4U; i++) {
        const u32 v = ((u32)c[0][i] * (128U - fs) * (128U - ft)) + ((u32)c[1][i] * fs * (128U - ft)) +
                      ((u32)c[2][i] * (128U - fs) * ft) + ((u32)c[3][i] * fs * ft);
        out[i] = (u8)(v >> 14);
    }
}

/* Cache de texturas decodificadas (M7, desempenho): cada nível usado por um desenho é decodificado inteiro para RGBA
 * uma vez e reaproveitado enquanto a memória dele não for escrita (contadores por página de am/mem.h) e a TLUT for a
 * mesma (hash do conteúdo). Os texels são os mesmos de gx_tex_decode_texel, então a imagem não muda. Texturas na TMEM
 * (pré-carregadas) e grandes demais ficam fora. */
#define TEX_CACHE_ENTRIES    96U
#define TEX_CACHE_MAX_TEXELS (1024U * 1024U)

typedef struct TexCacheEntry {
    bool used;
    u32 addr;
    u32 bytes;
    u32 format;
    u32 wm1;
    u32 hm1;
    u32 levels;
    u32 tlut_format;
    u64 tlut_hash;
    u64 written;
    u64 last_use;
    u8 *rgba;
    u32 offset[GX_TEX_CACHE_LEVELS];
} TexCacheEntry;

static struct {
    TexUnit units[GX_TEXMAP_MAX];
    TexCacheEntry cache[TEX_CACHE_ENTRIES];
    u64 clock;
    bool cache_on;
    GxTexCacheStats stats;
} s_tex = {.cache_on = true};

void gx_tex_set_cache(bool on) {
    gx_tex_cache_clear();
    s_tex.cache_on = on;
}

void gx_tex_cache_clear(void) {
    for (u32 i = 0; i < TEX_CACHE_ENTRIES; i++) {
        free(s_tex.cache[i].rgba);
    }
    memset(s_tex.cache, 0, sizeof(s_tex.cache));
}

const GxTexCacheStats *gx_tex_cache_stats(void) {
    return &s_tex.stats;
}

static u64 fnv(const u8 *p, u32 size) {
    u64 h = 1469598103934665603ULL;
    for (u32 i = 0; i < size; i++) {
        h = (h ^ (p != NULL ? p[i] : 0U)) * 1099511628211ULL;
    }
    return h;
}

static bool paletted(u32 format) {
    return format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2;
}

static u32 tlut_bytes(u32 format) {
    if (format == GX_TF_C4) {
        return 32U;
    }
    return format == GX_TF_C8 ? 512U : 32768U;
}

/* Decodifica `levels` níveis numa entrada (já com a chave preenchida). */
static bool cache_fill(TexCacheEntry *e, const TexUnit *u) {
    u32 total = 0;
    for (u32 l = 0; l < e->levels; l++) {
        e->offset[l] = total;
        total += (((u->width_minus_1 >> l) + 1U) * ((u->height_minus_1 >> l) + 1U)) * 4U;
    }
    u8 *buf = total != 0U ? realloc(e->rgba, total) : NULL;
    if (buf == NULL) {
        return false;
    }
    e->rgba = buf;
    for (u32 l = 0; l < e->levels; l++) {
        GxTexSource src;
        u32 wm1;
        u32 hm1;
        level_source(u, l, &src, &wm1, &hm1);
        u8 *out = buf + e->offset[l];
        for (u32 t = 0; t <= hm1; t++) {
            for (u32 s = 0; s <= wm1; s++) {
                gx_tex_decode_texel(out, &src, s, t, wm1, u->format);
                out += 4;
            }
        }
        s_tex.stats.decoded_texels += (u64)(wm1 + 1U) * (hm1 + 1U);
    }
    return true;
}

static void cache_attach(TexUnit *u, const TexCacheEntry *e) {
    u->cached_levels = e->levels;
    for (u32 l = 0; l < e->levels; l++) {
        u->cached[l] = e->rgba + e->offset[l];
    }
}

/* Níveis que um desenho pode amostrar: só o 0 sem mipmap; senão até o LOD máximo (+1 do filtro linear). */
static u32 levels_needed(u32 texmap, const TexUnit *u) {
    if (((u->mode0 >> 5) & 3U) == MIP_NONE) {
        return 1U;
    }
    const u32 max_lod = (gx_bp_tex_reg(GX_BP_TX_SETMODE1, texmap) >> 8) & 0xFFU;
    const u32 n = (max_lod >> 4) + 2U;
    return n > GX_TEX_CACHE_LEVELS ? GX_TEX_CACHE_LEVELS : n;
}

static void cache_lookup(u32 texmap, TexUnit *u) {
    const u32 levels = levels_needed(texmap, u);
    const u32 addr = (gx_bp_tex_reg(GX_BP_TX_SETIMAGE3, texmap) << 5) & 0x01FFFFFFU;
    GxTexSource last;
    u32 lw;
    u32 lh;
    level_source(u, levels - 1U, &last, &lw, &lh);
    const u32 bytes = (u32)(last.data - u->src.data) + gx_tex_level_size(u->format, lw + 1U, lh + 1U);
    const u64 tlut_hash =
        paletted(u->format)
            ? fnv(u->src.tlut, (u->src.tlut_size < tlut_bytes(u->format) ? u->src.tlut_size : tlut_bytes(u->format)))
            : 0U;
    const u64 written = mem_written_sum(addr, bytes);
    TexCacheEntry *victim = &s_tex.cache[0];
    for (u32 i = 0; i < TEX_CACHE_ENTRIES; i++) {
        TexCacheEntry *e = &s_tex.cache[i];
        if (e->used && e->addr == addr && e->format == u->format && e->wm1 == u->width_minus_1 &&
            e->hm1 == u->height_minus_1 && e->levels >= levels && e->tlut_hash == tlut_hash &&
            e->tlut_format == u->src.tlut_format && e->written == written) {
            e->last_use = ++s_tex.clock;
            s_tex.stats.hits++;
            cache_attach(u, e);
            return;
        }
        if (victim->used && (!e->used || e->last_use < victim->last_use)) {
            victim = e; /* a primeira livre, senão a usada há mais tempo */
        }
    }
    *victim = (TexCacheEntry){.used = true,
                              .addr = addr,
                              .bytes = bytes,
                              .format = u->format,
                              .wm1 = u->width_minus_1,
                              .hm1 = u->height_minus_1,
                              .levels = levels,
                              .tlut_format = u->src.tlut_format,
                              .tlut_hash = tlut_hash,
                              .written = written,
                              .last_use = ++s_tex.clock,
                              .rgba = victim->rgba};
    s_tex.stats.misses++;
    if (cache_fill(victim, u)) {
        cache_attach(u, victim);
    } else {
        victim->used = false;
    }
}

/* Unidades que os estágios (normais e indiretos) deste desenho usam. */
static u32 used_texmaps(void) {
    const u32 gen = gx_bp_reg(GX_BP_GENMODE);
    const u32 stages = ((gen >> 10) & 15U) + 1U;
    u32 mask = 0;
    for (u32 i = 0; i < stages; i++) {
        const u32 order = gx_bp_reg((u8)(GX_BP_TREF + (i >> 1)));
        const u32 o = (i & 1U) != 0U ? order >> 12 : order;
        mask |= ((o >> 6) & 1U) != 0U ? 1U << (o & 7U) : 0U;
    }
    const u32 ind = (gen >> 16) & 7U;
    const u32 iref = gx_bp_reg(GX_BP_IREF);
    for (u32 i = 0; i < ind && i < 4U; i++) {
        mask |= 1U << ((iref >> (6U * i)) & 7U);
    }
    return mask;
}

void gx_tex_prepare(void) {
    const u32 used = s_tex.cache_on ? used_texmaps() : 0U;
    for (u32 i = 0; i < GX_TEXMAP_MAX; i++) {
        TexUnit *u = &s_tex.units[i];
        *u = tex_unit(i);
        if ((used & (1U << i)) != 0U && !u->tmem && u->src.data != NULL &&
            (u->width_minus_1 + 1U) * (u->height_minus_1 + 1U) <= TEX_CACHE_MAX_TEXELS) {
            cache_lookup(i, u);
        }
    }
}

void gx_tex_sample(s32 s, s32 t, s32 lod, bool linear, u32 texmap, u8 *rgba) {
    AM_ASSERT(texmap < GX_TEXMAP_MAX, "texmap %u", texmap);
    const TexUnit *u = &s_tex.units[texmap];
    const u32 mip_filter = (u->mode0 >> 5) & 3U;
    const s32 frac = lod & 0xF;
    u32 base = 0;
    bool mip_linear = false;
    if (lod > 0 && mip_filter != MIP_NONE) {
        base = (u32)(lod >> 4);
        mip_linear = frac != 0 && mip_filter == MIP_LINEAR;
        if (mip_filter == MIP_POINT && frac >= 8) {
            base++;
        }
    }
    if (!mip_linear) {
        sample_mip(u, s, t, base, linear, rgba);
        return;
    }
    u8 a[4];
    u8 b[4];
    sample_mip(u, s, t, base, linear, a);
    sample_mip(u, s, t, base + 1U, linear, b);
    for (u32 i = 0; i < 4U; i++) {
        rgba[i] = (u8)((((u32)a[i] * (u32)(16 - frac)) + ((u32)b[i] * (u32)frac)) >> 4);
    }
}
