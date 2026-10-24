/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Montagem de primitivas e recorte. Tradução para C de Dolphin VideoBackends/Software/SetupUnit.cpp e Clipper.cpp
 * (GPL-2.0-or-later, Dolphin Emulator Project; partes de Markus Trenkwalder, licença BSD citada no original). */
#include "am/gx_setup.h"

#include <math.h>
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/gx_raster.h"
#include "am/gx_tev.h"
#include "am/gx_tex.h"
#include "am/gx_vtx.h"
#include "am/gx_xf.h"
#include "am/log.h"

#define CLIPPED_MAX 33U
#define INDICES_MAX (CLIPPED_MAX + 3U)
#define POLY_MAX    ((2U * 6U) + 1U)
#define SKIP        (-1)
#define CLIP_POS_X  0x01U
#define CLIP_NEG_X  0x02U
#define CLIP_POS_Y  0x04U
#define CLIP_NEG_Y  0x08U
#define CLIP_POS_Z  0x10U
#define CLIP_NEG_Z  0x20U
#define CULL_BACK   1U
#define CULL_FRONT  2U
#define CULL_ALL    3U

static struct {
    u32 primitive;
    u32 vat;
    u32 vertex_size;
    u32 counter;
    GxXfVertex verts[3];
    GxXfVertex *vp[3];
    GxXfVertex *write;
    f32 tex_scale[GX_TEXCOORD_MAX][2];
    u32 texgens;
    GxXfVertex clipped[CLIPPED_MAX];
    GxXfVertex *list[INDICES_MAX];
    GxSetupStats stats;
} s_setup;

void gx_setup_reset(void) {
    memset(&s_setup, 0, sizeof(s_setup));
}

const GxSetupStats *gx_setup_stats(void) {
    return &s_setup.stats;
}

/* Contadores de recursos por desenho (relatório do fim da execução). */
static void note_features(u32 gen, u32 stages) {
    GxSetupStats *st = &s_setup.stats;
    for (u32 i = 0; i < stages; i++) {
        const u32 order = gx_bp_reg((u8)(GX_BP_TREF + (i >> 1)));
        const u32 o = (i & 1U) != 0U ? order >> 12 : order;
        if (((o >> 6) & 1U) != 0U) {
            const u32 format = (gx_bp_tex_reg(GX_BP_TX_SETIMAGE0, o & 7U) >> 20) & 0xFU;
            st->tex_formats |= 1U << format;
            if (format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2) {
                st->tlut_formats |= 1U << ((gx_bp_tex_reg(GX_BP_TX_SETTLUT, o & 7U) >> 10) & 3U);
            }
        }
    }
    const u32 blend = gx_bp_reg(GX_BP_BLENDMODE);
    st->blend_draws += (blend & 1U) != 0U ? 1U : 0U;
    st->logic_draws += (blend & 2U) != 0U ? 1U : 0U;
    st->subtract_draws += (blend & 0x800U) != 0U ? 1U : 0U;
    const u32 at = gx_bp_reg(GX_BP_ALPHACOMPARE);
    st->alpha_tests |= (1U << ((at >> 16) & 7U)) | (1U << ((at >> 19) & 7U));
    st->fog_types |= 1U << ((gx_bp_reg(GX_BP_FOGPARAM3) >> 21) & 7U);
    st->stage_counts |= 1U << (stages - 1U);
    const u32 texgens = gen & 15U;
    st->max_texgens = texgens > st->max_texgens ? texgens : st->max_texgens;
}

void gx_setup_begin(u32 primitive, u32 vat) {
    gx_raster_flush(); /* a fila do desenho anterior usa o estado de antes (normalmente já vazia: gx_setup_end) */
    s_setup.primitive = primitive;
    s_setup.vat = vat;
    s_setup.vertex_size = gx_vtx_size(vat);
    s_setup.counter = 0;
    for (u32 i = 0; i < 3U; i++) {
        s_setup.vp[i] = &s_setup.verts[i];
    }
    s_setup.write = s_setup.vp[0];
    s_setup.stats.draws++;
    const u32 gen = gx_bp_reg(GX_BP_GENMODE);
    const u32 stages = ((gen >> 10) & 15U) + 1U;
    s_setup.stats.max_stages = stages > s_setup.stats.max_stages ? stages : s_setup.stats.max_stages;
    s_setup.stats.indirect_draws += ((gen >> 16) & 7U) != 0U ? 1U : 0U;
    s_setup.stats.ztex_draws += ((gx_bp_reg(GX_BP_ZTEX2) >> 2) & 3U) != 0U ? 1U : 0U;
    s_setup.stats.fog_draws += ((gx_bp_reg(GX_BP_FOGPARAM3) >> 21) & 7U) != 0U ? 1U : 0U;
    note_features(gen, stages);
    s_setup.texgens = gx_xf_num_texgens();
    for (u32 n = 0; n < GX_TEXCOORD_MAX; n++) { /* TCInfo.scale_minus_1 + 1 (SU_SSIZE/TSIZE) */
        s_setup.tex_scale[n][0] = (f32)((gx_bp_reg((u8)(GX_BP_SU_SSIZE + (2U * n))) & 0xFFFFU) + 1U);
        s_setup.tex_scale[n][1] = (f32)((gx_bp_reg((u8)(GX_BP_SU_SSIZE + (2U * n) + 1U)) & 0xFFFFU) + 1U);
    }
    gx_tex_prepare();
    gx_tev_begin_draw();
    gx_efb_begin_draw();
    gx_raster_begin_draw();
}

/* OutputVertexData::Lerp: floats linearmente, cores em ponto fixo de 8 bits. */
static void lerp(f32 t, const GxXfVertex *a, const GxXfVertex *b, GxXfVertex *out) {
    for (u32 i = 0; i < 3U; i++) {
        out->mv[i] = a->mv[i] + ((b->mv[i] - a->mv[i]) * t);
    }
    for (u32 i = 0; i < 4U; i++) {
        out->proj[i] = a->proj[i] + ((b->proj[i] - a->proj[i]) * t);
    }
    for (u32 n = 0; n < 3U; n++) {
        for (u32 i = 0; i < 3U; i++) {
            out->nrm[n][i] = a->nrm[n][i] + ((b->nrm[n][i] - a->nrm[n][i]) * t);
        }
    }
    const s32 ti = (s32)(u16)(t * 256.0F);
    for (u32 c = 0; c < 2U; c++) {
        for (u32 i = 0; i < 4U; i++) {
            const s32 va = a->color[c][i];
            out->color[c][i] = (u8)(va + (((b->color[c][i] - va) * ti) >> 8));
        }
    }
    for (u32 n = 0; n < GX_TEXCOORD_MAX; n++) {
        for (u32 i = 0; i < 3U; i++) {
            out->tex[n][i] = a->tex[n][i] + ((b->tex[n][i] - a->tex[n][i]) * t);
        }
    }
}

static u32 clip_mask(const GxXfVertex *v) {
    const f32 *p = v->proj;
    u32 m = 0;
    m |= p[3] - p[0] < 0.0F ? CLIP_POS_X : 0U;
    m |= p[0] + p[3] < 0.0F ? CLIP_NEG_X : 0U;
    m |= p[3] - p[1] < 0.0F ? CLIP_POS_Y : 0U;
    m |= p[1] + p[3] < 0.0F ? CLIP_NEG_Y : 0U;
    m |= p[3] * p[2] > 0.0F ? CLIP_POS_Z : 0U;
    m |= p[2] + p[3] < 0.0F ? CLIP_NEG_Z : 0U;
    return m;
}

static f32 plane_dot(u32 i, const f32 *plane) {
    const f32 *p = s_setup.list[i]->proj;
    return (p[0] * plane[0]) + (p[1] * plane[1]) + (p[2] * plane[2]) + (p[3] * plane[3]);
}

static bool different_signs(f32 x, f32 y) {
    return (x <= 0.0F && y > 0.0F) || (x > 0.0F && y <= 0.0F);
}

/* POLY_CLIP de um plano: devolve false se o polígono sumiu (menos de 3 vértices). */
static bool clip_plane(const f32 *plane, s32 **in, s32 **out, u32 *n, u32 *count) {
    s32 *inl = *in;
    s32 *outl = *out;
    s32 prev = inl[0];
    f32 dprev = plane_dot((u32)prev, plane);
    u32 outc = 0;
    inl[*n] = inl[0];
    for (u32 j = 1; j <= *n; j++) {
        const s32 idx = inl[j];
        const f32 dp = plane_dot((u32)idx, plane);
        if (dprev >= 0.0F) {
            outl[outc++] = prev;
        }
        if (different_signs(dp, dprev)) {
            AM_ASSERT(*count < INDICES_MAX, "recorte: vértices demais");
            if (dp < 0.0F) {
                lerp(dp / (dp - dprev), s_setup.list[idx], s_setup.list[prev], s_setup.list[*count]);
            } else {
                lerp(dprev / (dprev - dp), s_setup.list[prev], s_setup.list[idx], s_setup.list[*count]);
            }
            outl[outc++] = (s32)(*count)++;
        }
        prev = idx;
        dprev = dp;
    }
    if (outc < 3U) {
        return false;
    }
    *in = outl;
    *out = inl;
    *n = outc;
    return true;
}

/* ClipTriangle: índices de triângulos (leque) em `indices`; devolve quantos índices. */
static u32 clip_triangle(s32 *indices) {
    const u32 mask = clip_mask(s_setup.list[0]) | clip_mask(s_setup.list[1]) | clip_mask(s_setup.list[2]);
    if (mask == 0U) {
        return 3U;
    }
    static const f32 s_planes[6][4] = {{-1, 0, 0, 1}, {1, 0, 0, 1}, {0, -1, 0, 1},
                                       {0, 1, 0, 1},  {0, 0, 0, 1}, {0, 0, 1, 1}};
    s32 vlist[2][POLY_MAX + 1U];
    s32 *in = vlist[0];
    s32 *out = vlist[1];
    u32 n = 3;
    u32 count = 3;
    in[0] = 0;
    in[1] = 1;
    in[2] = 2;
    for (u32 p = 0; p < 6U; p++) {
        if ((mask & (1U << p)) != 0U && !clip_plane(s_planes[p], &in, &out, &n, &count)) {
            return 0U;
        }
    }
    u32 num = 3;
    indices[0] = in[0];
    indices[1] = in[1];
    indices[2] = in[2];
    for (u32 j = 3; j < n; j++) {
        indices[num++] = in[0];
        indices[num++] = in[j - 1U];
        indices[num++] = in[j];
    }
    return num;
}

static bool is_backface(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2) {
    const f32 *a = v0->proj;
    const f32 *b = v1->proj;
    const f32 *c = v2->proj;
    /* x0 w2 - x2 w0 ... com x = [0], y = [1], w = [3] */
    const f32 nz = (((a[0] * c[3]) - (c[0] * a[3])) * b[1]) + (((c[0] * a[1]) - (a[0] * c[1])) * b[3]) +
                   (((c[1] * a[3]) - (a[1] * c[3])) * b[0]);
    bool back = nz <= 0.0F;
    if (gx_xf_viewport().ht > 0.0F) { /* viewport positivo inverte o teste (Jimmie Johnson's) */
        back = !back;
    }
    return back;
}

static void process_triangle(GxXfVertex *v0, GxXfVertex *v1, GxXfVertex *v2) {
    if ((clip_mask(v0) & clip_mask(v1) & clip_mask(v2)) != 0U) {
        s_setup.stats.rejected++;
        return;
    }
    const bool back = is_backface(v0, v1, v2);
    const u32 cull = (gx_bp_reg(GX_BP_GENMODE) >> 14) & 3U;
    if ((!back && (cull == CULL_BACK || cull == CULL_ALL)) || (back && (cull == CULL_FRONT || cull == CULL_ALL))) {
        gx_xf_perspective_divide(v0);
        gx_xf_perspective_divide(v1);
        gx_xf_perspective_divide(v2);
        if (back) {
            gx_raster_update_z_slope(v0, v2, v1);
        } else {
            gx_raster_update_z_slope(v0, v1, v2);
        }
        s_setup.stats.culled++;
        return;
    }
    s_setup.list[0] = v0;
    s_setup.list[1] = back ? v2 : v1;
    s_setup.list[2] = back ? v1 : v2;
    for (u32 i = 0; i < CLIPPED_MAX; i++) {
        s_setup.list[i + 3U] = &s_setup.clipped[i];
    }
    s32 indices[INDICES_MAX * 3U];
    u32 num = 3;
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    const bool skip_clip = (gx_xf_read(GX_XF_CLIPDISABLE) & 1U) != 0U && s_setup.list[0]->proj[3] >= 0.0F &&
                           s_setup.list[1]->proj[3] >= 0.0F && s_setup.list[2]->proj[3] >= 0.0F;
    if (!skip_clip) {
        num = clip_triangle(indices);
    }
    for (u32 i = 0; i + 3U <= num; i += 3U) {
        GxXfVertex *a = s_setup.list[indices[i]];
        GxXfVertex *b = s_setup.list[indices[i + 1U]];
        GxXfVertex *c = s_setup.list[indices[i + 2U]];
        gx_xf_perspective_divide(a);
        gx_xf_perspective_divide(b);
        gx_xf_perspective_divide(c);
        gx_raster_triangle(a, b, c);
    }
}

/* Deslocamentos de texcoord de linhas e pontos (Clipper.cpp LINE_PT_TEX_OFFSETS). */
static const f32 s_lp_tex_offsets[8] = {0.0F, 1.0F / 16.0F, 1.0F / 8.0F, 1.0F / 4.0F, 1.0F / 2.0F, 1.0F, 1.0F, 1.0F};

static void line_vertex(GxXfVertex *dst, const GxXfVertex *src, s32 px, s32 py, bool offset) {
    const u32 lp = gx_bp_reg(GX_BP_LINEPTWIDTH);
    const f32 half = (f32)(lp & 0xFFU) / 12.0F;
    *dst = *src;
    dst->screen[0] = src->screen[0] + ((f32)px * half);
    dst->screen[1] = src->screen[1] + ((f32)py * half);
    const f32 off = s_lp_tex_offsets[(lp >> 16) & 7U];
    if (offset && off != 0.0F) {
        for (u32 n = 0; n < s_setup.texgens; n++) {
            if (((gx_bp_reg((u8)(GX_BP_SU_SSIZE + (2U * n))) >> 18) & 1U) != 0U) {
                dst->tex[n][0] += s_setup.tex_scale[n][0] * off;
            }
        }
    }
}

static void process_line(GxXfVertex *a, GxXfVertex *b) {
    static const f32 s_planes[6][4] = {{-1, 0, 0, 1}, {1, 0, 0, 1},  {0, -1, 0, 1},
                                       {0, 1, 0, 1},  {0, 0, -1, 1}, {0, 0, 1, 1}};
    const u32 m0 = clip_mask(a);
    const u32 m1 = clip_mask(b);
    GxXfVertex ca = *a;
    GxXfVertex cb = *b;
    if ((m0 | m1) != 0U) {
        f32 t0 = 0.0F;
        f32 t1 = 0.0F;
        for (u32 p = 0; p < 6U; p++) {
            if (((m0 | m1) & (1U << p)) == 0U) {
                continue;
            }
            const f32 *pl = s_planes[p];
            const f32 d0 = (a->proj[0] * pl[0]) + (a->proj[1] * pl[1]) + (a->proj[2] * pl[2]) + (a->proj[3] * pl[3]);
            const f32 d1 = (b->proj[0] * pl[0]) + (b->proj[1] * pl[1]) + (b->proj[2] * pl[2]) + (b->proj[3] * pl[3]);
            if (d0 < 0.0F && d1 < 0.0F) {
                return;
            }
            if (d1 < 0.0F) {
                t1 = fmaxf(t1, d1 / (d1 - d0));
            } else if (d0 < 0.0F) {
                t0 = fmaxf(t0, d0 / (d0 - d1));
            }
        }
        if (m0 != 0U) {
            lerp(t0, a, b, &ca);
        }
        if (m1 != 0U) {
            lerp(t1, b, a, &cb);
        }
    }
    gx_xf_perspective_divide(&ca);
    gx_xf_perspective_divide(&cb);
    const f32 dx = cb.screen[0] - ca.screen[0];
    const f32 dy = cb.screen[1] - ca.screen[1];
    s32 px = 0;
    s32 py = 0;
    if (fabsf(dx) > fabsf(dy)) { /* pontas verticais ou horizontais conforme a inclinação */
        py = dx > 0.0F ? -1 : 1;
    } else {
        px = dy > 0.0F ? 1 : -1;
    }
    GxXfVertex tri[3];
    line_vertex(&tri[0], &ca, px, py, false);
    line_vertex(&tri[1], &cb, px, py, false);
    line_vertex(&tri[2], &cb, -px, -py, true);
    gx_raster_triangle(&tri[2], &tri[1], &tri[0]);
    line_vertex(&tri[1], &ca, -px, -py, true);
    gx_raster_triangle(&tri[0], &tri[1], &tri[2]);
}

static void point_vertex(GxXfVertex *dst, const GxXfVertex *src, bool px, bool py) {
    const u32 lp = gx_bp_reg(GX_BP_LINEPTWIDTH);
    const f32 radius = (f32)((lp >> 8) & 0xFFU) / 12.0F;
    *dst = *src;
    dst->screen[0] = src->screen[0] + ((px ? 1.0F : -1.0F) * radius);
    dst->screen[1] = src->screen[1] + ((py ? 1.0F : -1.0F) * radius);
    const f32 off = s_lp_tex_offsets[(lp >> 19) & 7U];
    for (u32 n = 0; n < s_setup.texgens && off != 0.0F; n++) {
        if (((gx_bp_reg((u8)(GX_BP_SU_SSIZE + (2U * n))) >> 19) & 1U) != 0U) {
            dst->tex[n][0] += px ? s_setup.tex_scale[n][0] * off : 0.0F;
            dst->tex[n][1] += py ? s_setup.tex_scale[n][1] * off : 0.0F;
        }
    }
}

static void process_point(GxXfVertex *c) {
    gx_xf_perspective_divide(c); /* sem recorte, como no Dolphin */
    GxXfVertex ll;
    GxXfVertex lr;
    GxXfVertex ul;
    GxXfVertex ur;
    point_vertex(&ll, c, false, false);
    point_vertex(&lr, c, true, false);
    point_vertex(&ur, c, true, true);
    point_vertex(&ul, c, false, true);
    gx_raster_triangle(&ll, &ul, &lr);
    gx_raster_triangle(&ur, &lr, &ul);
}

/* SetupUnit: as primitivas com estado entre vértices (faixas, leques, quads) trocam os ponteiros como no Dolphin. */
static void setup_vertex(void) {
    GxXfVertex **vp = s_setup.vp;
    const u32 prim = s_setup.primitive;
    if (prim == GX_PRIM_POINTS) {
        process_point(vp[0]);
        return;
    }
    const u32 needed = (prim == GX_PRIM_LINES || prim == GX_PRIM_LINE_STRIP) ? 1U : 2U;
    if (s_setup.counter < needed) {
        s_setup.counter++;
        s_setup.write = vp[s_setup.counter];
        return;
    }
    switch (prim) {
    case GX_PRIM_QUADS:
    case GX_PRIM_QUADS_2: {
        process_triangle(vp[0], vp[1], vp[2]);
        s_setup.counter = (s_setup.counter + 1U) & 3U;
        s_setup.write = &s_setup.verts[s_setup.counter & 1U];
        GxXfVertex *tmp = vp[1];
        vp[1] = vp[2];
        vp[2] = tmp;
        break;
    }
    case GX_PRIM_TRIANGLES:
        process_triangle(vp[0], vp[1], vp[2]);
        s_setup.counter = 0;
        s_setup.write = vp[0];
        break;
    case GX_PRIM_TRI_STRIP:
        process_triangle(vp[0], vp[1], vp[2]);
        s_setup.counter++;
        vp[2U - (s_setup.counter & 1U)] = vp[0];
        s_setup.write = vp[0];
        vp[0] = &s_setup.verts[(s_setup.counter + 1U) % 3U];
        break;
    case GX_PRIM_TRI_FAN:
        process_triangle(vp[0], vp[1], vp[2]);
        s_setup.counter++;
        vp[1] = vp[2];
        vp[2] = &s_setup.verts[2U - (s_setup.counter & 1U)];
        s_setup.write = vp[2];
        break;
    case GX_PRIM_LINES:
        process_line(vp[0], vp[1]);
        s_setup.counter = 0;
        s_setup.write = vp[0];
        break;
    default: /* GX_PRIM_LINE_STRIP */
        s_setup.counter++;
        process_line(vp[0], vp[1]);
        s_setup.write = vp[0];
        vp[0] = vp[1];
        vp[1] = &s_setup.verts[s_setup.counter & 1U];
        break;
    }
}

void gx_setup_end(void) {
    gx_raster_flush();
}

void gx_setup_vertex(const u8 *data) {
    GxVertex in;
    memset(&in, 0, sizeof(in));
    gx_xf_default_matrices(&in);
    gx_vtx_decode(s_setup.vat, data, &in);
    s_setup.stats.vertices++;
    if (in.skip) {
        return; /* índice de posição "nulo": o vértice sai da primitiva (VertexLoader m_skippedVertices) */
    }
    GxXfVertex *out = s_setup.write;
    gx_xf_transform(&in, out);
    for (u32 n = 0; n < s_setup.texgens; n++) {
        out->tex[n][0] *= s_setup.tex_scale[n][0];
        out->tex[n][1] *= s_setup.tex_scale[n][1];
    }
    setup_vertex();
}
