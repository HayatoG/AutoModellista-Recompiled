/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitor mínimo do fluxo de comandos do GX (o que o Command Processor consome da FIFO).
 *
 * No M5 não há GPU: o leitor só anda pelo fluxo para (1) manter o alinhamento entre comandos, o que exige saber o
 * tamanho de cada vértice (VCD/VAT, registradores do CP carregados pelo próprio fluxo), e (2) reagir às escritas de BP
 * que a CPU espera ver concluídas: PE_DONE (GXSetDrawDone) e os tokens (GXSetDrawSync). Todo o resto é contado e
 * descartado; desenhar é o M6. Formatos: YAGCD 5.11 (CP), 5.12 (BP) e Dolphin VideoCommon/OpcodeDecoding.cpp,
 * VertexLoaderBase/CPMemory.h, BPStructs.cpp (tag 2609a; códigos de BP em BPMemory.h). */
#ifndef AM_GX_FIFO_H
#define AM_GX_FIFO_H

#include "am/types.h"

/* Opcodes (Dolphin OpcodeDecoding.h). */
#define GX_CMD_NOP            0x00U
#define GX_CMD_LOAD_CP        0x08U
#define GX_CMD_LOAD_XF        0x10U
#define GX_CMD_LOAD_INDX_A    0x20U
#define GX_CMD_LOAD_INDX_D    0x38U
#define GX_CMD_CALL_DL        0x40U
#define GX_CMD_UNKNOWN_METRIC 0x44U
#define GX_CMD_INVL_VC        0x48U
#define GX_CMD_LOAD_BP        0x61U
#define GX_CMD_DRAW_FIRST     0x80U /* 0x80..0xBF: primitiva nos bits 3-6, VAT nos bits 0-2 */

/* Registradores do CP no fluxo (Dolphin CPMemory.h). */
#define GX_CP_VCD_LO 0x50U
#define GX_CP_VCD_HI 0x60U
#define GX_CP_VAT_A  0x70U
#define GX_CP_VAT_B  0x80U
#define GX_CP_VAT_C  0x90U

/* Registradores de BP que o M5 atende (Dolphin BPMemory.h). */
#define GX_BP_SETDRAWDONE  0x45U
#define GX_BP_PE_TOKEN     0x47U
#define GX_BP_PE_TOKEN_INT 0x48U
#define GX_BP_COPY_EXECUTE 0x52U

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
    u64 copies; /* GX_BP_COPY_EXECUTE: cópias EFB -> XFB/textura (M6) */
} GxFifoStats;

/* Quem recebe os eventos de BP (no executável, o PE; nos testes, funções sintéticas). */
typedef struct GxFifoSink {
    void (*draw_done)(void);
    void (*token)(u16 value, bool interrupt);
} GxFifoSink;

void gx_fifo_reset(const GxFifoSink *sink);

/* Alimenta bytes do fluxo, na ordem; comandos podem atravessar chamadas. */
void gx_fifo_feed(const u8 *data, u32 size);

/* Tamanho em bytes de um vértice com o VCD atual e o formato `vat` (0..7). Exposto para teste. */
u32 gx_fifo_vertex_size(u32 vat);

const GxFifoStats *gx_fifo_stats(void);

#endif /* AM_GX_FIFO_H */
