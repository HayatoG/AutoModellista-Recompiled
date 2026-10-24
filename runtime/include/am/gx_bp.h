/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Registradores de BP do GX (rasterização, TEV, texturas, Z, blend, cópias do EFB) e a TMEM.
 *
 * Cada escrita de BP é "LOAD_BP reg valor24". A máscara BP_MASK (0xFE) vale só para a escrita seguinte. Os
 * registradores de cor do TEV (0xE0..0xE7) são dois por endereço (cor e konst, escolhidos pelo bit 23), então ficam
 * à parte. Escritas que disparam algo devolvem um evento ao leitor do fluxo (fim de desenho, token, cópia do EFB);
 * a carga de TLUT e o preload copiam da memória para a TMEM aqui mesmo. Fontes: YAGCD 5.12; Dolphin
 * VideoCommon/BPMemory.h (campos) e BPStructs.cpp (LoadBPReg, BPWritten), tag 2609a. */
#ifndef AM_GX_BP_H
#define AM_GX_BP_H

#include "am/types.h"

#define GX_BP_GENMODE          0x00U
#define GX_BP_DISPCOPYFILTER   0x01U /* 0x01..0x04: padrão de amostras do AA */
#define GX_BP_IND_MTXA         0x06U /* 0x06..0x0E: três matrizes indiretas (A, B, C) */
#define GX_BP_IND_CMD          0x10U /* 0x10..0x1F: TEV indireto por estágio */
#define GX_BP_SCISSORTL        0x20U
#define GX_BP_SCISSORBR        0x21U
#define GX_BP_LINEPTWIDTH      0x22U
#define GX_BP_RAS1_SS0         0x25U
#define GX_BP_RAS1_SS1         0x26U
#define GX_BP_IREF             0x27U
#define GX_BP_TREF             0x28U /* 0x28..0x2F: ordem do TEV, dois estágios por registrador */
#define GX_BP_SU_SSIZE         0x30U /* 0x30 + 2n: escala s da texcoord n; 0x31 + 2n: t */
#define GX_BP_ZMODE            0x40U
#define GX_BP_BLENDMODE        0x41U
#define GX_BP_CONSTANTALPHA    0x42U
#define GX_BP_ZCOMPARE         0x43U /* PEControl: formato de pixel, formato de Z, Z antecipado */
#define GX_BP_SETDRAWDONE      0x45U
#define GX_BP_PE_TOKEN         0x47U
#define GX_BP_PE_TOKEN_INT     0x48U
#define GX_BP_EFB_TL           0x49U
#define GX_BP_EFB_WH           0x4AU
#define GX_BP_EFB_ADDR         0x4BU
#define GX_BP_EFB_STRIDE       0x4DU
#define GX_BP_COPYYSCALE       0x4EU
#define GX_BP_CLEAR_AR         0x4FU
#define GX_BP_CLEAR_GB         0x50U
#define GX_BP_CLEAR_Z          0x51U
#define GX_BP_COPY_EXECUTE     0x52U
#define GX_BP_COPYFILTER0      0x53U
#define GX_BP_COPYFILTER1      0x54U
#define GX_BP_SCISSOROFFSET    0x59U
#define GX_BP_PRELOAD_ADDR     0x60U
#define GX_BP_PRELOAD_TMEMEVEN 0x61U
#define GX_BP_PRELOAD_TMEMODD  0x62U
#define GX_BP_PRELOAD_MODE     0x63U
#define GX_BP_LOADTLUT0        0x64U
#define GX_BP_LOADTLUT1        0x65U
#define GX_BP_TX_SETMODE0      0x80U /* + texmap & 3; texmaps 4..7 em + 0x20 */
#define GX_BP_TX_SETMODE1      0x84U
#define GX_BP_TX_SETIMAGE0     0x88U
#define GX_BP_TX_SETIMAGE1     0x8CU
#define GX_BP_TX_SETIMAGE2     0x90U
#define GX_BP_TX_SETIMAGE3     0x94U
#define GX_BP_TX_SETTLUT       0x98U
#define GX_BP_TEV_COLOR_ENV    0xC0U /* + 2 * estágio */
#define GX_BP_TEV_ALPHA_ENV    0xC1U
#define GX_BP_TEV_COLOR_RA     0xE0U /* + 2n */
#define GX_BP_TEV_COLOR_BG     0xE1U
#define GX_BP_FOGRANGE         0xE8U
#define GX_BP_FOGPARAM0        0xEEU
#define GX_BP_FOGBMAGNITUDE    0xEFU
#define GX_BP_FOGBEXPONENT     0xF0U
#define GX_BP_FOGPARAM3        0xF1U
#define GX_BP_FOGCOLOR         0xF2U
#define GX_BP_ALPHACOMPARE     0xF3U
#define GX_BP_ZTEX_BIAS        0xF4U
#define GX_BP_ZTEX2            0xF5U
#define GX_BP_TEV_KSEL         0xF6U /* 0xF6..0xFD */
#define GX_BP_MASK             0xFEU

#define GX_TMEM_SIZE  (1024U * 1024U)
#define GX_TMEM_LINE  32U
#define GX_TEXMAP_MAX 8U

typedef enum GxBpEvent {
    GX_BP_EVENT_NONE,
    GX_BP_EVENT_DRAW_DONE, /* SETDRAWDONE com 0x02 */
    GX_BP_EVENT_TOKEN,
    GX_BP_EVENT_TOKEN_INT,
    GX_BP_EVENT_COPY, /* COPY_EXECUTE: o leitor do fluxo chama o EFB */
} GxBpEvent;

void gx_bp_reset(void);
/* `value` com 24 bits; devolve o que a escrita disparou. */
GxBpEvent gx_bp_write(u8 reg, u32 value);
u32 gx_bp_reg(u8 reg);

/* Registrador `kind` (GX_BP_TX_SETMODE0, ...) da unidade de textura `texmap` (0..7). */
u32 gx_bp_tex_reg(u32 kind, u32 texmap);

/* Cores do TEV (PREV, C0, C1, C2) e konst (K0..K3), RGBA de 11 bits com sinal. */
void gx_bp_tev_color(u32 n, s16 *rgba);
void gx_bp_tev_konst(u32 n, s16 *rgba);

/* Bytes da TMEM a partir de `offset` (TLUTs e texturas pré-carregadas); NULL se passar do fim. */
const u8 *gx_bp_tmem(u32 offset, u32 size);

#endif /* AM_GX_BP_H */
