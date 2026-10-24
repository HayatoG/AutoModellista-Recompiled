/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Montagem de comandos do GX para os testes (LOAD_BP, LOAD_CP, LOAD_XF, desenho) e um estado 2D mínimo: viewport de
 * 640x480, projeção ortográfica em pixels, scissor da tela inteira, um canal de cor do vértice e um estágio de TEV
 * que passa a cor rasterizada. Tudo sintético: nenhum byte do jogo. */
#ifndef AM_GX_TEST_UTIL_H
#define AM_GX_TEST_UTIL_H

#include <string.h>

#include "am/gx_bp.h"
#include "am/gx_fifo.h"
#include "am/gx_vtx.h"
#include "am/gx_xf.h"

static inline void gxt_feed(const u8 *b, u32 n) {
    gx_fifo_feed(b, n);
}

static inline void gxt_bp(u8 reg, u32 value) {
    const u8 cmd[5] = {GX_CMD_LOAD_BP, reg, (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    gxt_feed(cmd, sizeof(cmd));
}

static inline void gxt_cp(u8 reg, u32 value) {
    const u8 cmd[6] = {GX_CMD_LOAD_CP, reg, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value};
    gxt_feed(cmd, sizeof(cmd));
}

static inline u32 gxt_f2u(f32 f) {
    u32 u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* LOAD_XF de `n` palavras (1..16) a partir de `addr`. */
static inline void gxt_xf(u32 addr, u32 n, const u32 *words) {
    u8 cmd[5 + 64];
    const u32 head = ((n - 1U) << 16) | addr;
    cmd[0] = GX_CMD_LOAD_XF;
    cmd[1] = (u8)(head >> 24);
    cmd[2] = (u8)(head >> 16);
    cmd[3] = (u8)(head >> 8);
    cmd[4] = (u8)head;
    for (u32 i = 0; i < n; i++) {
        cmd[5U + (i * 4U)] = (u8)(words[i] >> 24);
        cmd[6U + (i * 4U)] = (u8)(words[i] >> 16);
        cmd[7U + (i * 4U)] = (u8)(words[i] >> 8);
        cmd[8U + (i * 4U)] = (u8)words[i];
    }
    gxt_feed(cmd, 5U + (n * 4U));
}

static inline void gxt_xf1(u32 addr, u32 value) {
    gxt_xf(addr, 1, &value);
}

static inline void gxt_xf_floats(u32 addr, u32 n, const f32 *f) {
    u32 w[16];
    for (u32 i = 0; i < n; i++) {
        w[i] = gxt_f2u(f[i]);
    }
    gxt_xf(addr, n, w);
}

/* Big-endian num buffer de vértice. */
static inline u32 gxt_put_f32(u8 *p, f32 f) {
    const u32 u = gxt_f2u(f);
    p[0] = (u8)(u >> 24);
    p[1] = (u8)(u >> 16);
    p[2] = (u8)(u >> 8);
    p[3] = (u8)u;
    return 4;
}

/* Matriz 3x4 identidade na linha `row` (posição: 0; texturas: GX_IDENTITY = 60). */
static inline void gxt_identity(u32 row) {
    const f32 m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    gxt_xf_floats(row * 4U, 12, m);
}

#define GXT_SCISSOR_BASE 342U

/* Estado 2D: coordenadas do vértice em pixels do EFB (0..640, 0..480), cor do vértice RGBA8888 no canal 0. */
static inline void gxt_setup_2d(void) {
    gxt_identity(0);
    gxt_xf1(GX_XF_MATRIXINDEX_A, 60U << 6); /* posição 0, tex0 = identidade (60) */
    const f32 vp[6] = {320.0F, -240.0F, 16777215.0F, 342.0F + 320.0F, 342.0F + 240.0F, 16777215.0F};
    gxt_xf_floats(GX_XF_VIEWPORT, 6, vp);
    const f32 proj[6] = {2.0F / 640.0F, -1.0F, -2.0F / 480.0F, 1.0F, -1.0F, -1.0F};
    gxt_xf_floats(GX_XF_PROJECTION, 6, proj);
    gxt_xf1(GX_XF_PROJECTION + 6U, 1); /* ortográfica */
    gxt_xf1(GX_XF_NUMCHAN, 1);
    gxt_xf1(GX_XF_COLOR0, 1); /* material do vértice, sem luz */
    gxt_xf1(GX_XF_ALPHA0, 1);
    gxt_xf1(GX_XF_NUMTEXGENS, 0);
    gxt_bp(GX_BP_GENMODE, 1U << 4); /* 0 texgens, 1 canal, 1 estágio, sem descarte */
    gxt_bp(GX_BP_SCISSORTL, (GXT_SCISSOR_BASE << 12) | GXT_SCISSOR_BASE);
    gxt_bp(GX_BP_SCISSORBR, ((GXT_SCISSOR_BASE + 639U) << 12) | (GXT_SCISSOR_BASE + 479U));
    gxt_bp(GX_BP_SCISSOROFFSET, ((GXT_SCISSOR_BASE / 2U) << 10) | (GXT_SCISSOR_BASE / 2U));
    gxt_bp(GX_BP_TEV_KSEL, 1U << 2);             /* tabela de troca 0: R, G */
    gxt_bp(GX_BP_TEV_KSEL + 1U, 2U | (3U << 2)); /* B, A */
    gxt_bp(GX_BP_TREF, 0);                       /* estágio 0: sem textura, canal de cor 0 */
    gxt_bp(GX_BP_TEV_COLOR_ENV, (15U << 12) | (15U << 8) | (15U << 4) | 10U | (1U << 19));     /* d = ras.rgb */
    gxt_bp(GX_BP_TEV_ALPHA_ENV, (7U << 13) | (7U << 10) | (7U << 7) | (5U << 4) | (1U << 19)); /* d = ras.a */
    gxt_bp(GX_BP_ALPHACOMPARE, (7U << 16) | (7U << 19));                                       /* sempre passa */
    gxt_bp(GX_BP_ZMODE, 0);
    gxt_bp(GX_BP_BLENDMODE, 0x18);                /* escreve cor e alfa, sem blend */
    gxt_bp(GX_BP_ZCOMPARE, 0);                    /* RGB8_Z24 */
    gxt_cp(GX_CP_VCD_LO, (1U << 9) | (1U << 13)); /* posição e cor 0 diretas */
    gxt_cp(GX_CP_VCD_HI, 0);
    gxt_cp(GX_CP_VAT_A, 1U | (4U << 1) | (5U << 14)); /* xyz f32, cor RGBA8888 */
}

/* Quad (4 vértices) de cor única entre (x0, y0) e (x1, y1). */
static inline void gxt_quad(f32 x0, f32 y0, f32 x1, f32 y1, u32 rgba) {
    u8 cmd[3 + (4 * 16)];
    u32 n = 0;
    cmd[n++] = GX_CMD_DRAW_FIRST; /* quads, VAT 0 */
    cmd[n++] = 0;
    cmd[n++] = 4;
    const f32 xy[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (u32 v = 0; v < 4U; v++) {
        n += gxt_put_f32(&cmd[n], xy[v][0]);
        n += gxt_put_f32(&cmd[n], xy[v][1]);
        n += gxt_put_f32(&cmd[n], 0.0F);
        cmd[n++] = (u8)(rgba >> 24);
        cmd[n++] = (u8)(rgba >> 16);
        cmd[n++] = (u8)(rgba >> 8);
        cmd[n++] = (u8)rgba;
    }
    gxt_feed(cmd, n);
}

#endif /* AM_GX_TEST_UTIL_H */
