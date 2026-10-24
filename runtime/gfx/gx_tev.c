/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TEV. Tradução para C de Dolphin VideoBackends/Software/Tev.cpp (GPL-2.0-or-later, Dolphin Emulator Project); os
 * registradores de BP são decodificados uma vez por desenho. */
#include "am/gx_tev.h"

#include <math.h>
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/gx_tex.h"
#include "am/gx_util.h"
#include "am/gx_xf.h"
#include "am/log.h"

#define CH_R 0U
#define CH_G 1U
#define CH_B 2U
#define CH_A 3U

#define BIAS_COMPARE 3U
#define SCALE_DIV2   3U

/* Tabela de valores de entrada de um estágio (por pixel): os 4 registradores (PREV, REG0..2), textura, cor
 * rasterizada, konst e as constantes 1, 1/2 e 0. Os seletores do BP viram índices nela uma vez por desenho. */
enum { V_TEX = 4, V_RAS = 5, V_KONST = 6, V_ONE = 7, V_HALF = 8, V_ZERO = 9, V_COUNT = 10 };

typedef struct Stage {
    u32 cc;
    u32 ac;
    u8 csel[4];   /* a, b, c, d da cor: índice na tabela */
    bool crep[4]; /* a entrada da cor é o alfa replicado */
    u8 asel[4];   /* a, b, c, d do alfa */
    u32 texmap;
    u32 texcoord;
    bool tex;
    u32 chan;
    u32 kc;
    u32 ka;
    u32 ind;
} Stage;

static struct {
    u32 stages;
    u32 texgens;
    u32 ind_stages;
    Stage st[GX_TEV_STAGES];
    u8 swap[4][4];
    s16 regs[4][4];
    s16 kc[32][3];
    s16 ka[32];
    u32 alpha_test;
    u32 ztex2;
    u32 ztex_bias;
    bool early_z;
    bool late_z;
    u32 fog3;
    f32 fog_a;
    f32 fog_c;
    s32 fog_bmag;
    u32 fog_bshift;
    u32 fog_color;
    u32 fog_range[6];
    f32 vp_wd;
    u32 iref;
    u32 ind_scale[4][2];
    s32 ind_mtx[3][6];
    s32 ind_mtx_scale[3];
} s_tev;

static const s16 s_konst_fixed[8] = {255, 223, 191, 159, 128, 96, 64, 32}; /* 1, 7/8 ... 1/8 (Tev.h) */
static const s16 s_bias[4] = {0, 128, -128, 0};
static const u8 s_lshift[4] = {0, 1, 2, 0};
static const u8 s_rshift[4] = {0, 0, 0, 1};

static f32 fog_float(u32 v) {
    const u32 bits = (((v >> 19) & 1U) << 31) | (((v >> 11) & 0xFFU) << 23) | ((v & 0x7FFU) << 12);
    f32 f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static s32 sign11(u32 v) {
    return (s32)(v << 21) >> 21;
}

static void decode_konst(void) {
    s16 k[4][4];
    for (u32 n = 0; n < 4U; n++) {
        gx_bp_tev_konst(n, k[n]);
    }
    memset(s_tev.kc, 0, sizeof(s_tev.kc));
    memset(s_tev.ka, 0, sizeof(s_tev.ka));
    for (u32 i = 0; i < 8U; i++) {
        s_tev.kc[i][0] = s_tev.kc[i][1] = s_tev.kc[i][2] = s_konst_fixed[i];
        s_tev.ka[i] = s_konst_fixed[i];
    }
    for (u32 n = 0; n < 4U; n++) {
        memcpy(s_tev.kc[12U + n], k[n], 3U * sizeof(s16)); /* K0..K3 RGB; alfa 0 */
        for (u32 c = 0; c < 4U; c++) {                     /* K0..K3 R, G, B, A replicados */
            const u32 sel = 16U + (c * 4U) + n;
            s_tev.kc[sel][0] = s_tev.kc[sel][1] = s_tev.kc[sel][2] = k[n][c];
            s_tev.ka[sel] = k[n][c];
        }
    }
}

/* TevColorArg / TevAlphaArg -> tabela de valores. */
static void decode_selectors(Stage *s) {
    for (u32 k = 0; k < 4U; k++) {
        const u32 arg = (s->cc >> (12U - (4U * k))) & 15U;
        static const u8 s_const_sel[4] = {V_ONE, V_HALF, V_KONST, V_ZERO};
        if ((arg >> 1) < 6U) {
            s->csel[k] = (u8)(arg >> 1);
            s->crep[k] = (arg & 1U) != 0U;
        } else {
            s->csel[k] = s_const_sel[arg - 12U];
            s->crep[k] = false;
        }
        const u32 aarg = (s->ac >> (13U - (3U * k))) & 7U;
        s->asel[k] = (u8)(aarg == 7U ? V_ZERO : aarg);
    }
}

static void decode_stages(void) {
    for (u32 i = 0; i < s_tev.stages; i++) {
        const u32 order = gx_bp_reg((u8)(GX_BP_TREF + (i >> 1)));
        const u32 o = (i & 1U) != 0U ? order >> 12 : order;
        const u32 ksel = gx_bp_reg((u8)(GX_BP_TEV_KSEL + (i >> 1)));
        Stage *s = &s_tev.st[i];
        s->cc = gx_bp_reg((u8)(GX_BP_TEV_COLOR_ENV + (2U * i)));
        s->ac = gx_bp_reg((u8)(GX_BP_TEV_ALPHA_ENV + (2U * i)));
        s->texmap = o & 7U;
        s->texcoord = (o >> 3) & 7U;
        s->tex = ((o >> 6) & 1U) != 0U;
        s->chan = (o >> 7) & 7U;
        s->kc = (i & 1U) != 0U ? (ksel >> 14) & 31U : (ksel >> 4) & 31U;
        s->ka = (i & 1U) != 0U ? (ksel >> 19) & 31U : (ksel >> 9) & 31U;
        s->ind = gx_bp_reg((u8)(GX_BP_IND_CMD + i));
        decode_selectors(s);
        if (s->texcoord >= s_tev.texgens) {
            s->texcoord = 0; /* coordenada inexistente: usa a 0 (Luigi's Mansion, Tev.cpp) */
        }
    }
    for (u32 t = 0; t < 4U; t++) { /* GetSwapTable */
        const u32 rg = gx_bp_reg((u8)(GX_BP_TEV_KSEL + (t * 2U)));
        const u32 ba = gx_bp_reg((u8)(GX_BP_TEV_KSEL + (t * 2U) + 1U));
        s_tev.swap[t][0] = (u8)(rg & 3U);
        s_tev.swap[t][1] = (u8)((rg >> 2) & 3U);
        s_tev.swap[t][2] = (u8)(ba & 3U);
        s_tev.swap[t][3] = (u8)((ba >> 2) & 3U);
    }
}

static void decode_indirect(void) {
    s_tev.iref = gx_bp_reg(GX_BP_IREF);
    for (u32 i = 0; i < 2U; i++) {
        const u32 ss = gx_bp_reg((u8)(GX_BP_RAS1_SS0 + i));
        s_tev.ind_scale[(size_t)i * 2U][0] = ss & 15U;
        s_tev.ind_scale[(size_t)i * 2U][1] = (ss >> 4) & 15U;
        s_tev.ind_scale[(i * 2U) + 1U][0] = (ss >> 8) & 15U;
        s_tev.ind_scale[(i * 2U) + 1U][1] = (ss >> 12) & 15U;
    }
    for (u32 m = 0; m < 3U; m++) {
        const u32 a = gx_bp_reg((u8)(GX_BP_IND_MTXA + (3U * m)));
        const u32 b = gx_bp_reg((u8)(GX_BP_IND_MTXA + (3U * m) + 1U));
        const u32 c = gx_bp_reg((u8)(GX_BP_IND_MTXA + (3U * m) + 2U));
        s_tev.ind_mtx[m][0] = sign11(a);
        s_tev.ind_mtx[m][1] = sign11(a >> 11);
        s_tev.ind_mtx[m][2] = sign11(b);
        s_tev.ind_mtx[m][3] = sign11(b >> 11);
        s_tev.ind_mtx[m][4] = sign11(c);
        s_tev.ind_mtx[m][5] = sign11(c >> 11);
        s_tev.ind_mtx_scale[m] = (s32)(((a >> 22) & 3U) | (((b >> 22) & 3U) << 2) | (((c >> 22) & 1U) << 4));
    }
}

void gx_tev_begin_draw(void) {
    const u32 gen = gx_bp_reg(GX_BP_GENMODE);
    s_tev.stages = ((gen >> 10) & 15U) + 1U;
    s_tev.texgens = gen & 15U;
    s_tev.ind_stages = (gen >> 16) & 7U;
    if (s_tev.ind_stages > 4U) {
        s_tev.ind_stages = 4U;
    }
    decode_stages();
    decode_konst();
    decode_indirect();
    for (u32 n = 0; n < 4U; n++) {
        gx_bp_tev_color(n, s_tev.regs[n]);
    }
    s_tev.alpha_test = gx_bp_reg(GX_BP_ALPHACOMPARE);
    s_tev.ztex2 = gx_bp_reg(GX_BP_ZTEX2);
    s_tev.ztex_bias = gx_bp_reg(GX_BP_ZTEX_BIAS) & 0xFFFFFFU;
    const bool ztest = (gx_bp_reg(GX_BP_ZMODE) & 1U) != 0U;
    const bool early = ((gx_bp_reg(GX_BP_ZCOMPARE) >> 6) & 1U) != 0U;
    s_tev.early_z = ztest && early;
    s_tev.late_z = ztest && !early;
    s_tev.fog3 = gx_bp_reg(GX_BP_FOGPARAM3);
    s_tev.fog_a = fog_float(gx_bp_reg(GX_BP_FOGPARAM0));
    s_tev.fog_c = fog_float(s_tev.fog3);
    s_tev.fog_bmag = (s32)gx_bp_reg(GX_BP_FOGBMAGNITUDE);
    s_tev.fog_bshift = gx_bp_reg(GX_BP_FOGBEXPONENT);
    s_tev.fog_color = gx_bp_reg(GX_BP_FOGCOLOR);
    for (u32 i = 0; i < 6U; i++) {
        s_tev.fog_range[i] = gx_bp_reg((u8)(GX_BP_FOGRANGE + i));
    }
    s_tev.vp_wd = gx_xf_viewport().wd;
}

bool gx_tev_early_z(void) {
    return s_tev.early_z;
}

/* DrawColorRegular / DrawAlphaRegular: a, b, c truncados a 8 bits; d com 11 bits com sinal. */
static s32 combine(u32 env, s32 a, s32 b, s32 c, s32 d, bool alpha) {
    const u32 bias = (env >> 16) & 3U;
    const bool sub = ((env >> 18) & 1U) != 0U;
    const u32 scale = (env >> 20) & 3U;
    a &= 0xFF;
    b &= 0xFF;
    c &= 0xFF;
    d = sign11((u32)d);
    const s32 cc = c + (c >> 7);
    s32 temp = (a * (256 - cc)) + (b * cc);
    temp <<= s_lshift[scale];
    if (scale != SCALE_DIV2) {
        temp += sub ? 127 : 128;
    }
    if (alpha) {
        temp = sub ? (-temp >> 8) : (temp >> 8);
    } else {
        temp >>= 8;
        temp = sub ? -temp : temp;
    }
    s32 result = ((d + s_bias[bias]) << s_lshift[scale]) + temp;
    return result >> s_rshift[scale];
}

/* Modo de comparação: R8, GR16, BGR24 e RGB8/A8 (DrawColorCompare / DrawAlphaCompare). */
static bool compare(u32 env, const s32 a[4], const s32 b[4], u32 channel) {
    const u32 mode = (env >> 20) & 3U;
    u32 va;
    u32 vb;
    switch (mode) {
    case 0U:
        va = (u32)a[CH_R] & 0xFFU;
        vb = (u32)b[CH_R] & 0xFFU;
        break;
    case 1U:
        va = (((u32)a[CH_G] & 0xFFU) << 8) | ((u32)a[CH_R] & 0xFFU);
        vb = (((u32)b[CH_G] & 0xFFU) << 8) | ((u32)b[CH_R] & 0xFFU);
        break;
    case 2U:
        va = (((u32)a[CH_B] & 0xFFU) << 16) | (((u32)a[CH_G] & 0xFFU) << 8) | ((u32)a[CH_R] & 0xFFU);
        vb = (((u32)b[CH_B] & 0xFFU) << 16) | (((u32)b[CH_G] & 0xFFU) << 8) | ((u32)b[CH_R] & 0xFFU);
        break;
    default:
        va = (u32)a[channel] & 0xFFU;
        vb = (u32)b[channel] & 0xFFU;
        break;
    }
    return ((env >> 18) & 1U) != 0U ? va == vb : va > vb; /* comparison: 0 GT, 1 EQ */
}

static s16 clamp_out(s32 v, bool clamp) {
    if (clamp) {
        return (s16)gx_clamp_s32(v, 0, 255);
    }
    return (s16)gx_clamp_s32(v, -1024, 1023);
}

static void run_combiners(const Stage *s, s16 (*v)[4]) {
    s32 a[4];
    s32 b[4];
    s32 c[4];
    s32 d[4];
    s32 *const args[4] = {a, b, c, d};
    for (u32 k = 0; k < 4U; k++) {
        const s16 *src = v[s->csel[k]];
        s32 *o = args[k];
        if (s->crep[k]) {
            o[0] = o[1] = o[2] = src[CH_A];
        } else {
            o[0] = src[0];
            o[1] = src[1];
            o[2] = src[2];
        }
        o[CH_A] = v[s->asel[k]][CH_A];
    }
    s32 out[4];
    const bool ccmp = ((s->cc >> 16) & 3U) == BIAS_COMPARE;
    for (u32 ch = 0; ch < 3U; ch++) {
        out[ch] = ccmp ? sign11((u32)d[ch]) + (compare(s->cc, a, b, ch) ? (c[ch] & 0xFF) : 0)
                       : combine(s->cc, a[ch], b[ch], c[ch], d[ch], false);
    }
    const bool acmp = ((s->ac >> 16) & 3U) == BIAS_COMPARE;
    out[CH_A] = acmp ? sign11((u32)d[CH_A]) + (compare(s->ac, a, b, CH_A) ? (c[CH_A] & 0xFF) : 0)
                     : combine(s->ac, a[CH_A], b[CH_A], c[CH_A], d[CH_A], true);
    const u32 cdest = (s->cc >> 22) & 3U;
    const u32 adest = (s->ac >> 22) & 3U;
    for (u32 ch = 0; ch < 3U; ch++) {
        v[cdest][ch] = clamp_out(out[ch], ((s->cc >> 19) & 1U) != 0U);
    }
    v[adest][CH_A] = clamp_out(out[CH_A], ((s->ac >> 19) & 1U) != 0U);
}

static bool alpha_compare(s32 alpha, s32 ref, u32 comp) {
    switch (comp) {
    case 0U:
        return false;
    case 1U:
        return alpha < ref;
    case 2U:
        return alpha == ref;
    case 3U:
        return alpha <= ref;
    case 4U:
        return alpha > ref;
    case 5U:
        return alpha != ref;
    case 6U:
        return alpha >= ref;
    default:
        return true;
    }
}

static bool alpha_test(s32 alpha) {
    const u32 t = s_tev.alpha_test;
    const bool c0 = alpha_compare(alpha, (s32)(t & 0xFFU), (t >> 16) & 7U);
    const bool c1 = alpha_compare(alpha, (s32)((t >> 8) & 0xFFU), (t >> 19) & 7U);
    switch ((t >> 22) & 3U) {
    case 0U:
        return c0 && c1;
    case 1U:
        return c0 || c1;
    case 2U:
        return c0 != c1;
    default:
        return c0 == c1;
    }
}

static s32 wrap_ind(s32 coord, u32 mode) {
    static const s32 s_sizes[6] = {0, 256, 128, 64, 32, 16};
    if (mode == 0U) {
        return coord;
    }
    if (mode >= 6U) {
        return 0; /* ITW_0 (e o inválido 7) */
    }
    return coord & ((s_sizes[mode] << 7) - 1);
}

static s32 sign24(s32 v) {
    return (s32)((u32)v << 8) >> 8;
}

typedef struct IndState {
    s32 tc[2];
    u8 alpha_bump;
} IndState;

/* Tev::Indirect. ind_tex: amostras RGBA das texturas indiretas. */
static void indirect(const Stage *st, const u8 (*ind_tex)[4], s32 s, s32 t, IndState *is) {
    const u32 ind = st->ind;
    if (ind == 0U) { /* o caso comum: o resultado abaixo com tudo desligado, sem as contas */
        is->tc[0] = sign24(s);
        is->tc[1] = sign24(t);
        is->alpha_bump = 0;
        return;
    }
    const u8 *map = ind_tex[ind & 3U];
    const u32 fmt = (ind >> 2) & 3U;
    const u32 bs = (ind >> 7) & 3U;
    static const u8 s_bump_ch[4] = {0, CH_A, CH_B, CH_G};
    is->alpha_bump = bs == 0U ? 0U : map[s_bump_ch[bs]];
    const s32 bias_value = fmt == 0U ? -128 : 1;
    s32 coord[3] = {map[CH_A], map[CH_B], map[CH_G]};
    static const u8 s_shift[4] = {0, 3, 4, 5};
    for (u32 i = 0; i < 3U; i++) {
        coord[i] = (coord[i] >> s_shift[fmt]) + (((ind >> (4U + i)) & 1U) != 0U ? bias_value : 0);
    }
    is->alpha_bump = fmt == 0U ? (u8)(is->alpha_bump & 0xF8U) : (u8)(is->alpha_bump << (8U - s_shift[fmt]));
    s32 trans[2] = {0, 0};
    const u32 mi = (ind >> 9) & 3U;
    if (mi != 0U) {
        const s32 *m = s_tev.ind_mtx[mi - 1U];
        const s32 shift = 17 - s_tev.ind_mtx_scale[mi - 1U];
        switch ((ind >> 11) & 3U) {
        case 0U:
            trans[0] = ((m[0] * coord[0]) + (m[2] * coord[1]) + (m[4] * coord[2])) >> 3;
            trans[1] = ((m[1] * coord[0]) + (m[3] * coord[1]) + (m[5] * coord[2])) >> 3;
            break;
        case 1U:
            trans[0] = s * coord[0] / 256;
            trans[1] = t * coord[0] / 256;
            break;
        case 2U:
            trans[0] = s * coord[1] / 256;
            trans[1] = t * coord[1] / 256;
            break;
        default:
            am_fatal("GX: matriz indireta de id 3 (inválida) no estágio, IND_CMD 0x%06X", ind);
        }
        for (u32 i = 0; i < 2U; i++) {
            trans[i] = shift >= 0 ? trans[i] >> shift : (s32)((u32)trans[i] << (u32)-shift);
        }
    }
    const s32 ns = wrap_ind(s, (ind >> 13) & 7U) + trans[0];
    const s32 nt = wrap_ind(t, (ind >> 16) & 7U) + trans[1];
    const bool add = ((ind >> 20) & 1U) != 0U;
    is->tc[0] = sign24(add ? is->tc[0] + ns : ns);
    is->tc[1] = sign24(add ? is->tc[1] + nt : nt);
}

static void ras_color(const GxTevPixel *p, u32 chan, u32 swap, u8 bump, s16 *out) {
    if (chan <= 1U) {
        for (u32 c = 0; c < 4U; c++) {
            out[c] = p->color[chan][s_tev.swap[swap][c]];
        }
        return;
    }
    s16 v = 0; /* 7 = zero; outros valores são inválidos e também dão zero */
    if (chan == 5U) {
        v = (s16)bump;
    } else if (chan == 6U) {
        v = (s16)(u16)(bump | (bump >> 5));
    }
    out[0] = out[1] = out[2] = out[3] = v;
}

static void sample_indirect(const GxTevPixel *p, u8 (*ind_tex)[4]) {
    for (u32 i = 0; i < s_tev.ind_stages; i++) {
        const u32 map = (s_tev.iref >> (6U * i)) & 7U;
        u32 coord = (s_tev.iref >> ((6U * i) + 3U)) & 7U;
        if (coord >= s_tev.texgens) {
            coord = 0;
        }
        gx_tex_sample(p->uv[coord][0] >> s_tev.ind_scale[i][0], p->uv[coord][1] >> s_tev.ind_scale[i][1], p->ind_lod[i],
                      p->ind_linear[i], map, ind_tex[i]);
    }
}

static void apply_fog(const GxTevPixel *p, s32 z, u8 *out) {
    const u32 fsel = (s_tev.fog3 >> 21) & 7U;
    f32 ze;
    if (((s_tev.fog3 >> 20) & 1U) == 0U) {
        const s32 denom = s_tev.fog_bmag - (z >> s_tev.fog_bshift);
        ze = (s_tev.fog_a * 16777215.0F) / (f32)denom;
    } else {
        ze = s_tev.fog_a * ((f32)z / 16777215.0F);
    }
    if (((s_tev.fog_range[0] >> 10) & 1U) != 0U) { /* ajuste por distância do centro (não verificado no hardware) */
        const f32 offset = ((f32)p->x - ((f32)(s_tev.fog_range[0] & 0x3FFU) - 342.0F)) / s_tev.vp_wd;
        f32 fi = 9.0F - (fabsf(offset) * 9.0F);
        fi = gx_clamp_f32(fi, 0.0F, 9.0F);
        const s32 lo = (s32)fi;
        const s32 hi = lo + 1;
        const u32 klo_reg = s_tev.fog_range[1 + (lo / 2)];
        const u32 khi_reg = s_tev.fog_range[1 + ((hi > 9 ? 9 : hi) / 2)];
        const f32 klo = (f32)((lo % 2) != 0 ? (klo_reg & 0xFFFU) : (klo_reg >> 12) & 0xFFFU) / 256.0F * 4.0F;
        const f32 khi = (f32)((hi % 2) != 0 ? (khi_reg & 0xFFFU) : (khi_reg >> 12) & 0xFFFU) / 256.0F * 4.0F;
        const f32 factor = (f32)hi - fi;
        const f32 k = (klo * factor) + (khi * (1.0F - factor));
        ze *= sqrtf((offset * offset) + (k * k)) / k;
    }
    ze -= s_tev.fog_c;
    f32 fog = gx_clamp_f32(ze, 0.0F, 1.0F);
    switch (fsel) {
    case 4U:
        fog = 1.0F - powf(2.0F, -8.0F * fog);
        break;
    case 5U:
        fog = 1.0F - powf(2.0F, -8.0F * fog * fog);
        break;
    case 6U:
        fog = powf(2.0F, -8.0F * (1.0F - fog));
        break;
    case 7U:
        fog = powf(2.0F, -8.0F * (1.0F - fog) * (1.0F - fog));
        break;
    default:
        break;
    }
    const u32 f = (u32)(fog * 256.0F);
    const u32 inv = 256U - f;
    const u32 rgb[3] = {(s_tev.fog_color >> 16) & 0xFFU, (s_tev.fog_color >> 8) & 0xFFU, s_tev.fog_color & 0xFFU};
    for (u32 c = 0; c < 3U; c++) {
        out[c] = (u8)((((u32)out[c] * inv) + (f * rgb[c])) >> 8);
    }
}

/* Textura de Z: soma ou substitui o Z pelo texel do último estágio (U8, U16 ou U24). */
static s32 z_texture(s32 z, const s16 *raw) {
    const u32 op = (s_tev.ztex2 >> 2) & 3U;
    if (op == 0U) {
        return z;
    }
    u32 ztex = s_tev.ztex_bias;
    switch (s_tev.ztex2 & 3U) {
    case 0U:
        ztex += (u32)raw[CH_A];
        break;
    case 1U:
        ztex += ((u32)raw[CH_A] << 8) | (u32)raw[CH_R];
        break;
    case 2U:
        ztex += ((u32)raw[CH_R] << 16) | ((u32)raw[CH_G] << 8) | (u32)raw[CH_B];
        break;
    default:
        am_fatal("GX: formato de textura de Z 3 inválido (ZTEX2 0x%06X)", s_tev.ztex2);
    }
    if (op == 1U) {
        ztex += (u32)z;
    }
    return (s32)(ztex & 0xFFFFFFU);
}

void gx_tev_shade(const GxTevPixel *p) {
    s16 v[V_COUNT][4] = {{0}};
    memcpy(v, s_tev.regs, sizeof(s_tev.regs));
    v[V_ONE][0] = v[V_ONE][1] = v[V_ONE][2] = v[V_ONE][3] = 255;
    v[V_HALF][0] = v[V_HALF][1] = v[V_HALF][2] = v[V_HALF][3] = 128;
    u8 ind_tex[4][4] = {{0}};
    sample_indirect(p, ind_tex);
    IndState is = {{0, 0}, 0};
    s16 raw[4] = {0, 0, 0, 0};
    for (u32 i = 0; i < s_tev.stages; i++) {
        const Stage *s = &s_tev.st[i];
        indirect(s, ind_tex, p->uv[s->texcoord][0], p->uv[s->texcoord][1], &is);
        if (s->tex) {
            u8 texel[4] = {0, 0, 0, 0};
            if (s_tev.texgens > 0U) { /* sem texgens o resultado é preto (Tev.cpp) */
                gx_tex_sample(is.tc[0], is.tc[1], p->lod[i], p->linear[i], s->texmap, texel);
            }
            const u8 *sw = s_tev.swap[(s->ac >> 2) & 3U];
            for (u32 c = 0; c < 4U; c++) {
                raw[c] = texel[c];
                v[V_TEX][c] = texel[sw[c]];
            }
        }
        v[V_KONST][0] = s_tev.kc[s->kc][0];
        v[V_KONST][1] = s_tev.kc[s->kc][1];
        v[V_KONST][2] = s_tev.kc[s->kc][2];
        v[V_KONST][3] = s_tev.ka[s->ka];
        ras_color(p, s->chan, s->ac & 3U, is.alpha_bump, v[V_RAS]);
        run_combiners(s, v);
    }
    AM_ASSERT(s_tev.stages >= 1U && s_tev.stages <= GX_TEV_STAGES, "TEV: %u estágios", s_tev.stages);
    const Stage *last = &s_tev.st[s_tev.stages - 1U];
    const u32 cdest = (last->cc >> 22) & 3U;
    const u32 adest = (last->ac >> 22) & 3U;
    u8 out[4] = {(u8)v[cdest][CH_R], (u8)v[cdest][CH_G], (u8)v[cdest][CH_B], (u8)v[adest][CH_A]};
    if (!alpha_test(out[CH_A])) {
        return;
    }
    const s32 z = z_texture(p->z, raw);
    if (((s_tev.fog3 >> 21) & 7U) != 0U) {
        apply_fog(p, z, out);
    }
    if (s_tev.late_z && !gx_efb_z_test((u32)p->x, (u32)p->y, (u32)z)) {
        return;
    }
    gx_efb_blend((u32)p->x, (u32)p->y, out);
}
