/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9 parte 6: rastro por quadro do jogo (--frame-trace [q0:q1:]arq.csv). Uma linha por quadro de fn_80051034, guardada
 * em memória e gravada só no fim (sem E/S durante o jogo), com um relatório de percentis em <arq>.txt. Desligado, o
 * custo é um teste por quadro (frame_trace_on). */
#ifndef AM_FRAME_TRACE_H
#define AM_FRAME_TRACE_H

#include "am/types.h"

typedef struct FrameTraceRow {
    u64 frame;
    u64 wall_ns;       /* relógio do host desde o quadro anterior */
    u64 emu_cycles;    /* ciclos emulados no quadro */
    u32 vi_fields;     /* campos de VI no quadro */
    bool skipped;      /* o frameskip não desenhou este quadro */
    bool presented;    /* a janela foi atualizada neste quadro */
    bool unique;       /* com imagem nova (não a mesma do quadro anterior) */
    u64 present_ns;    /* dentro de platform_window_present (na thread da CPU emulada) */
    u64 pace_sleep_ns; /* dormindo no ritmo do tempo real (emulação adiantada) */
    u64 gx_wait_ns;    /* a CPU esperando o GX (DrawDone, tokens, leituras, fila cheia) */
    u64 gx_busy_ns;    /* a thread do GX trabalhando nos blocos */
    u64 gpu_wait_us;   /* esperando a GPU nas leituras do EFB */
    u32 readbacks;     /* leituras do EFB */
    u32 cow_copies;    /* páginas copiadas na escrita */
} FrameTraceRow;

/* spec = "arq.csv" (todos os quadros) ou "q0:q1:arq.csv" (só q0..q1). */
bool frame_trace_open(const char *spec);
bool frame_trace_on(void);
/* Quadro dentro da faixa pedida (o chamador mede só nele). */
bool frame_trace_wants(u64 frame);
void frame_trace_add(const FrameTraceRow *row);
/* Grava o CSV e o relatório e libera. */
void frame_trace_close(void);

#endif /* AM_FRAME_TRACE_H */
