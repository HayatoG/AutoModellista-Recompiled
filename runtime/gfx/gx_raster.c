/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Rasterizador. Tradução para C de Dolphin VideoBackends/Software/Rasterizer.cpp (GPL-2.0-or-later, Dolphin
 * Emulator Project; partes de Markus Trenkwalder, licença BSD citada no original). */
#include "am/gx_raster.h"

#include <math.h>
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/gx_tev.h"
#include "am/gx_util.h"
#include "am/log.h"

#define BLOCK      2
#define MAX_RECTS  81 /* 9 faixas em x por 9 em y (ComputeScissorRanges) */
#define FILTER_LIN 1U

typedef struct Rect {
    s32 x_off, y_off;
    s32 left, top, right, bottom; /* [left, right) x [top, bottom) no EFB */
} Rect;

typedef struct SlopeCtx {
    s32 x0, y0;
    f32 x_off, y_off;
    f32 dx10, dx20, dy10, dy20;
} SlopeCtx;

typedef struct Slope {
    f32 dfdx, dfdy, f0;
    s32 x0, y0;
    f32 x_off, y_off;
} Slope;

typedef struct BlockPixel {
    f32 uv[GX_TEXCOORD_MAX][2];
} BlockPixel;

static struct {
    Rect rects[MAX_RECTS];
    u32 rect_count;
    u32 texgens;
    u32 colchans;
    u32 stages;
    u32 ind_stages;
    bool zfreeze;
    f32 tex_scale[GX_TEXCOORD_MAX][2];
    Slope z, w;
    Slope color[2][4];
    Slope tex[GX_TEXCOORD_MAX][3];
    BlockPixel block[BLOCK][BLOCK];
    s32 lod[GX_TEV_STAGES];
    bool linear[GX_TEV_STAGES];
    s32 ind_lod[4];
    bool ind_linear[4];
    GxRasterStats stats;
} s_ras;

void gx_raster_reset(void) {
    memset(&s_ras, 0, sizeof(s_ras));
    s_ras.z.f0 = 1.0F; /* valor padrão "chutado" do Dolphin para zfreeze no primeiro desenho */
}

const GxRasterStats *gx_raster_stats(void) {
    return &s_ras.stats;
}

/* ComputeScissorRanges: o deslocamento pode dar a volta em 1024. */
static u32 scissor_ranges(s32 start, s32 end, s32 offset, s32 dim, s32 (*out)[3]) {
    u32 n = 0;
    for (s32 extra = -4096; extra <= 4096; extra += 1024) {
        const s32 off = offset + extra;
        const s32 s = gx_clamp_s32(start - off, 0, dim);
        const s32 e = gx_clamp_s32(end - off + 1, 0, dim);
        if (s < e) {
            out[n][0] = off;
            out[n][1] = s;
            out[n][2] = e;
            n++;
        }
    }
    return n;
}

static void compute_scissor(void) {
    const u32 tl = gx_bp_reg(GX_BP_SCISSORTL);
    const u32 br = gx_bp_reg(GX_BP_SCISSORBR);
    const u32 off = gx_bp_reg(GX_BP_SCISSOROFFSET);
    const s32 left = (s32)((tl >> 12) & 0x7FFU);
    const s32 top = (s32)(tl & 0x7FFU);
    const s32 right = (s32)((br >> 12) & 0x7FFU);
    const s32 bottom = (s32)(br & 0x7FFU);
    s_ras.rect_count = 0;
    if (left > right || top > bottom) {
        return;
    }
    s32 xr[9][3];
    s32 yr[9][3];
    const u32 nx = scissor_ranges(left, right, (s32)((off & 0x1FFU) << 1), (s32)GX_EFB_WIDTH, xr);
    const u32 ny = scissor_ranges(top, bottom, (s32)(((off >> 10) & 0x1FFU) << 1), (s32)GX_EFB_HEIGHT, yr);
    for (u32 i = 0; i < nx; i++) {
        for (u32 j = 0; j < ny; j++) {
            s_ras.rects[s_ras.rect_count++] = (Rect){xr[i][0], yr[j][0], xr[i][1], yr[j][1], xr[i][2], yr[j][2]};
        }
    }
}

void gx_raster_begin_draw(void) {
    const u32 gen = gx_bp_reg(GX_BP_GENMODE);
    s_ras.texgens = gen & 15U;
    s_ras.texgens = s_ras.texgens > GX_TEXCOORD_MAX ? GX_TEXCOORD_MAX : s_ras.texgens;
    s_ras.colchans = (gen >> 4) & 7U;
    s_ras.colchans = s_ras.colchans > 2U ? 2U : s_ras.colchans;
    s_ras.stages = ((gen >> 10) & 15U) + 1U;
    s_ras.ind_stages = (gen >> 16) & 7U;
    s_ras.ind_stages = s_ras.ind_stages > 4U ? 4U : s_ras.ind_stages;
    s_ras.zfreeze = ((gen >> 19) & 1U) != 0U;
    compute_scissor();
}

static s32 iround(f32 x) {
    const s32 t = (s32)x;
    return (x - (f32)t) >= 0.5F ? t + 1 : t;
}

static SlopeCtx slope_ctx(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2, s32 x0, s32 y0, s32 x_off,
                          s32 y_off) {
    const f32 adjust = 0.495F; /* um pouco menos que 0,5 */
    return (SlopeCtx){x0,
                      y0,
                      ((f32)x0 - (v0->screen[0] - (f32)x_off)) + adjust,
                      ((f32)y0 - (v0->screen[1] - (f32)y_off)) + adjust,
                      v1->screen[0] - v0->screen[0],
                      v2->screen[0] - v0->screen[0],
                      v1->screen[1] - v0->screen[1],
                      v2->screen[1] - v0->screen[1]};
}

static Slope slope(f32 f0, f32 f1, f32 f2, const SlopeCtx *c) {
    const f32 d20 = f2 - f0;
    const f32 d10 = f1 - f0;
    const f32 a = (d20 * c->dy10) - (d10 * c->dy20);
    const f32 b = (c->dx20 * d10) - (c->dx10 * d20);
    const f32 den = (c->dx20 * c->dy10) - (c->dx10 * c->dy20);
    return (Slope){a / den, b / den, f0, c->x0, c->y0, c->x_off, c->y_off};
}

static f32 slope_value(const Slope *s, s32 x, s32 y) {
    const f32 dx = s->x_off + (f32)(x - s->x0);
    const f32 dy = s->y_off + (f32)(y - s->y0);
    return s->f0 + (s->dfdx * dx) + (s->dfdy * dy);
}

static void update_z_slope(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2, s32 x_off, s32 y_off) {
    if (s_ras.zfreeze) {
        return;
    }
    const s32 x1 = iround(16.0F * (v0->screen[0] - (f32)x_off)) - 9;
    const s32 y1 = iround(16.0F * (v0->screen[1] - (f32)y_off)) - 9;
    const SlopeCtx c = slope_ctx(v0, v1, v2, (x1 + 0xF) >> 4, (y1 + 0xF) >> 4, x_off, y_off);
    s_ras.z = slope(v0->screen[2], v1->screen[2], v2->screen[2], &c);
}

void gx_raster_update_z_slope(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2) {
    const u32 off = gx_bp_reg(GX_BP_SCISSOROFFSET);
    update_z_slope(v0, v1, v2, (s32)(off & 0x1FFU) * 2, (s32)((off >> 10) & 0x1FFU) * 2);
}

/* FixedLog2: log2 aproximado em s28.4, suficiente para LOD. */
static s32 fixed_log2(f32 f) {
    u32 x;
    memcpy(&x, &f, sizeof(x));
    return (s32)((x & 0x7F800000U) >> 19) - 2032 + (s32)((x & 0x007FFFFFU) >> 19);
}

static void calc_lod(u32 texmap, u32 texcoord, s32 *lod_out, bool *linear) {
    const u32 m0 = gx_bp_tex_reg(GX_BP_TX_SETMODE0, texmap);
    const u32 m1 = gx_bp_tex_reg(GX_BP_TX_SETMODE1, texmap);
    const f32 *uv00 = s_ras.block[0][0].uv[texcoord];
    const f32 *uv10 = s_ras.block[1][0].uv[texcoord];
    const f32 *uv01 = s_ras.block[0][1].uv[texcoord];
    const f32 dudx = fabsf(uv00[0] - uv10[0]);
    const f32 dvdx = fabsf(uv00[1] - uv10[1]);
    const f32 dudy = fabsf(uv00[0] - uv01[0]);
    const f32 dvdy = fabsf(uv00[1] - uv01[1]);
    f32 sd;
    f32 td;
    if (((m0 >> 8) & 1U) != 0U) { /* diag_lod: 1 = diagonal, 0 = aresta */
        sd = dudx + dudy;
        td = dvdx + dvdy;
    } else {
        sd = fmaxf(dudx, dudy);
        td = fmaxf(dvdx, dvdy);
    }
    s32 lod = fixed_log2(fmaxf(sd, td));
    lod += ((s32)(m0 << 15) >> 24) >> 1; /* bias s2.5 (bits 9-16, com sinal) -> s28.4 */
    *linear = (lod > 0 && ((m0 >> 7) & 1U) == FILTER_LIN) || (lod <= 0 && ((m0 >> 4) & 1U) == FILTER_LIN);
    const s32 max_lod = (s32)((m1 >> 8) & 0xFFU);
    const s32 min_lod = (s32)(m1 & 0xFFU);
    if (lod > max_lod) { /* a ordem dos testes importa (Rasterizer.cpp) */
        lod = max_lod;
    } else if (lod < min_lod) {
        lod = min_lod;
    }
    *lod_out = lod;
}

static void build_block(s32 bx, s32 by) {
    for (s32 yi = 0; yi < BLOCK; yi++) {
        for (s32 xi = 0; xi < BLOCK; xi++) {
            BlockPixel *px = &s_ras.block[xi][yi];
            const s32 x = bx + xi;
            const s32 y = by + yi;
            const f32 inv_w = 1.0F / slope_value(&s_ras.w, x, y);
            for (u32 i = 0; i < s_ras.texgens; i++) {
                f32 proj = inv_w;
                const f32 q = slope_value(&s_ras.tex[i][2], x, y) * inv_w;
                if (q != 0.0F) {
                    proj = inv_w / q;
                }
                px->uv[i][0] = slope_value(&s_ras.tex[i][0], x, y) * proj;
                px->uv[i][1] = slope_value(&s_ras.tex[i][1], x, y) * proj;
            }
        }
    }
    const u32 iref = gx_bp_reg(GX_BP_IREF);
    for (u32 i = 0; i < s_ras.ind_stages; i++) {
        calc_lod((iref >> (6U * i)) & 7U, (iref >> ((6U * i) + 3U)) & 7U, &s_ras.ind_lod[i], &s_ras.ind_linear[i]);
    }
    for (u32 i = 0; i < s_ras.stages; i++) {
        const u32 order = gx_bp_reg((u8)(GX_BP_TREF + (i >> 1)));
        const u32 o = (i & 1U) != 0U ? order >> 12 : order;
        if (((o >> 6) & 1U) != 0U) {
            calc_lod(o & 7U, (o >> 3) & 7U, &s_ras.lod[i], &s_ras.linear[i]);
        }
    }
}

static void draw_pixel(s32 x, s32 y, s32 xi, s32 yi) {
    f32 zf = slope_value(&s_ras.z, x, y);
    zf = gx_clamp_f32(zf, 0.0F, 16777215.0F);
    GxTevPixel p;
    p.x = x;
    p.y = y;
    p.z = (s32)zf;
    if (gx_tev_early_z() && !gx_efb_z_test((u32)x, (u32)y, (u32)p.z)) {
        return;
    }
    s_ras.stats.pixels++;
    memset(p.color, 0, sizeof(p.color));
    for (u32 i = 0; i < s_ras.colchans; i++) {
        for (u32 c = 0; c < 4U; c++) {
            const f32 v = slope_value(&s_ras.color[i][c], x, y);
            p.color[i][c] = (u8)gx_clamp_f32(v, 0.0F, 255.0F);
        }
    }
    const BlockPixel *px = &s_ras.block[xi][yi];
    for (u32 i = 0; i < GX_TEXCOORD_MAX; i++) {
        p.uv[i][0] = i < s_ras.texgens ? (s32)(px->uv[i][0] * 128.0F) : 0; /* TEV guarda UV em s17.7 */
        p.uv[i][1] = i < s_ras.texgens ? (s32)(px->uv[i][1] * 128.0F) : 0;
    }
    memcpy(p.lod, s_ras.lod, sizeof(p.lod));
    memcpy(p.linear, s_ras.linear, sizeof(p.linear));
    memcpy(p.ind_lod, s_ras.ind_lod, sizeof(p.ind_lod));
    memcpy(p.ind_linear, s_ras.ind_linear, sizeof(p.ind_linear));
    gx_tev_shade(&p);
}

static void setup_slopes(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2, const SlopeCtx *c) {
    const f32 w[3] = {1.0F / v0->proj[3], 1.0F / v1->proj[3], 1.0F / v2->proj[3]};
    s_ras.w = slope(w[0], w[1], w[2], c);
    for (u32 i = 0; i < s_ras.colchans; i++) {
        for (u32 k = 0; k < 4U; k++) {
            s_ras.color[i][k] = slope((f32)v0->color[i][k], (f32)v1->color[i][k], (f32)v2->color[i][k], c);
        }
    }
    for (u32 i = 0; i < s_ras.texgens; i++) {
        for (u32 k = 0; k < 3U; k++) {
            s_ras.tex[i][k] = slope(v0->tex[i][k] * w[0], v1->tex[i][k] * w[1], v2->tex[i][k] * w[2], c);
        }
    }
}

/* Arestas em 28.4 (meio-espaço) e o retângulo envolvente já cortado pelo scissor. */
typedef struct Edges {
    s32 x[3], y[3];
    s32 dx[3], dy[3]; /* 12, 23, 31 */
    s32 c[3];
    s32 minx, maxx, miny, maxy;
} Edges;

static bool edges(const GxXfVertex *const v[3], const Rect *r, Edges *e) {
    for (u32 i = 0; i < 3U; i++) {
        e->x[i] = iround(16.0F * (v[i]->screen[0] - (f32)r->x_off)) - 9;
        e->y[i] = iround(16.0F * (v[i]->screen[1] - (f32)r->y_off)) - 9;
    }
    for (u32 i = 0; i < 3U; i++) {
        const u32 j = (i + 1U) % 3U;
        e->dx[i] = e->x[i] - e->x[j];
        e->dy[i] = e->y[i] - e->y[j];
        e->c[i] = (e->dy[i] * e->x[i]) - (e->dx[i] * e->y[i]);
        if (e->dy[i] < 0 || (e->dy[i] == 0 && e->dx[i] > 0)) { /* regra de preenchimento */
            e->c[i]++;
        }
    }
    const s32 xmin = gx_min_s32(gx_min_s32(e->x[0], e->x[1]), e->x[2]);
    const s32 xmax = gx_max_s32(gx_max_s32(e->x[0], e->x[1]), e->x[2]);
    const s32 ymin = gx_min_s32(gx_min_s32(e->y[0], e->y[1]), e->y[2]);
    const s32 ymax = gx_max_s32(gx_max_s32(e->y[0], e->y[1]), e->y[2]);
    e->minx = (xmin + 0xF) >> 4;
    e->maxx = (xmax + 0xF) >> 4;
    e->miny = (ymin + 0xF) >> 4;
    e->maxy = (ymax + 0xF) >> 4;
    e->minx = gx_max_s32(e->minx, r->left);
    e->maxx = gx_min_s32(e->maxx, r->right);
    e->miny = gx_max_s32(e->miny, r->top);
    e->maxy = gx_min_s32(e->maxy, r->bottom);
    return e->minx < e->maxx && e->miny < e->maxy;
}

static u32 edge_mask(const Edges *e, u32 i, s32 x0, s32 x1, s32 y0, s32 y1) {
    return (u32)(e->c[i] + (e->dx[i] * y0) - (e->dy[i] * x0) > 0) |
           ((u32)(e->c[i] + (e->dx[i] * y0) - (e->dy[i] * x1) > 0) << 1) |
           ((u32)(e->c[i] + (e->dx[i] * y1) - (e->dy[i] * x0) > 0) << 2) |
           ((u32)(e->c[i] + (e->dx[i] * y1) - (e->dy[i] * x1) > 0) << 3);
}

static void raster_block(const Edges *e, s32 x, s32 y) {
    const s32 x0 = x << 4;
    const s32 x1 = (x + BLOCK - 1) << 4;
    const s32 y0 = y << 4;
    const s32 y1 = (y + BLOCK - 1) << 4;
    const u32 a = edge_mask(e, 0, x0, x1, y0, y1);
    const u32 b = edge_mask(e, 1, x0, x1, y0, y1);
    const u32 c = edge_mask(e, 2, x0, x1, y0, y1);
    if (a == 0U || b == 0U || c == 0U) {
        return;
    }
    build_block(x, y);
    s32 cy[3];
    for (u32 i = 0; i < 3U; i++) {
        cy[i] = e->c[i] + (e->dx[i] * y0) - (e->dy[i] * x0);
    }
    for (s32 iy = 0; iy < BLOCK; iy++) {
        s32 cx[3] = {cy[0], cy[1], cy[2]};
        for (s32 ix = 0; ix < BLOCK; ix++) {
            const bool inside = (a == 0xFU && b == 0xFU && c == 0xFU) || (cx[0] > 0 && cx[1] > 0 && cx[2] > 0);
            if (inside && x + ix >= e->minx && x + ix < e->maxx && y + iy >= e->miny && y + iy < e->maxy) {
                draw_pixel(x + ix, y + iy, ix, iy);
            }
            for (u32 i = 0; i < 3U; i++) {
                cx[i] -= e->dy[i] * 16;
            }
        }
        for (u32 i = 0; i < 3U; i++) {
            cy[i] += e->dx[i] * 16;
        }
    }
}

static void triangle_in_rect(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2, const Rect *r) {
    update_z_slope(v0, v1, v2, r->x_off, r->y_off);
    const GxXfVertex *const v[3] = {v0, v1, v2};
    Edges e;
    if (!edges(v, r, &e)) {
        return;
    }
    const SlopeCtx c = slope_ctx(v0, v1, v2, (e.x[0] + 0xF) >> 4, (e.y[0] + 0xF) >> 4, r->x_off, r->y_off);
    setup_slopes(v0, v1, v2, &c);
    for (s32 y = e.miny & ~(BLOCK - 1); y < e.maxy; y += BLOCK) {
        for (s32 x = e.minx & ~(BLOCK - 1); x < e.maxx; x += BLOCK) {
            raster_block(&e, x, y);
        }
    }
}

void gx_raster_triangle(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2) {
    s_ras.stats.triangles++;
    for (u32 i = 0; i < s_ras.rect_count; i++) {
        triangle_in_rect(v0, v1, v2, &s_ras.rects[i]);
    }
}
