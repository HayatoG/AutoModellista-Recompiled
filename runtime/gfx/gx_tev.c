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

typedef struct Stage {
    u32 cc;
    u32 ac;
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

/* Valores de entrada de um estágio (TevColor do Dolphin, em RGBA). */
typedef struct Inputs {
    s16 (*reg)[4];
    const s16 *tex;
    const s16 *ras;
    const s16 *konst;
} Inputs;

static void color_arg(const Inputs *in, u32 arg, s32 *out) {
    static const s16 s_one[4] = {255, 255, 255, 255};
    static const s16 s_half[4] = {128, 128, 128, 128};
    static const s16 s_zero[4] = {0, 0, 0, 0};
    const s16 *src;
    switch (arg >> 1) {
    case 0U:
    case 1U:
    case 2U:
    case 3U:
        src = in->reg[arg >> 1];
        break;
    case 4U:
        src = in->tex;
        break;
    case 5U:
        src = in->ras;
        break;
    case 6U:
        src = (arg & 1U) == 0U ? s_one : s_half;
        arg &= ~1U; /* one e half: valor em todos os canais */
        break;
    default:
        src = (arg & 1U) == 0U ? in->konst : s_zero;
        arg &= ~1U;
        break;
    }
    for (u32 c = 0; c < 3U; c++) {
        out[c] = (arg & 1U) != 0U ? src[CH_A] : src[c];
    }
}

static s32 alpha_arg(const Inputs *in, u32 arg) {
    switch (arg) {
    case 0U:
    case 1U:
    case 2U:
    case 3U:
        return in->reg[arg][CH_A];
    case 4U:
        return in->tex[CH_A];
    case 5U:
        return in->ras[CH_A];
    case 6U:
        return in->konst[CH_A];
    default:
        return 0;
    }
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

static void run_combiners(const Stage *s, const Inputs *in, s16 (*reg)[4]) {
    s32 a[4];
    s32 b[4];
    s32 c[4];
    s32 d[4];
    color_arg(in, (s->cc >> 12) & 15U, a);
    color_arg(in, (s->cc >> 8) & 15U, b);
    color_arg(in, (s->cc >> 4) & 15U, c);
    color_arg(in, s->cc & 15U, d);
    a[CH_A] = alpha_arg(in, (s->ac >> 13) & 7U);
    b[CH_A] = alpha_arg(in, (s->ac >> 10) & 7U);
    c[CH_A] = alpha_arg(in, (s->ac >> 7) & 7U);
    d[CH_A] = alpha_arg(in, (s->ac >> 4) & 7U);
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
        reg[cdest][ch] = clamp_out(out[ch], ((s->cc >> 19) & 1U) != 0U);
    }
    reg[adest][CH_A] = clamp_out(out[CH_A], ((s->ac >> 19) & 1U) != 0U);
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
    s16 reg[4][4];
    memcpy(reg, s_tev.regs, sizeof(reg));
    u8 ind_tex[4][4] = {{0}};
    sample_indirect(p, ind_tex);
    IndState is = {{0, 0}, 0};
    s16 raw[4] = {0, 0, 0, 0};
    s16 tex[4] = {0, 0, 0, 0};
    s16 ras[4];
    s16 konst[4];
    const Inputs in = {reg, tex, ras, konst};
    for (u32 i = 0; i < s_tev.stages; i++) {
        const Stage *s = &s_tev.st[i];
        indirect(s, ind_tex, p->uv[s->texcoord][0], p->uv[s->texcoord][1], &is);
        if (s->tex) {
            u8 texel[4] = {0, 0, 0, 0};
            if (s_tev.texgens > 0U) { /* sem texgens o resultado é preto (Tev.cpp) */
                gx_tex_sample(is.tc[0], is.tc[1], p->lod[i], p->linear[i], s->texmap, texel);
            }
            const u32 tswap = (s->ac >> 2) & 3U;
            for (u32 c = 0; c < 4U; c++) {
                raw[c] = texel[c];
                tex[c] = texel[s_tev.swap[tswap][c]];
            }
        }
        konst[0] = s_tev.kc[s->kc][0];
        konst[1] = s_tev.kc[s->kc][1];
        konst[2] = s_tev.kc[s->kc][2];
        konst[3] = s_tev.ka[s->ka];
        ras_color(p, s->chan, s->ac & 3U, is.alpha_bump, ras);
        run_combiners(s, &in, reg);
    }
    AM_ASSERT(s_tev.stages >= 1U && s_tev.stages <= GX_TEV_STAGES, "TEV: %u estágios", s_tev.stages);
    const Stage *last = &s_tev.st[s_tev.stages - 1U];
    const u32 cdest = (last->cc >> 22) & 3U;
    const u32 adest = (last->ac >> 22) & 3U;
    u8 out[4] = {(u8)reg[cdest][CH_R], (u8)reg[cdest][CH_G], (u8)reg[cdest][CH_B], (u8)reg[adest][CH_A]};
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
