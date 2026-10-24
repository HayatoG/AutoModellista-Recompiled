/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitor do fluxo de comandos do GX (o que o Command Processor consome da FIFO e das display lists).
 *
 * Cada comando vai ao seu módulo: LOAD_CP ao vertex loader (gx_vtx), LOAD_XF e LOAD_INDX_* ao XF (gx_xf), LOAD_BP ao
 * BP (gx_bp), e os desenhos, vértice a vértice, à montagem de primitivas (gx_setup), que transforma, rasteriza e
 * passa pelo TEV até o EFB. A cópia do EFB (BP 0x52) vai ao gx_efb. Fim de desenho (PE_DONE) e tokens vão ao PE pelo
 * `GxFifoSink`. Com a renderização desligada (padrão, e nos testes do M5) os desenhos e as cópias são só contados.
 * Formatos: YAGCD 5.11 (CP), 5.12 (BP) e Dolphin VideoCommon/OpcodeDecoding.cpp, tag 2609a. */
#ifndef AM_GX_FIFO_H
#define AM_GX_FIFO_H

#include "am/gx_bp.h"
#include "am/gx_vtx.h"
#include "am/types.h"

/* Opcodes (Dolphin OpcodeDecoding.h). */
#define GX_CMD_NOP            0x00U
#define GX_CMD_LOAD_CP        0x08U
#define GX_CMD_LOAD_XF        0x10U
#define GX_CMD_LOAD_INDX_A    0x20U
#define GX_CMD_LOAD_INDX_B    0x28U
#define GX_CMD_LOAD_INDX_C    0x30U
#define GX_CMD_LOAD_INDX_D    0x38U
#define GX_CMD_CALL_DL        0x40U
#define GX_CMD_UNKNOWN_METRIC 0x44U
#define GX_CMD_INVL_VC        0x48U
#define GX_CMD_LOAD_BP        0x61U
#define GX_CMD_DRAW_FIRST     0x80U /* 0x80..0xBF: primitiva nos bits 3-6, VAT nos bits 0-2 */

typedef struct GxFifoStats {
    u64 bytes;
    u64 commands;
    u64 draws;
    u64 vertices;
    u64 bp_writes;
    u64 cp_writes;
    u64 xf_words;
    u64 display_lists;
    u64 draw_done;
    u64 tokens;
    u64 copies; /* GX_BP_COPY_EXECUTE: cópias EFB -> XFB/textura */
} GxFifoStats;

/* Quem recebe os eventos de BP (no executável, o PE; nos testes, funções sintéticas). */
typedef struct GxFifoSink {
    void (*draw_done)(void);
    void (*token)(u16 value, bool interrupt);
} GxFifoSink;

/* Zera o leitor e todo o estado do GX (CP, XF, BP, EFB); a renderização fica desligada. */
void gx_fifo_reset(const GxFifoSink *sink);

/* Liga ou desliga a rasterização e as cópias do EFB (o estado de CP/XF/BP é sempre mantido). */
void gx_fifo_set_render(bool enabled);
bool gx_fifo_render_enabled(void);

/* Alimenta bytes do fluxo, na ordem; comandos podem atravessar chamadas. */
void gx_fifo_feed(const u8 *data, u32 size);

/* Tamanho em bytes de um vértice com o VCD atual e o formato `vat` (0..7). Exposto para teste. */
u32 gx_fifo_vertex_size(u32 vat);

const GxFifoStats *gx_fifo_stats(void);

#endif /* AM_GX_FIFO_H */
