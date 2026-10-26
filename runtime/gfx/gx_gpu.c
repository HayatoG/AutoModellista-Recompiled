/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gx_gpu.h"

#include <stdlib.h>
#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_efb.h"
#include "am/gx_raster.h"
#include "am/gx_tex.h"
#include "am/log.h"

/* Mapa serial da decodificação -> posição no buffer de texels da GPU (endereçamento aberto, potência de 2). */
#define ARENA_SLOTS 8192U

typedef struct ArenaSlot {
    u64 serial;
    u32 offset;
} ArenaSlot;

/* Triângulo guardado quando o desenho tem mais de um retângulo de scissor (deslocamento que dá a volta). */
typedef struct HeldTri {
    GxXfVertex v[3];
} HeldTri;

static struct {
    GxGpuRunFn run;
    GpuVertex *verts;
    u32 vert_count, vert_cap;
    GpuDraw *draws;
    u32 draw_count, draw_cap;
    GpuTexUpload *uploads;
    u32 upload_count, upload_cap;
    u8 *staging; /* cópia dos texels a enviar (o cache da CPU pode reaproveitar o buffer antes do envio) */
    u32 staging_used, staging_cap;
    ArenaSlot slots[ARENA_SLOTS];
    u32 arena_used;
    bool efb_dirty;
    bool in_draw;
    s32 rects[GX_RASTER_MAX_RECTS][6];
    u32 rect_count;
    u32 texgens;
    GpuDrawUniforms u;     /* do desenho atual */
    GpuDrawUniforms u_tev; /* M9 parte 6: só o fill_tev do último desenho com estado novo */
    bool u_tev_valid;
    HeldTri *held;
    u32 held_count, held_cap;
    u32 mergeable; /* M9 parte 6: 1 + índice do último comando de um retângulo só, que o próximo desenho pode continuar */
    s32 mergeable_rect[6];
    u32 begun; /* desenhos começados desde o último envio (o ritmo dos lotes parciais, com ou sem junção) */
    GxGpuStats stats;
} s_gpu;

static void *grow(void *p, u32 *cap, u32 need, size_t elem) {
    if (need <= *cap) {
        return p;
    }
    u32 c = *cap != 0U ? *cap : 256U;
    while (c < need) {
        c *= 2U;
    }
    void *q = realloc(p, (size_t)c * elem);
    AM_ASSERT(q != NULL, "GX GPU: sem memória (%u elementos)", c);
    *cap = c;
    return q;
}

void gx_gpu_set_backend(GxGpuRunFn run) {
    s_gpu.run = run;
    s_gpu.efb_dirty = true; /* o EFB da GPU começa igual ao da CPU */
}

bool gx_gpu_active(void) {
    return s_gpu.run != NULL;
}

void gx_gpu_reset(void) {
    const GxGpuRunFn run = s_gpu.run;
    free(s_gpu.verts);
    free(s_gpu.draws);
    free(s_gpu.uploads);
    free(s_gpu.staging);
    free(s_gpu.held);
    memset(&s_gpu, 0, sizeof(s_gpu));
    s_gpu.run = run;
    s_gpu.efb_dirty = true;
}

const GxGpuStats *gx_gpu_stats(void) {
    return &s_gpu.stats;
}

static void arena_clear(void) {
    memset(s_gpu.slots, 0, sizeof(s_gpu.slots));
    s_gpu.arena_used = 0;
    s_gpu.stats.arena_resets++;
}

/* Desenhos por lote parcial: a GPU começa a desenhar o quadro enquanto a CPU emula o resto dele, e a espera na
 * cópia do EFB fica só com a última parte. */
#define PARTIAL_DRAWS 192U

static void submit_rows(bool readback, u32 top, u32 bottom, bool depth_too) {
    u32 *color = gx_efb_color_buffer();
    u32 *depth = gx_efb_depth_buffer();
    const GpuBatch b = {.vertices = s_gpu.verts,
                        .vertex_count = s_gpu.vert_count,
                        .draws = s_gpu.draws,
                        .draw_count = s_gpu.draw_count,
                        .uploads = s_gpu.uploads,
                        .upload_count = s_gpu.upload_count,
                        .efb_color_in = s_gpu.efb_dirty ? color : NULL,
                        .efb_depth_in = s_gpu.efb_dirty ? depth : NULL,
                        .efb_color_out = readback ? color : NULL,
                        .efb_depth_out = readback && depth_too ? depth : NULL,
                        .readback_top = top,
                        .readback_bottom = bottom};
    if (!s_gpu.run(&b)) {
        am_fatal("GX GPU: o lote de %u desenhos falhou na GPU", s_gpu.draw_count);
    }
    s_gpu.stats.batches++;
    s_gpu.vert_count = 0;
    s_gpu.draw_count = 0;
    s_gpu.upload_count = 0;
    s_gpu.staging_used = 0;
    s_gpu.efb_dirty = false;
    s_gpu.mergeable = 0;
    s_gpu.begun = 0;
}

static void submit_lot(bool readback) {
    submit_rows(readback, 0, GPU_EFB_HEIGHT, true);
}

/* Posição da decodificação no buffer de texels; envia se for nova. Sem espaço: desenha o lote e recomeça o buffer. */
static u32 arena_place(const GxTexGpuUnit *t) {
    u32 h = (u32)(t->serial * 0x9E3779B97F4A7C15ULL >> 40);
    for (u32 n = 0; n < ARENA_SLOTS; n++, h++) {
        ArenaSlot *s = &s_gpu.slots[h & (ARENA_SLOTS - 1U)];
        if (s->serial == t->serial) {
            return s->offset;
        }
        if (s->serial == 0U) {
            break;
        }
    }
    if (s_gpu.arena_used + t->texels > GPU_ARENA_TEXELS || s_gpu.upload_count + 1U >= ARENA_SLOTS / 2U) {
        submit_lot(false); /* os desenhos já gravados usam o buffer como está */
        arena_clear();     /* o mapa recomeça vazio: a busca abaixo acha lugar */
    }
    h = (u32)(t->serial * 0x9E3779B97F4A7C15ULL >> 40);
    while (s_gpu.slots[h & (ARENA_SLOTS - 1U)].serial != 0U) {
        h++;
    }
    const u32 offset = s_gpu.arena_used;
    s_gpu.slots[h & (ARENA_SLOTS - 1U)] = (ArenaSlot){t->serial, offset};
    s_gpu.arena_used += t->texels;
    const u32 bytes = t->texels * 4U;
    u8 *old = s_gpu.staging;
    s_gpu.staging = grow(s_gpu.staging, &s_gpu.staging_cap, s_gpu.staging_used + bytes, 1);
    if (old != s_gpu.staging) { /* os envios apontam para dentro do buffer: refaz os ponteiros */
        u32 at = 0;
        for (u32 i = 0; i < s_gpu.upload_count; i++) {
            s_gpu.uploads[i].rgba = s_gpu.staging + at;
            at += s_gpu.uploads[i].texels * 4U;
        }
    }
    memcpy(s_gpu.staging + s_gpu.staging_used, t->rgba, bytes);
    s_gpu.uploads = grow(s_gpu.uploads, &s_gpu.upload_cap, s_gpu.upload_count + 1U, sizeof(GpuTexUpload));
    s_gpu.uploads[s_gpu.upload_count++] = (GpuTexUpload){offset, t->texels, s_gpu.staging + s_gpu.staging_used};
    s_gpu.staging_used += bytes;
    s_gpu.stats.texture_uploads++;
    s_gpu.stats.uploaded_texels += t->texels;
    return offset;
}

/* Unidades que os estágios usam (como used_texmaps de gx_tex.c, sem os indiretos). */
static u32 used_texmaps(const GpuDrawUniforms *u) {
    u32 mask = 0;
    for (u32 i = 0; i < u->stages; i++) {
        mask |= ((u->order[i] >> 6) & 1U) != 0U ? 1U << (u->order[i] & 7U) : 0U;
    }
    return mask;
}

static void fill_textures(GpuDrawUniforms *u) {
    const u32 used = used_texmaps(u);
    for (u32 m = 0; m < GPU_TEXMAPS; m++) {
        if ((used & (1U << m)) == 0U) {
            continue;
        }
        GxTexGpuUnit t;
        gx_tex_gpu_unit(m, &t);
        u->tex_size[m] = t.wm1 | (t.hm1 << 16);
        u->tex_mode0[m] = t.mode0;
        u->tex_mode1[m] = t.mode1;
        u->tex_levels[m] = t.serial != 0U ? t.levels : 0U;
        u->tex_base[m] = t.serial != 0U ? arena_place(&t) : 0U;
    }
}

static f32 fog_float(u32 v) { /* gx_tev.c */
    const u32 bits = (((v >> 19) & 1U) << 31) | (((v >> 11) & 0xFFU) << 23) | ((v & 0x7FFU) << 12);
    f32 f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static void fill_tev(GpuDrawUniforms *u) {
    const u32 gen = gx_bp_reg(GX_BP_GENMODE);
    u->stages = ((gen >> 10) & 15U) + 1U;
    u->texgens = gen & 15U;
    u->texgens = u->texgens > GPU_TEXCOORDS ? GPU_TEXCOORDS : u->texgens;
    u->ind_stages = (gen >> 16) & 7U;
    u->colchans = (gen >> 4) & 7U;
    u->colchans = u->colchans > 2U ? 2U : u->colchans;
    for (u32 i = 0; i < u->stages; i++) {
        const u32 order = gx_bp_reg((u8)(GX_BP_TREF + (i >> 1)));
        u->order[i] = ((i & 1U) != 0U ? order >> 12 : order) & 0xFFFU;
        u->cc[i] = gx_bp_reg((u8)(GX_BP_TEV_COLOR_ENV + (2U * i)));
        u->ac[i] = gx_bp_reg((u8)(GX_BP_TEV_ALPHA_ENV + (2U * i)));
    }
    for (u32 i = 0; i < 8U; i++) {
        u->ksel[i] = gx_bp_reg((u8)(GX_BP_TEV_KSEL + i));
    }
    for (u32 n = 0; n < 4U; n++) {
        s16 c[4];
        s16 k[4];
        gx_bp_tev_color(n, c);
        gx_bp_tev_konst(n, k);
        for (u32 ch = 0; ch < 4U; ch++) {
            u->regs[(n * 4U) + ch] = c[ch];
            u->konst[(n * 4U) + ch] = k[ch];
        }
    }
    u->alpha_test = gx_bp_reg(GX_BP_ALPHACOMPARE);
    u->ztex2 = gx_bp_reg(GX_BP_ZTEX2);
    u->ztex_bias = gx_bp_reg(GX_BP_ZTEX_BIAS) & 0xFFFFFFU;
    u->fog3 = gx_bp_reg(GX_BP_FOGPARAM3);
    u->fog_a = fog_float(gx_bp_reg(GX_BP_FOGPARAM0));
    u->fog_c = fog_float(u->fog3);
    u->fog_bmag = gx_bp_reg(GX_BP_FOGBMAGNITUDE);
    u->fog_bshift = gx_bp_reg(GX_BP_FOGBEXPONENT);
    u->fog_color = gx_bp_reg(GX_BP_FOGCOLOR);
    for (u32 i = 0; i < 6U; i++) {
        u->fog_range[i] = gx_bp_reg((u8)(GX_BP_FOGRANGE + i));
    }
    u->vp_wd = gx_xf_viewport().wd;
    u->zmode = gx_bp_reg(GX_BP_ZMODE);
    u->pe_control = gx_bp_reg(GX_BP_ZCOMPARE);
    u->blend = gx_bp_reg(GX_BP_BLENDMODE);
    u->dst_alpha = gx_bp_reg(GX_BP_CONSTANTALPHA);
}

static GpuDraw *draw_for_rect(u32 r);

/* M9 parte 6: a corrida tem ~2.400 desenhos por quadro, de ~4 vértices; com o mesmo estado (uniformes, retângulo) e os
 * vértices logo depois, o desenho novo continua o comando anterior em vez de abrir outro. A GPU desenha os triângulos
 * na mesma ordem (o ROV mistura por ordem de primitiva), então a imagem é a mesma, com menos chamadas no driver. */
static bool continue_last_draw(void) {
    if (s_gpu.mergeable == 0U || s_gpu.mergeable != s_gpu.draw_count) {
        return false;
    }
    const GpuDraw *d = &s_gpu.draws[s_gpu.mergeable - 1U];
    if (d->first_vertex + d->vertex_count != s_gpu.vert_count ||
        memcmp(s_gpu.mergeable_rect, s_gpu.rects[0], sizeof(s_gpu.mergeable_rect)) != 0 ||
        memcmp(&d->u, &s_gpu.u, sizeof(s_gpu.u)) != 0) {
        return false;
    }
    s_gpu.stats.merged_draws++;
    return true;
}

static void begin_draw(bool same_state);

void gx_gpu_begin_draw(void) {
    begin_draw(false);
}

void gx_gpu_begin_draw_same(void) {
    begin_draw(true);
}

static void begin_draw(bool same_state) {
    gx_gpu_end_draw();
    if (s_gpu.begun >= PARTIAL_DRAWS) {
        submit_lot(false);
        s_gpu.stats.partial_batches++;
    }
    s_gpu.begun++;
    if (!same_state || !s_gpu.u_tev_valid) {
        memset(&s_gpu.u_tev, 0, sizeof(s_gpu.u_tev));
        fill_tev(&s_gpu.u_tev);
        s_gpu.u_tev_valid = true;
    }
    s_gpu.u = s_gpu.u_tev;
    fill_textures(&s_gpu.u);
    s_gpu.texgens = s_gpu.u.texgens;
    s_gpu.rect_count = gx_raster_rects(s_gpu.rects, GX_RASTER_MAX_RECTS);
    s_gpu.held_count = 0;
    s_gpu.in_draw = true;
    if (s_gpu.rect_count == 1U && !continue_last_draw()) {
        (void)draw_for_rect(0); /* o caso comum: os triângulos entram direto neste comando */
        s_gpu.mergeable = s_gpu.draw_count;
        memcpy(s_gpu.mergeable_rect, s_gpu.rects[0], sizeof(s_gpu.mergeable_rect));
    }
    s_gpu.stats.draws++;
    s_gpu.stats.indirect_draws += s_gpu.u.ind_stages != 0U ? 1U : 0U;
    s_gpu.stats.zfreeze_draws += ((gx_bp_reg(GX_BP_GENMODE) >> 19) & 1U) != 0U ? 1U : 0U;
}

/* Vértice em pixels do EFB. O rasterizador do software amostra o pixel x onde a aresta, em 28.4, passa por
 * 16 * (sx - deslocamento) - 9 (Rasterizer.cpp); a GPU amostra no centro (x + 0,5): daí o -1/16. */
static void put_vertex(const GxXfVertex *v, const s32 *rect) {
    GpuVertex *o = &s_gpu.verts[s_gpu.vert_count++];
    o->pos[0] = v->screen[0] - (f32)rect[0] - (1.0F / 16.0F);
    o->pos[1] = v->screen[1] - (f32)rect[1] - (1.0F / 16.0F);
    o->pos[2] = v->screen[2];
    o->pos[3] = v->proj[3];
    for (u32 c = 0; c < 2U; c++) {
        for (u32 k = 0; k < 4U; k++) {
            o->color[c][k] = (f32)v->color[c][k];
        }
    }
    for (u32 n = 0; n < GPU_TEXCOORDS; n++) {
        const bool on = n < s_gpu.texgens;
        o->tex[n][0] = on ? v->tex[n][0] : 0.0F;
        o->tex[n][1] = on ? v->tex[n][1] : 0.0F;
        o->tex[n][2] = on ? v->tex[n][2] : 0.0F;
        o->tex[n][3] = 0.0F;
    }
}

static GpuDraw *draw_for_rect(u32 r) {
    s_gpu.draws = grow(s_gpu.draws, &s_gpu.draw_cap, s_gpu.draw_count + 1U, sizeof(GpuDraw));
    GpuDraw *d = &s_gpu.draws[s_gpu.draw_count++];
    const s32 *rc = s_gpu.rects[r];
    *d = (GpuDraw){.first_vertex = s_gpu.vert_count, .scissor = {rc[2], rc[3], rc[4] - rc[2], rc[5] - rc[3]}};
    d->u = s_gpu.u;
    return d;
}

void gx_gpu_triangle(const GxXfVertex *v0, const GxXfVertex *v1, const GxXfVertex *v2) {
    s_gpu.stats.triangles++;
    if (!s_gpu.in_draw || s_gpu.rect_count == 0U) {
        return;
    }
    if (s_gpu.rect_count == 1U) {
        s_gpu.verts = grow(s_gpu.verts, &s_gpu.vert_cap, s_gpu.vert_count + 3U, sizeof(GpuVertex));
        put_vertex(v0, s_gpu.rects[0]);
        put_vertex(v1, s_gpu.rects[0]);
        put_vertex(v2, s_gpu.rects[0]);
        s_gpu.draws[s_gpu.draw_count - 1U].vertex_count += 3U;
        return;
    }
    s_gpu.held = grow(s_gpu.held, &s_gpu.held_cap, s_gpu.held_count + 1U, sizeof(HeldTri));
    s_gpu.held[s_gpu.held_count++] = (HeldTri){{*v0, *v1, *v2}};
}

void gx_gpu_end_draw(void) {
    if (!s_gpu.in_draw) {
        return;
    }
    s_gpu.in_draw = false;
    if (s_gpu.rect_count <= 1U) {
        return;
    }
    s_gpu.mergeable = 0;
    for (u32 r = 0; r < s_gpu.rect_count; r++) {
        GpuDraw *d = draw_for_rect(r);
        s_gpu.verts = grow(s_gpu.verts, &s_gpu.vert_cap, s_gpu.vert_count + (3U * s_gpu.held_count), sizeof(GpuVertex));
        for (u32 i = 0; i < s_gpu.held_count; i++) {
            for (u32 k = 0; k < 3U; k++) {
                put_vertex(&s_gpu.held[i].v[k], s_gpu.rects[r]);
            }
        }
        d->vertex_count = 3U * s_gpu.held_count;
    }
    s_gpu.held_count = 0;
}

void gx_gpu_sync(void) {
    gx_gpu_end_draw();
    submit_lot(true);
}

/* Limpeza na GPU: um retângulo com o shader no modo de limpeza (as mesmas máscaras do ClearEfb de gx_efb.c). */
static void push_clear(const GxEfbClear *c) {
    s_gpu.mergeable = 0;
    s_gpu.draws = grow(s_gpu.draws, &s_gpu.draw_cap, s_gpu.draw_count + 1U, sizeof(GpuDraw));
    GpuDraw *d = &s_gpu.draws[s_gpu.draw_count++];
    memset(d, 0, sizeof(*d));
    d->first_vertex = s_gpu.vert_count;
    d->vertex_count = 6;
    d->scissor[0] = (s32)c->left;
    d->scissor[1] = (s32)c->top;
    d->scissor[2] = (s32)(c->right - c->left);
    d->scissor[3] = (s32)(c->bottom - c->top);
    d->u.flags = GPU_FLAG_CLEAR;
    d->u.stages = 1;
    for (u32 i = 0; i < 4U; i++) {
        d->u.regs[i] = c->rgba[i];
    }
    d->u.ztex_bias = c->z;
    d->u.blend = (c->color ? 1U << 3 : 0U) | (c->alpha ? 1U << 4 : 0U);
    d->u.zmode = c->z_update ? 1U << 4 : 0U;
    d->u.pe_control = c->format;
    const f32 x0 = (f32)c->left;
    const f32 y0 = (f32)c->top;
    const f32 x1 = (f32)c->right;
    const f32 y1 = (f32)c->bottom;
    const f32 corners[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y0}, {x1, y1}, {x0, y1}};
    s_gpu.verts = grow(s_gpu.verts, &s_gpu.vert_cap, s_gpu.vert_count + 6U, sizeof(GpuVertex));
    for (u32 i = 0; i < 6U; i++) {
        GpuVertex *v = &s_gpu.verts[s_gpu.vert_count++];
        memset(v, 0, sizeof(*v));
        v->pos[0] = corners[i][0];
        v->pos[1] = corners[i][1];
        v->pos[3] = 1.0F;
    }
}

void gx_gpu_copy(void) {
    gx_gpu_end_draw();
    u32 top = 0;
    u32 bottom = 0;
    bool depth = false;
    const bool rows = gx_efb_copy_rows(&top, &bottom, &depth);
    submit_rows(true, top, rows ? bottom : 0U, depth);
    gx_efb_copy_ex(false);
    GxEfbClear c;
    if (gx_efb_copy_clears() && gx_efb_clear_params(&c)) {
        push_clear(&c);
    }
    s_gpu.stats.copies++;
}
