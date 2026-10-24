/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Transform unit. Tradução para C de Dolphin VideoBackends/Software/TransformUnit.cpp (GPL-2.0-or-later, Dolphin
 * Emulator Project), com a memória do XF guardada como palavras cruas. */
#include "am/gx_xf.h"

#include <math.h>
#include <string.h>

#include "am/gx_util.h"
#include "am/log.h"
#include "am/mem.h"

#define XF_POS_MATRICES  0x000U
#define XF_NRM_MATRICES  0x400U
#define XF_POST_MATRICES 0x500U
#define XF_LIGHTS        0x600U
#define XF_LIGHTS_END    0x680U
#define XF_LIGHT_WORDS   16U

/* TexMtxInfo / LitChannel (XFMemory.h). */
#define TEXGEN_REGULAR 0U
#define TEXGEN_EMBOSS  1U
#define TEXGEN_COLOR0  2U
#define TEXGEN_COLOR1  3U
#define ATTN_NONE      0U
#define ATTN_SPEC      1U
#define ATTN_DIR       2U
#define ATTN_SPOT      3U
#define DIFFUSE_NONE   0U
#define DIFFUSE_SIGN   1U
#define DIFFUSE_CLAMP  2U

static struct {
    u32 mem[GX_XF_END];
} s_xf;

typedef struct Vec3 {
    f32 x, y, z;
} Vec3;

void gx_xf_reset(void) {
    memset(&s_xf, 0, sizeof(s_xf));
}

/* Endereços fora das faixas documentadas são ignorados (Dolphin XFStructs.cpp: LoadXFReg só avisa). */
void gx_xf_write(u32 addr, u32 value) {
    if (addr < GX_XF_END &&
        (addr < 0x100U || (addr >= XF_NRM_MATRICES && addr < XF_LIGHTS_END) || addr >= GX_XF_REGS)) {
        s_xf.mem[addr] = value;
    }
}

u32 gx_xf_read(u32 addr) {
    return addr < GX_XF_END ? s_xf.mem[addr] : 0U;
}

void gx_xf_load_indexed(u32 array, u32 index, u32 addr, u32 size) {
    u8 buf[16U * 4U]; /* LOAD_INDX: até 16 palavras */
    const u32 n = size < 16U ? size : 16U;
    mem_gx_read(MEM_CACHED_BASE | gx_vtx_array_addr(array, index), buf, n * 4U); /* visão da época (mem.h) */
    for (u32 i = 0; i < n; i++) {
        const u8 *w = &buf[(size_t)i * 4U];
        gx_xf_write(addr + i, ((u32)w[0] << 24) | ((u32)w[1] << 16) | ((u32)w[2] << 8) | w[3]);
    }
}

static f32 xf_float(u32 addr) {
    f32 f;
    memcpy(&f, &s_xf.mem[addr], sizeof(f));
    return f;
}

void gx_xf_default_matrices(GxVertex *v) {
    const u32 a = s_xf.mem[GX_XF_MATRIXINDEX_A];
    const u32 b = s_xf.mem[GX_XF_MATRIXINDEX_B];
    v->pos_mtx = (u8)(a & 0x3FU);
    for (u32 n = 0; n < 4U; n++) {
        v->tex_mtx[n] = (u8)((a >> (6U * (n + 1U))) & 0x3FU);
        v->tex_mtx[n + 4U] = (u8)((b >> (6U * n)) & 0x3FU);
    }
}

GxViewport gx_xf_viewport(void) {
    const u32 v = GX_XF_VIEWPORT;
    return (GxViewport){xf_float(v),      xf_float(v + 1U), xf_float(v + 2U),
                        xf_float(v + 3U), xf_float(v + 4U), xf_float(v + 5U)};
}

u32 gx_xf_num_texgens(void) {
    const u32 n = s_xf.mem[GX_XF_NUMTEXGENS] & 0xFU;
    return n > GX_TEXCOORD_MAX ? GX_TEXCOORD_MAX : n;
}

/* Matriz de linhas de 4 (posição/textura/pós) aplicada a (x, y, z, 1); `rows` = 2 ou 3. */
static Vec3 mul_mat4(u32 addr, Vec3 v, u32 rows) {
    f32 r[3] = {0.0F, 0.0F, 1.0F};
    for (u32 i = 0; i < rows; i++) {
        const u32 row = addr + (i * 4U);
        r[i] = (xf_float(row) * v.x) + (xf_float(row + 1U) * v.y) + (xf_float(row + 2U) * v.z) + xf_float(row + 3U);
    }
    return (Vec3){r[0], r[1], r[2]};
}

static Vec3 mul_mat3(u32 addr, const f32 *v) {
    f32 r[3];
    for (u32 i = 0; i < 3U; i++) {
        const u32 row = addr + (i * 3U);
        r[i] = (xf_float(row) * v[0]) + (xf_float(row + 1U) * v[1]) + (xf_float(row + 2U) * v[2]);
    }
    return (Vec3){r[0], r[1], r[2]};
}

static f32 dot(Vec3 a, Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static Vec3 normalized(Vec3 v) {
    const f32 len = sqrtf(dot(v, v));
    if (len == 0.0F) {
        return v; /* Common::Vec3::Normalized divide por zero; o resultado só é usado se não for zero */
    }
    return (Vec3){v.x / len, v.y / len, v.z / len};
}

static Vec3 sub(Vec3 a, Vec3 b) {
    return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static void transform_position(const GxVertex *in, GxXfVertex *out) {
    const Vec3 p = {in->pos[0], in->pos[1], in->pos[2]};
    const Vec3 mv = mul_mat4(XF_POS_MATRICES + ((u32)in->pos_mtx * 4U), p, 3U);
    out->mv[0] = mv.x;
    out->mv[1] = mv.y;
    out->mv[2] = mv.z;
    const u32 pr = GX_XF_PROJECTION;
    if ((s_xf.mem[pr + 6U] & 1U) == 0U) { /* MultipleVec3Perspective */
        out->proj[0] = (xf_float(pr) * mv.x) + (xf_float(pr + 1U) * mv.z);
        out->proj[1] = (xf_float(pr + 2U) * mv.y) + (xf_float(pr + 3U) * mv.z);
        out->proj[2] = ((xf_float(pr + 4U) * mv.z) + xf_float(pr + 5U)) * (1.0F - 1e-7F);
        out->proj[3] = -mv.z;
    } else {
        out->proj[0] = (xf_float(pr) * mv.x) + xf_float(pr + 1U);
        out->proj[1] = (xf_float(pr + 2U) * mv.y) + xf_float(pr + 3U);
        out->proj[2] = (xf_float(pr + 4U) * mv.z) + xf_float(pr + 5U);
        out->proj[3] = 1.0F;
    }
}

static void transform_normal(const GxVertex *in, GxXfVertex *out) {
    const u32 mtx = XF_NRM_MATRICES + (((u32)in->pos_mtx & 31U) * 3U);
    for (u32 n = 0; n < 3U; n++) {
        Vec3 r = mul_mat3(mtx, in->nrm[n]);
        if (n == 0U) {
            r = normalized(r); /* só a normal: as outras escalam o emboss (TransformUnit.cpp) */
        }
        out->nrm[n][0] = r.x;
        out->nrm[n][1] = r.y;
        out->nrm[n][2] = r.z;
    }
}

typedef struct Light {
    u8 color[4]; /* RGBA */
    Vec3 cosatt, distatt, pos, dir;
} Light;

static Light light(u32 n) {
    const u32 a = XF_LIGHTS + (n * XF_LIGHT_WORDS);
    const u32 c = s_xf.mem[a + 3U];
    Light l = {{(u8)(c >> 24), (u8)(c >> 16), (u8)(c >> 8), (u8)c},
               {xf_float(a + 4U), xf_float(a + 5U), xf_float(a + 6U)},
               {xf_float(a + 7U), xf_float(a + 8U), xf_float(a + 9U)},
               {xf_float(a + 10U), xf_float(a + 11U), xf_float(a + 12U)},
               {xf_float(a + 13U), xf_float(a + 14U), xf_float(a + 15U)}};
    return l;
}

static f32 safe_divide(f32 n, f32 d) {
    if (d == 0.0F) {
        return n > 0.0F ? 1.0F : 0.0F;
    }
    return n / d;
}

/* CalculateLightAttn. */
static f32 light_attn(const Light *l, Vec3 *ldir, Vec3 normal, u32 chan) {
    const u32 attnfunc = (chan >> 9) & 3U;
    if (attnfunc == ATTN_NONE || attnfunc == ATTN_DIR) {
        *ldir = normalized(*ldir);
        if (ldir->x == 0.0F && ldir->y == 0.0F && ldir->z == 0.0F) {
            *ldir = normal;
        }
        return 1.0F;
    }
    if (attnfunc == ATTN_SPEC) {
        *ldir = normalized(*ldir);
        f32 attn = dot(*ldir, normal) >= 0.0F ? fmaxf(0.0F, dot(l->dir, normal)) : 0.0F;
        const Vec3 att_len = {1.0F, attn, attn * attn};
        Vec3 dist = l->distatt;
        if (((chan >> 7) & 3U) != DIFFUSE_NONE) {
            dist = normalized(dist);
        }
        return safe_divide(fmaxf(0.0F, dot(att_len, l->cosatt)), dot(att_len, dist));
    }
    const f32 dist2 = dot(*ldir, *ldir);
    const f32 dist = sqrtf(dist2);
    *ldir = (Vec3){ldir->x / dist, ldir->y / dist, ldir->z / dist};
    const f32 attn = fmaxf(0.0F, dot(*ldir, l->dir));
    const f32 cos_att = l->cosatt.x + (l->cosatt.y * attn) + (l->cosatt.z * attn * attn);
    const f32 dist_att = l->distatt.x + (l->distatt.y * dist) + (l->distatt.z * dist2);
    return safe_divide(fmaxf(0.0F, cos_att), dist_att);
}

/* LightColor/LightAlpha: soma a contribuição da luz n em acc[0..2] (RGB) ou acc[3] (alfa). */
static void add_light(u32 n, Vec3 pos, Vec3 normal, u32 chan, f32 *acc, bool alpha) {
    const Light l = light(n);
    Vec3 ldir = sub(l.pos, pos);
    const f32 attn = light_attn(&l, &ldir, normal, chan);
    f32 dif = dot(ldir, normal);
    f32 scale = attn;
    switch ((chan >> 7) & 3U) {
    case DIFFUSE_NONE:
        break;
    case DIFFUSE_SIGN:
        scale = attn * dif;
        break;
    case DIFFUSE_CLAMP:
        dif = fmaxf(0.0F, dif);
        scale = attn * dif;
        break;
    default:
        am_fatal("XF: função difusa 3 inválida no canal 0x%08X", chan);
    }
    if (alpha) {
        acc[3] += (f32)l.color[3] * scale;
    } else {
        for (u32 i = 0; i < 3U; i++) {
            acc[i] += (f32)l.color[i] * scale;
        }
    }
}

static u32 light_mask(u32 chan) {
    return ((chan >> 1) & 1U) != 0U ? (((chan >> 2) & 0xFU) | (((chan >> 11) & 0xFU) << 4)) : 0U;
}

static u8 lit(u8 mat, f32 light_value) {
    s32 l = (s32)light_value;
    l = gx_clamp_s32(l, 0, 255);
    return (u8)(((u32)mat * (u32)(l + (l >> 7))) >> 8);
}

/* Um canal de cor (cor + alfa): material e ambiente do registrador ou do vértice, luzes da máscara. */
static void transform_channel(const GxVertex *in, const GxXfVertex *out, u32 c, u8 *result) {
    const u32 cchan = s_xf.mem[GX_XF_COLOR0 + c];
    const u32 achan = s_xf.mem[GX_XF_ALPHA0 + c];
    const u32 matreg = s_xf.mem[GX_XF_MATCOLOR0 + c];
    const u32 ambreg = s_xf.mem[GX_XF_AMBCOLOR0 + c];
    const u8 reg_mat[4] = {(u8)(matreg >> 24), (u8)(matreg >> 16), (u8)(matreg >> 8), (u8)matreg};
    const u8 reg_amb[4] = {(u8)(ambreg >> 24), (u8)(ambreg >> 16), (u8)(ambreg >> 8), (u8)ambreg};
    const Vec3 pos = {out->mv[0], out->mv[1], out->mv[2]};
    const Vec3 normal = {out->nrm[0][0], out->nrm[0][1], out->nrm[0][2]};
    for (u32 part = 0; part < 2U; part++) { /* 0: RGB pelo canal de cor, 1: alfa pelo canal de alfa */
        const u32 chan = part == 0U ? cchan : achan;
        const u8 *mat = (chan & 1U) != 0U ? in->color[c] : reg_mat;
        const u8 *amb = ((chan >> 6) & 1U) != 0U ? in->color[c] : reg_amb;
        const u32 first = part == 0U ? 0U : 3U;
        const u32 last = part == 0U ? 3U : 4U;
        if (((chan >> 1) & 1U) == 0U) {
            for (u32 i = first; i < last; i++) {
                result[i] = mat[i];
            }
            continue;
        }
        f32 acc[4] = {(f32)amb[0], (f32)amb[1], (f32)amb[2], (f32)amb[3]};
        const u32 mask = light_mask(chan);
        for (u32 n = 0; n < 8U; n++) {
            if ((mask & (1U << n)) != 0U) {
                add_light(n, pos, normal, chan, acc, part == 1U);
            }
        }
        for (u32 i = first; i < last; i++) {
            result[i] = lit(mat[i], acc[i]);
        }
    }
}

static Vec3 texgen_source(const GxVertex *in, u32 info, u32 n) {
    const u32 row = (info >> 7) & 31U;
    Vec3 src;
    switch (row) {
    case 0U:
        src = (Vec3){in->pos[0], in->pos[1], in->pos[2]};
        break;
    case 1U:
    case 3U:
    case 4U: {
        const u32 k = row == 1U ? 0U : row - 2U;
        src = (Vec3){in->nrm[k][0], in->nrm[k][1], in->nrm[k][2]};
        break;
    }
    default:
        if (row < 5U || row > 12U) {
            am_fatal("XF: linha de origem %u inválida no texgen %u (0x%08X)", row, n, info);
        }
        src = (Vec3){in->tex[row - 5U][0], in->tex[row - 5U][1], 1.0F};
        break;
    }
    src.x = isnan(src.x) ? 1.0F : src.x; /* Shadow the Hedgehog (TransformUnit.cpp) */
    src.y = isnan(src.y) ? 1.0F : src.y;
    src.z = isnan(src.z) ? 1.0F : src.z;
    return src;
}

/* TransformTexCoordRegular. */
static void texgen_regular(const GxVertex *in, GxXfVertex *out, u32 info, u32 n) {
    Vec3 src = texgen_source(in, info, n);
    if (((info >> 2) & 1U) == 0U) { /* AB11: MultiplyVec2Mat24/34 somam m2 + m3, como se z = 1 */
        src.z = 1.0F;
    }
    const u32 mtx = (u32)in->tex_mtx[n] * 4U;
    Vec3 dst = mul_mat4(mtx, src, ((info >> 1) & 1U) != 0U ? 3U : 2U); /* ST: q = 1 */
    if ((s_xf.mem[GX_XF_DUALTEX] & 1U) != 0U) {
        const u32 post = s_xf.mem[GX_XF_POSTMTXINFO + n];
        const Vec3 t = ((post >> 8) & 1U) != 0U ? normalized(dst) : dst;
        dst = mul_mat4(XF_POST_MATRICES + ((post & 63U) * 4U), t, 3U);
    }
    if (dst.z == 0.0F) { /* q = 0: caso especial do GameCube (neheGX Lesson08) */
        dst.x = fminf(fmaxf(dst.x / 2.0F, -1.0F), 1.0F);
        dst.y = fminf(fmaxf(dst.y / 2.0F, -1.0F), 1.0F);
    }
    out->tex[n][0] = dst.x;
    out->tex[n][1] = dst.y;
    out->tex[n][2] = dst.z;
}

static void transform_texcoords(const GxVertex *in, GxXfVertex *out) {
    const u32 count = gx_xf_num_texgens();
    for (u32 n = 0; n < count; n++) {
        const u32 info = s_xf.mem[GX_XF_TEXMTXINFO + n];
        switch ((info >> 4) & 7U) {
        case TEXGEN_REGULAR:
            texgen_regular(in, out, info, n);
            break;
        case TEXGEN_EMBOSS: {
            const Light l = light((info >> 15) & 7U);
            const Vec3 pos = {out->mv[0], out->mv[1], out->mv[2]};
            const Vec3 ldir = normalized(sub(l.pos, pos));
            const Vec3 t = {out->nrm[1][0], out->nrm[1][1], out->nrm[1][2]};
            const Vec3 b = {out->nrm[2][0], out->nrm[2][1], out->nrm[2][2]};
            const u32 src = (info >> 12) & 7U;
            out->tex[n][0] = out->tex[src][0] + dot(ldir, t);
            out->tex[n][1] = out->tex[src][1] + dot(ldir, b);
            out->tex[n][2] = out->tex[src][2];
            break;
        }
        case TEXGEN_COLOR0:
        case TEXGEN_COLOR1: {
            const u32 c = ((info >> 4) & 7U) - TEXGEN_COLOR0;
            out->tex[n][0] = (f32)out->color[c][0] / 255.0F;
            out->tex[n][1] = (f32)out->color[c][1] / 255.0F;
            out->tex[n][2] = 1.0F;
            break;
        }
        default:
            am_fatal("XF: tipo de texgen %u inválido (0x%08X)", (info >> 4) & 7U, info);
        }
    }
}

void gx_xf_transform(const GxVertex *in, GxXfVertex *out) {
    memset(out, 0, sizeof(*out));
    transform_position(in, out);
    transform_normal(in, out);
    transform_channel(in, out, 0, out->color[0]);
    transform_channel(in, out, 1, out->color[1]);
    transform_texcoords(in, out);
}

void gx_xf_perspective_divide(GxXfVertex *v) {
    const GxViewport vp = gx_xf_viewport();
    const f32 inv_w = 1.0F / v->proj[3];
    v->screen[0] = (v->proj[0] * inv_w * vp.wd) + vp.x_orig;
    v->screen[1] = (v->proj[1] * inv_w * vp.ht) + vp.y_orig;
    v->screen[2] = (v->proj[2] * inv_w * vp.z_range) + vp.far_z;
}
