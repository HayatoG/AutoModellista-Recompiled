/* SPDX-License-Identifier: GPL-3.0-or-later */
/* EFB, Pixel Engine e cópias. Teste de Z, blend, operação lógica e dither traduzidos de Dolphin
 * VideoBackends/Software/SWEfbInterface.cpp; cópia para XFB/textura pelo caminho de hardware do Dolphin
 * (VideoCommon/TextureConversionShader.cpp) e layout de blocos de TextureEncoder.cpp (GPL-2.0-or-later, Dolphin
 * Emulator Project). */
#include "am/gx_efb.h"

#include <math.h>
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_raster.h"
#include "am/gx_util.h"
#include "am/log.h"
#include "am/mem.h"

#define EFB_PIXELS (GX_EFB_WIDTH * GX_EFB_HEIGHT)

/* Bits do registrador de cópia (UPE_Copy, BPMemory.h). */
#define COPY_CLAMP_TOP    0x00001U
#define COPY_CLAMP_BOTTOM 0x00002U
#define COPY_HALF_SCALE   0x00200U
#define COPY_SCALE_INVERT 0x00400U
#define COPY_CLEAR        0x00800U
#define COPY_TO_XFB       0x04000U
#define COPY_INTENSITY    0x08000U
#define COPY_AUTO_CONV    0x10000U

/* EFBCopyFormat (TextureDecoder.h). */
#define CF_R4     0U
#define CF_R8_1   1U
#define CF_RA4    2U
#define CF_RA8    3U
#define CF_RGB565 4U
#define CF_RGB5A3 5U
#define CF_RGBA8  6U
#define CF_A8     7U
#define CF_R8     8U
#define CF_G8     9U
#define CF_B8     10U
#define CF_RG8    11U
#define CF_GB8    12U

typedef struct DrawState {
    u32 format;
    bool z_test;
    u32 z_func;
    bool z_update;
    u32 blend; /* BlendMode */
    u32 dst_alpha;
} DrawState;

/* Parâmetros de amostragem de uma cópia (filtro vertical, gama, faixa de linhas). */
typedef struct CopyParams {
    u32 left, top, right, bottom;
    u32 coef[3];
    bool overflow;
    f32 gamma_rcp;
    u32 clamp_top, clamp_bottom; /* linhas extremas que o filtro pode ler */
    bool depth;
} CopyParams;

static struct {
    u32 color[EFB_PIXELS];
    u32 depth[EFB_PIXELS];
    DrawState draw;
    GxEfbStats stats;
} s_efb;

void gx_efb_reset(void) {
    memset(&s_efb, 0, sizeof(s_efb));
}

const GxEfbStats *gx_efb_stats(void) {
    return &s_efb.stats;
}

u32 *gx_efb_color_buffer(void) {
    return s_efb.color;
}

u32 *gx_efb_depth_buffer(void) {
    return s_efb.depth;
}

static u32 pixel_format(void) {
    return gx_bp_reg(GX_BP_ZCOMPARE) & 7U;
}

void gx_efb_begin_draw(void) {
    const u32 z = gx_bp_reg(GX_BP_ZMODE);
    s_efb.draw = (DrawState){.format = pixel_format(),
                             .z_test = (z & 1U) != 0U,
                             .z_func = (z >> 1) & 7U,
                             .z_update = ((z >> 4) & 1U) != 0U,
                             .blend = gx_bp_reg(GX_BP_BLENDMODE),
                             .dst_alpha = gx_bp_reg(GX_BP_CONSTANTALPHA)};
}

static u8 expand6(u32 v) {
    return (u8)((v << 2) | (v >> 4));
}

/* GetPixelColor: RGBA. RGB565 e Z24 guardam como RGB8 (o Dolphin SW faz o mesmo). */
static void unpack(u32 format, u32 v, u8 *rgba) {
    if (format == GX_PIXEL_RGBA6_Z24) {
        rgba[0] = expand6((v >> 18) & 63U);
        rgba[1] = expand6((v >> 12) & 63U);
        rgba[2] = expand6((v >> 6) & 63U);
        rgba[3] = expand6(v & 63U);
        return;
    }
    rgba[0] = (u8)(v >> 16);
    rgba[1] = (u8)(v >> 8);
    rgba[2] = (u8)v;
    rgba[3] = 0xFF;
}

/* SetPixelAlphaColor / SetPixelColorOnly / SetPixelAlphaOnly. */
static void store(u32 format, u32 index, const u8 *rgba, bool color, bool alpha) {
    u32 v = s_efb.color[index];
    if (format == GX_PIXEL_RGBA6_Z24) {
        if (color) {
            v = (v & 0x3FU) | ((u32)(rgba[0] >> 2) << 18) | ((u32)(rgba[1] >> 2) << 12) | ((u32)(rgba[2] >> 2) << 6);
        }
        if (alpha) {
            v = (v & ~0x3FU) | (u32)(rgba[3] >> 2);
        }
    } else if (color) {
        v = ((u32)rgba[0] << 16) | ((u32)rgba[1] << 8) | rgba[2];
    }
    s_efb.color[index] = v & 0xFFFFFFU;
}

void gx_efb_peek(u32 x, u32 y, u8 *rgba, u32 *z) {
    AM_ASSERT(x < GX_EFB_WIDTH && y < GX_EFB_HEIGHT, "EFB (%u, %u)", x, y);
    const u32 i = (y * GX_EFB_WIDTH) + x;
    unpack(pixel_format(), s_efb.color[i], rgba);
    *z = s_efb.depth[i];
}

bool gx_efb_z_test(u32 x, u32 y, u32 z) {
    const u32 i = (y * GX_EFB_WIDTH) + x;
    const u32 d = s_efb.depth[i];
    bool pass;
    switch (s_efb.draw.z_func) {
    case 0U:
        pass = false;
        break;
    case 1U:
        pass = z < d;
        break;
    case 2U:
        pass = z == d;
        break;
    case 3U:
        pass = z <= d;
        break;
    case 4U:
        pass = z > d;
        break;
    case 5U:
        pass = z != d;
        break;
    case 6U:
        pass = z >= d;
        break;
    default:
        pass = true;
        break;
    }
    if (pass && s_efb.draw.z_update) {
        s_efb.depth[i] = z & 0xFFFFFFU;
    }
    return pass;
}

static u32 blend_factor(u32 mode, bool src_side, const u8 *src, const u8 *dst, u32 channel) {
    switch (mode) {
    case 0U:
        return 0U;
    case 1U:
        return 255U;
    case 2U: /* fonte: cor do destino; destino: cor da fonte */
        return src_side ? dst[channel] : src[channel];
    case 3U:
        return 255U - (src_side ? dst[channel] : src[channel]);
    case 4U:
        return src[3];
    case 5U:
        return 255U - src[3];
    case 6U:
        return dst[3];
    default:
        return 255U - dst[3];
    }
}

static u32 logic_op(u32 op, u32 s, u32 d) {
    switch (op) {
    case 0U:
        return 0U;
    case 1U:
        return s & d;
    case 2U:
        return s & ~d;
    case 3U:
        return s;
    case 4U:
        return ~s & d;
    case 5U:
        return d;
    case 6U:
        return s ^ d;
    case 7U:
        return s | d;
    case 8U:
        return ~(s | d);
    case 9U:
        return ~(s ^ d);
    case 10U:
        return ~d;
    case 11U:
        return s | ~d;
    case 12U:
        return ~s;
    case 13U:
        return ~s | d;
    case 14U:
        return ~(s & d);
    default:
        return 0xFFFFFFFFU;
    }
}

static void blend_pixel(u32 mode, const u8 *src, u8 *dst) {
    const u32 df_mode = (mode >> 5) & 7U;
    const u32 sf_mode = (mode >> 8) & 7U;
    u8 out[4];
    for (u32 i = 0; i < 4U; i++) {
        u32 sf = blend_factor(sf_mode, true, src, dst, i);
        u32 df = blend_factor(df_mode, false, src, dst, i);
        sf += sf >> 7;
        df += df >> 7;
        const u32 c = (((u32)src[i] * sf) + ((u32)dst[i] * df)) >> 8;
        out[i] = (u8)(c > 255U ? 255U : c);
    }
    memcpy(dst, out, sizeof(out));
}

void gx_efb_blend(u32 x, u32 y, const u8 *rgba) {
    const DrawState *st = &s_efb.draw;
    const u32 i = (y * GX_EFB_WIDTH) + x;
    const u32 mode = st->blend;
    u8 dst[4];
    unpack(st->format, s_efb.color[i], dst);
    if ((mode & 1U) != 0U) {
        if (((mode >> 11) & 1U) != 0U) {
            for (u32 c = 0; c < 4U; c++) {
                dst[c] = (u8)(dst[c] > rgba[c] ? dst[c] - rgba[c] : 0U);
            }
        } else {
            blend_pixel(mode, rgba, dst);
        }
    } else if (((mode >> 1) & 1U) != 0U) {
        for (u32 c = 0; c < 4U; c++) {
            dst[c] = (u8)logic_op((mode >> 12) & 15U, rgba[c], dst[c]);
        }
    } else {
        memcpy(dst, rgba, sizeof(dst));
    }
    if (((st->dst_alpha >> 8) & 1U) != 0U) {
        dst[3] = (u8)st->dst_alpha;
    }
    const bool color = ((mode >> 3) & 1U) != 0U;
    const bool alpha = ((mode >> 4) & 1U) != 0U;
    if (color && ((mode >> 2) & 1U) != 0U && st->format == GX_PIXEL_RGBA6_Z24) {
        static const u8 s_dither[2][2] = {{0, 2}, {3, 1}}; /* Bayer 2x2 do Flipper */
        for (u32 c = 0; c < 3U; c++) {
            dst[c] = (u8)(((dst[c] - (dst[c] >> 6)) + s_dither[y & 1U][x & 1U]) & 0xFCU);
        }
    }
    if (color || alpha) {
        store(st->format, i, dst, color, alpha);
    }
}

/* Linha de origem de uma cópia: canal RGB (ou Z) filtrado, como SampleEFB do shader de codificação. */
static void efb_sample_raw(const CopyParams *p, u32 x, u32 y, u32 *out) {
    const u32 i = (y * GX_EFB_WIDTH) + x;
    if (p->depth) {
        const u32 z = s_efb.depth[i];
        out[0] = (z >> 16) & 0xFFU;
        out[1] = (z >> 8) & 0xFFU;
        out[2] = z & 0xFFU;
        out[3] = 0xFFU;
        return;
    }
    u8 c[4];
    unpack(pixel_format(), s_efb.color[i], c);
    for (u32 k = 0; k < 4U; k++) {
        out[k] = c[k];
    }
}

static void efb_sample(const CopyParams *p, s32 x, s32 y, u32 *out) {
    const u32 cx = (u32)gx_clamp_s32(x, 0, (s32)GX_EFB_WIDTH - 1);
    const s32 lo = (s32)p->clamp_top;
    const s32 hi = (s32)p->clamp_bottom;
    const u32 cur = (u32)gx_clamp_s32(y, lo, hi);
    const u32 prev = cur > p->clamp_top ? cur - 1U : p->clamp_top;
    const u32 next = cur < p->clamp_bottom ? cur + 1U : p->clamp_bottom;
    u32 a[4];
    u32 b[4];
    u32 c[4];
    efb_sample_raw(p, cx, cur, b);
    efb_sample_raw(p, cx, prev, a);
    efb_sample_raw(p, cx, next, c);
    for (u32 k = 0; k < 3U; k++) {
        u32 v = ((a[k] * p->coef[0]) + (b[k] * p->coef[1]) + (c[k] * p->coef[2])) >> 6;
        if (p->overflow) {
            v &= 0x1FFU;
        }
        v = v > 255U ? 255U : v;
        if (p->gamma_rcp != 1.0F) {
            v = (u32)roundf(powf((f32)v / 255.0F, p->gamma_rcp) * 255.0F);
        }
        out[k] = v;
    }
    out[3] = b[3];
}

static CopyParams copy_params(u32 copy) {
    CopyParams p;
    memset(&p, 0, sizeof(p));
    const u32 tl = gx_bp_reg(GX_BP_EFB_TL);
    const u32 wh = gx_bp_reg(GX_BP_EFB_WH);
    p.left = tl & 0x3FFU;
    p.top = (tl >> 10) & 0x3FFU;
    p.right = p.left + (wh & 0x3FFU) + 1U;
    p.bottom = p.top + ((wh >> 10) & 0x3FFU) + 1U;
    const u32 f0 = gx_bp_reg(GX_BP_COPYFILTER0);
    const u32 f1 = gx_bp_reg(GX_BP_COPYFILTER1);
    /* GetCoefficients + agrupamento do caminho de hardware: linha de cima w0+w1, atual w2+w3+w4, de baixo w5+w6. */
    p.coef[0] = (f0 & 63U) + ((f0 >> 6) & 63U);
    p.coef[1] = ((f0 >> 12) & 63U) + ((f0 >> 18) & 63U) + (f1 & 63U);
    p.coef[2] = ((f1 >> 6) & 63U) + ((f1 >> 12) & 63U);
    p.overflow = p.coef[0] + p.coef[1] + p.coef[2] >= 128U;
    static const f32 s_gamma[4] = {1.0F, 1.7F, 2.2F, 1.0F}; /* BPStructs.cpp s_gammaLUT */
    p.gamma_rcp = 1.0F / s_gamma[(copy >> 7) & 3U];
    p.depth = pixel_format() == GX_PIXEL_Z24;
    return p;
}

static void clamp_rows(CopyParams *p, u32 copy) {
    p->clamp_top = (copy & COPY_CLAMP_TOP) != 0U ? p->top : 0U;
    p->clamp_bottom = ((copy & COPY_CLAMP_BOTTOM) != 0U ? p->bottom : GX_EFB_HEIGHT) - 1U;
}

/* unorm8 de um valor do shader (round() do MSL: metade para longe de zero; conversão para unorm: mais próximo). */
static u8 yuv_byte(f32 dotval) {
    const f32 v = roundf(dotval) / 256.0F;
    const f32 b = roundf(v * 255.0F);
    return (u8)gx_clamp_f32(b, 0.0F, 255.0F);
}

/* WriteXFBEncoder: dois pixels -> Y0 U Y1 V. */
static void encode_xfb_pair(const u32 *c0, const u32 *c1, u8 *out) {
    f32 a[3];
    f32 b[3];
    f32 avg[3];
    for (u32 k = 0; k < 3U; k++) {
        a[k] = (f32)c0[k] / 255.0F;
        b[k] = (f32)c1[k] / 255.0F;
        avg[k] = (a[k] + b[k]) * 0.5F;
    }
    out[0] = yuv_byte((66.0F * a[0]) + (129.0F * a[1]) + (25.0F * a[2]) + 16.0F);
    out[1] = yuv_byte((-38.0F * avg[0]) + (-74.0F * avg[1]) + (112.0F * avg[2]) + 128.0F);
    out[2] = yuv_byte((66.0F * b[0]) + (129.0F * b[1]) + (25.0F * b[2]) + 16.0F);
    out[3] = yuv_byte((112.0F * avg[0]) + (-94.0F * avg[1]) + (-18.0F * avg[2]) + 128.0F);
}

typedef struct XfbJob {
    const CopyParams *p;
    u8 *host; /* destino na MEM1 (NULL: escrita pelo barramento, em sequência) */
    u32 dest, stride, width, height;
    f32 yscale;
} XfbJob;

/* Uma linha do XFB: cada linha é independente das outras, então as linhas podem ir para trabalhadores diferentes. */
static void xfb_row(u32 y, u32 worker, void *user) {
    (void)worker;
    const XfbJob *j = user;
    const CopyParams *p = j->p;
    const s32 row = (s32)floorf((((f32)y + 0.5F) + (f32)p->top) / j->yscale);
    for (u32 x = 0; x + 1U < j->width + 1U; x += 2U) {
        u32 c0[4];
        u32 c1[4];
        efb_sample(p, (s32)(p->left + x), row, c0);
        efb_sample(p, (s32)(p->left + x + 1U), row, c1);
        u8 yuyv[4];
        encode_xfb_pair(c0, c1, yuyv);
        if (j->host != NULL) {
            memcpy(j->host + ((size_t)y * j->stride) + ((size_t)x * 2U), yuyv, 4U); /* já na ordem da memória */
        } else {
            mem_gx_write32(MEM_CACHED_BASE | ((j->dest + (y * j->stride) + (x * 2U)) & 0x01FFFFFFU),
                           ((u32)yuyv[0] << 24) | ((u32)yuyv[1] << 16) | ((u32)yuyv[2] << 8) | yuyv[3]);
        }
    }
}

static void copy_to_xfb(const CopyParams *p, u32 copy, u32 dest, u32 stride) {
    const u32 wh = gx_bp_reg(GX_BP_EFB_WH);
    const u32 ys = gx_bp_reg(GX_BP_COPYYSCALE) & 0x1FFU;
    f32 yscale = (copy & COPY_SCALE_INVERT) != 0U ? 256.0F / (f32)ys : (f32)ys / 256.0F;
    if (!(yscale > 0.0F)) {
        yscale = 1.0F;
    }
    const u32 height = (u32)(1.0F + ((f32)((wh >> 10) & 0x3FFU) * yscale));
    const u32 width = p->right - p->left;
    XfbJob job = {p, NULL, dest, stride, width, height, yscale};
    const u32 span = height != 0U ? ((height - 1U) * stride) + (((width + 1U) & ~1U) * 2U) : 0U;
    const u32 phys = dest & 0x01FFFFFFU;
    if (span != 0U && phys + span <= MEM_MEM1_SIZE && stride >= ((width + 1U) & ~1U) * 2U) {
        job.host = mem_host_ptr(MEM_CACHED_BASE | phys, span);
    }
    if (job.host != NULL) {
        gx_raster_parallel_for(height, xfb_row, &job);
        mem_gx_wrote(phys, span);
    } else {
        for (u32 y = 0; y < height; y++) {
            xfb_row(y, 0, &job);
        }
    }
    s_efb.stats.xfb_copies++;
    s_efb.stats.last_xfb_addr = dest;
    s_efb.stats.last_xfb_width = width;
    s_efb.stats.last_xfb_height = height;
}

/* RGB8_to_I (TextureEncoder.cpp): intensidade BT.601 com 16 de piso. */
static u32 intensity(const u32 *c) {
    return (4096U + (66U * c[0]) + (129U * c[1]) + (25U * c[2])) >> 8;
}

typedef struct BlockLayout {
    u32 bw_log2, bh_log2;
    u32 bits; /* por texel */
} BlockLayout;

static BlockLayout copy_layout(u32 fmt) {
    switch (fmt) {
    case CF_R4:
        return (BlockLayout){3, 3, 4};
    case CF_R8_1:
    case CF_RA4:
    case CF_A8:
    case CF_R8:
    case CF_G8:
    case CF_B8:
        return (BlockLayout){3, 2, 8};
    case CF_RGBA8:
        return (BlockLayout){2, 2, 32};
    case CF_RA8:
    case CF_RGB565:
    case CF_RGB5A3:
    case CF_RG8:
    case CF_GB8:
        return (BlockLayout){2, 2, 16};
    default:
        am_fatal("GX: cópia do EFB para textura no formato %u (inválido)", fmt);
    }
}

/* Texel no formato de cópia: devolve até 4 bytes (16 bits: 2; RGBA8: AR e GB em out[0..1], out[2..3]). */
static u32 encode_texel(u32 fmt, const u32 *c, bool yuv) {
    const u32 i = yuv ? intensity(c) : c[0];
    switch (fmt) {
    case CF_R4:
        return i >> 4;
    case CF_R8_1:
    case CF_R8:
        return i;
    case CF_RA4:
        return ((c[3] >> 4) << 4) | (i >> 4);
    case CF_RA8:
        return (c[3] << 8) | i;
    case CF_RGB565:
        return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3);
    case CF_RGB5A3:
        if ((c[3] >> 5) == 7U) {
            return 0x8000U | ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3);
        }
        return ((c[3] >> 5) << 12) | ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4);
    case CF_RGBA8:
        return (c[3] << 24) | (c[0] << 16) | (c[1] << 8) | c[2];
    case CF_A8:
        return c[3];
    case CF_G8:
        return c[1];
    case CF_B8:
        return c[2];
    case CF_RG8:
        return (c[1] << 8) | c[0];
    default: /* CF_GB8 */
        return (c[2] << 8) | c[1];
    }
}

static void write_texel(GuestAddr block, BlockLayout l, u32 s, u32 t, u32 v) {
    const u32 idx = (t << l.bw_log2) + s;
    if (l.bits == 4U) {
        const GuestAddr a = block + (idx >> 1);
        const u8 old = mem_gx_read8(a);
        mem_gx_write8(a, (u8)((idx & 1U) != 0U ? ((old & 0xF0U) | v) : ((old & 0x0FU) | (v << 4))));
    } else if (l.bits == 8U) {
        mem_gx_write8(block + idx, (u8)v);
    } else if (l.bits == 16U) {
        mem_gx_write16(block + (idx * 2U), (u16)v);
    } else {
        mem_gx_write16(block + (idx * 2U), (u16)(v >> 16));
        mem_gx_write16(block + 32U + (idx * 2U), (u16)v);
    }
}

static void note_kind(u32 fmt, u32 w, u32 h, bool half, bool depth) {
    GxEfbStats *st = &s_efb.stats;
    for (u32 i = 0; i < st->kind_count; i++) {
        GxEfbCopyKind *k = &st->kinds[i];
        if (k->format == fmt && k->width == w && k->height == h && k->half == half && k->depth == depth) {
            k->count++;
            return;
        }
    }
    if (st->kind_count < GX_EFB_COPY_KINDS) {
        st->kinds[st->kind_count++] = (GxEfbCopyKind){fmt, w, h, half, depth, 1};
    }
}

static void copy_to_texture(const CopyParams *p, u32 copy, u32 dest, u32 stride) {
    const u32 fmt = (((copy >> 3) & 15U) / 2U) + ((((copy >> 3) & 15U) & 1U) * 8U); /* tp_realFormat */
    const bool half = (copy & COPY_HALF_SCALE) != 0U;
    const bool yuv = (copy & COPY_INTENSITY) != 0U && (copy & COPY_AUTO_CONV) != 0U;
    const BlockLayout l = copy_layout(fmt);
    const u32 shift = half ? 1U : 0U;
    const u32 w = ((p->right - p->left - 1U) >> shift) + 1U;
    const u32 h = ((p->bottom - p->top - 1U) >> shift) + 1U;
    const u32 bw = 1U << l.bw_log2;
    const u32 bh = 1U << l.bh_log2;
    const u32 block_bytes = l.bits == 32U ? 64U : 32U;
    for (u32 t = 0; t < ((h + bh - 1U) & ~(bh - 1U)); t++) {
        for (u32 s = 0; s < ((w + bw - 1U) & ~(bw - 1U)); s++) {
            u32 c[4] = {0, 0, 0, 0};
            for (u32 k = 0; k < (half ? 4U : 1U); k++) { /* meia escala: média de 2x2 */
                u32 v[4];
                efb_sample(p, (s32)(p->left + (s << shift) + (k & 1U)), (s32)(p->top + (t << shift) + (k >> 1)), v);
                for (u32 j = 0; j < 4U; j++) {
                    c[j] += v[j];
                }
            }
            for (u32 j = 0; j < 4U && half; j++) {
                c[j] >>= 2;
            }
            const GuestAddr block =
                MEM_CACHED_BASE |
                ((dest + ((t >> l.bh_log2) * stride) + ((s >> l.bw_log2) * block_bytes)) & 0x01FFFFFFU);
            write_texel(block, l, s & (bw - 1U), t & (bh - 1U), encode_texel(fmt, c, yuv));
        }
    }
    s_efb.stats.texture_copies++;
    s_efb.stats.copy_formats |= 1U << fmt;
    s_efb.stats.depth_copies += p->depth ? 1U : 0U;
    note_kind(fmt, p->right - p->left, p->bottom - p->top, half, p->depth);
}

/* ClearEfb: a área da cópia, respeitando as máscaras de escrita de cor/alfa/Z. */
static void clear(const CopyParams *p) {
    const u32 ar = gx_bp_reg(GX_BP_CLEAR_AR);
    const u32 gb = gx_bp_reg(GX_BP_CLEAR_GB);
    const u8 rgba[4] = {(u8)ar, (u8)(gb >> 8), (u8)gb, (u8)(ar >> 8)};
    const u32 z = gx_bp_reg(GX_BP_CLEAR_Z) & 0xFFFFFFU;
    const u32 mode = gx_bp_reg(GX_BP_BLENDMODE);
    const bool color = ((mode >> 3) & 1U) != 0U;
    const bool alpha = ((mode >> 4) & 1U) != 0U;
    const bool zupd = ((gx_bp_reg(GX_BP_ZMODE) >> 4) & 1U) != 0U;
    const u32 format = pixel_format();
    for (u32 y = p->top; y < p->bottom && y < GX_EFB_HEIGHT; y++) {
        for (u32 x = p->left; x < p->right && x < GX_EFB_WIDTH; x++) {
            const u32 i = (y * GX_EFB_WIDTH) + x;
            if (color || alpha) {
                store(format, i, rgba, color, alpha);
            }
            if (zupd) {
                s_efb.depth[i] = z;
            }
        }
    }
    s_efb.stats.clears++;
}

bool gx_efb_copy_clears(void) {
    return (gx_bp_reg(GX_BP_COPY_EXECUTE) & COPY_CLEAR) != 0U;
}

bool gx_efb_copy_is_xfb(void) {
    return (gx_bp_reg(GX_BP_COPY_EXECUTE) & COPY_TO_XFB) != 0U;
}

void gx_efb_copy(void) {
    gx_efb_copy_ex(true);
}

bool gx_efb_copy_rows(u32 *top, u32 *bottom, bool *depth) {
    const u32 copy = gx_bp_reg(GX_BP_COPY_EXECUTE);
    CopyParams p = copy_params(copy);
    if (p.left >= GX_EFB_WIDTH || p.top >= GX_EFB_HEIGHT) {
        return false;
    }
    /* O filtro vertical lê uma linha acima e uma abaixo. */
    *top = p.top > 0U ? p.top - 1U : 0U;
    *bottom = p.bottom + 1U < GX_EFB_HEIGHT ? p.bottom + 1U : GX_EFB_HEIGHT;
    *depth = p.depth;
    return true;
}

bool gx_efb_clear_params(GxEfbClear *c) {
    const CopyParams p = copy_params(gx_bp_reg(GX_BP_COPY_EXECUTE));
    const u32 ar = gx_bp_reg(GX_BP_CLEAR_AR);
    const u32 gb = gx_bp_reg(GX_BP_CLEAR_GB);
    const u32 mode = gx_bp_reg(GX_BP_BLENDMODE);
    *c = (GxEfbClear){.left = p.left,
                      .top = p.top,
                      .right = p.right < GX_EFB_WIDTH ? p.right : GX_EFB_WIDTH,
                      .bottom = p.bottom < GX_EFB_HEIGHT ? p.bottom : GX_EFB_HEIGHT,
                      .rgba = {(u8)ar, (u8)(gb >> 8), (u8)gb, (u8)(ar >> 8)},
                      .z = gx_bp_reg(GX_BP_CLEAR_Z) & 0xFFFFFFU,
                      .color = ((mode >> 3) & 1U) != 0U,
                      .alpha = ((mode >> 4) & 1U) != 0U,
                      .z_update = ((gx_bp_reg(GX_BP_ZMODE) >> 4) & 1U) != 0U,
                      .format = pixel_format()};
    return c->left < c->right && c->top < c->bottom;
}

/* M9 (--internal-res N, etapa 1b): a mesma cópia para o XFB, mas lida do EFB grande e escrita direto em RGBA, sem o
 * YUV 4:2:2 (a janela mostra a imagem em N x N pixels por pixel do XFB). O filtro vertical e a gama são os da cópia
 * nativa; o filtro olha a linha grande de cima e a de baixo (e não N linhas), então o desfoque do filtro de
 * deflicker encolhe 1/N junto com o pixel. */
typedef struct HiXfbJob {
    const CopyParams *p;
    const u32 *hi;
    u32 n, width, format;
    f32 yscale;
    u8 *rgba;
    u8 gamma[256]; /* só com gama diferente de 1 */
} HiXfbJob;

static void hi_xfb_row(u32 y, u32 worker, void *user) {
    (void)worker;
    const HiXfbJob *j = user;
    const CopyParams *p = j->p;
    const s32 lo = (s32)(p->clamp_top * j->n);
    const s32 last = (s32)((p->clamp_bottom + 1U) * j->n) - 1;
    const s32 row = (s32)floorf((((f32)y + 0.5F) + (f32)(p->top * j->n)) / j->yscale);
    const s32 cur = gx_clamp_s32(row, lo, last);
    const s32 rows[3] = {cur > lo ? cur - 1 : lo, cur, cur < last ? cur + 1 : last};
    const size_t stride = (size_t)GX_EFB_WIDTH * j->n;
    for (u32 x = 0; x < j->width; x++) {
        const size_t cx = (size_t)gx_clamp_s32((s32)((p->left * j->n) + x), 0, (s32)stride - 1);
        u8 px[3][4];
        for (u32 k = 0; k < 3U; k++) {
            unpack(j->format, j->hi[((size_t)rows[k] * stride) + cx], px[k]);
        }
        u8 *out = &j->rgba[(((size_t)y * j->width) + x) * 4U];
        for (u32 c = 0; c < 3U; c++) {
            u32 v = (((u32)px[0][c] * p->coef[0]) + ((u32)px[1][c] * p->coef[1]) + ((u32)px[2][c] * p->coef[2])) >> 6;
            v = p->overflow ? v & 0x1FFU : v;
            v = v > 255U ? 255U : v;
            out[c] = p->gamma_rcp != 1.0F ? j->gamma[v] : (u8)v;
        }
        out[3] = 0xFF;
    }
}

bool gx_efb_copy_xfb_hires(const u32 *hi, u32 n, u8 *rgba, u32 cap, GxEfbHiXfb *out) {
    const u32 copy = gx_bp_reg(GX_BP_COPY_EXECUTE);
    CopyParams p = copy_params(copy);
    if ((copy & COPY_TO_XFB) == 0U || p.depth || p.left >= GX_EFB_WIDTH || p.top >= GX_EFB_HEIGHT) {
        return false;
    }
    p.right = p.right > GX_EFB_WIDTH ? GX_EFB_WIDTH : p.right;
    p.bottom = p.bottom > GX_EFB_HEIGHT ? GX_EFB_HEIGHT : p.bottom;
    clamp_rows(&p, copy);
    const u32 ys = gx_bp_reg(GX_BP_COPYYSCALE) & 0x1FFU;
    f32 yscale = (copy & COPY_SCALE_INVERT) != 0U ? 256.0F / (f32)ys : (f32)ys / 256.0F;
    yscale = yscale > 0.0F ? yscale : 1.0F;
    const u32 height = (u32)(1.0F + ((f32)((gx_bp_reg(GX_BP_EFB_WH) >> 10) & 0x3FFU) * yscale));
    const u32 width = p.right - p.left;
    if (((gx_bp_reg(GX_BP_EFB_STRIDE) & 0x3FFU) << 5) == 0U || (size_t)width * height * n * n * 4U > cap) {
        return false;
    }
    HiXfbJob job = {.p = &p, .hi = hi, .n = n, .width = width * n, .format = pixel_format(), .yscale = yscale,
                    .rgba = rgba};
    for (u32 v = 0; v < 256U && p.gamma_rcp != 1.0F; v++) {
        job.gamma[v] = (u8)roundf(powf((f32)v / 255.0F, p.gamma_rcp) * 255.0F);
    }
    gx_raster_parallel_for(height * n, hi_xfb_row, &job);
    *out = (GxEfbHiXfb){.dest = (gx_bp_reg(GX_BP_EFB_ADDR) << 5) & 0x01FFFFFFU, .width = width * n, .height = height * n};
    return true;
}

void gx_efb_copy_ex(bool do_clear) {
    const u32 copy = gx_bp_reg(GX_BP_COPY_EXECUTE);
    CopyParams p = copy_params(copy);
    if (p.right > GX_EFB_WIDTH || p.bottom > GX_EFB_HEIGHT) { /* BPStructs.cpp: cópia maior que o EFB */
        if (p.left >= GX_EFB_WIDTH || p.top >= GX_EFB_HEIGHT) {
            return;
        }
        p.right = p.right > GX_EFB_WIDTH ? GX_EFB_WIDTH : p.right;
        p.bottom = p.bottom > GX_EFB_HEIGHT ? GX_EFB_HEIGHT : p.bottom;
    }
    clamp_rows(&p, copy);
    const u32 dest = (gx_bp_reg(GX_BP_EFB_ADDR) << 5) & 0x01FFFFFFU;
    const u32 stride = (gx_bp_reg(GX_BP_EFB_STRIDE) & 0x3FFU) << 5;
    if (stride != 0U) {
        if ((copy & COPY_TO_XFB) != 0U) {
            copy_to_xfb(&p, copy, dest, stride);
        } else {
            copy_to_texture(&p, copy, dest, stride);
        }
    }
    if (do_clear && (copy & COPY_CLEAR) != 0U) {
        clear(&p);
    }
}

/* write_xfb_png do oráculo: double, truncamento de int() e saturação em [0, 255]. */
static u8 xfb_channel(f64 v) {
    const s32 i = (s32)v;
    return (u8)gx_clamp_s32(i, 0, 255);
}

void gx_efb_xfb_to_rgba(GuestAddr addr, u32 width, u32 height, u32 stride, u8 *rgba) {
    for (u32 y = 0; y < height; y++) {
        const GuestAddr line = MEM_CACHED_BASE | ((addr + (y * stride)) & 0x01FFFFFFU);
        for (u32 x = 0; x + 1U < width + 1U; x += 2U) {
            const u32 w = mem_read32(NULL, line + (x * 2U));
            const f64 u = (f64)((w >> 16) & 0xFFU) - 128.0;
            const f64 v = (f64)(w & 0xFFU) - 128.0;
            const u32 ys[2] = {w >> 24, (w >> 8) & 0xFFU};
            for (u32 k = 0; k < 2U && x + k < width; k++) {
                const f64 c = 1.164 * ((f64)ys[k] - 16.0);
                u8 *px = &rgba[(((size_t)y * width) + x + k) * 4U];
                px[0] = xfb_channel(c + (1.596 * v));
                px[1] = xfb_channel(c - (0.392 * u) - (0.813 * v));
                px[2] = xfb_channel(c + (2.017 * u));
                px[3] = 0xFF;
            }
        }
    }
}
