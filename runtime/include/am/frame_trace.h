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
    /* 60 FPS: o que o diagnóstico precisava separar. A espera pelo GX (gx_wait_ns) é a soma das quatro. */
    u64 wait_draw_ns;   /* esperando a época do DrawDone */
    u64 wait_token_ns;  /* esperando a época de um token */
    u64 wait_read_ns;   /* esperando o GX por uma leitura de fora (XFB, estatísticas) */
    u64 wait_full_ns;   /* esperando lugar na fila de blocos cheia */
    u32 gx_lag_max;     /* maior atraso do GX em épocas num evento do quadro (antes de a CPU esperar) */
    u32 queue_max;      /* maior número de blocos em voo na fila */
    bool shown;         /* a janela mostrou de fato (presented menos os quadros que o swapchain descartou) */
    u64 present_gap_ns; /* tempo desde a última apresentação que mostrou; 0 se não há anterior ou não mostrou */
    /* Conteúdo que o GX concluiu no intervalo (GX na GPU). Com a thread, o GX anda até um quadro atrás da lógica:
     * os picos aparecem deslocados do quadro em que o jogo os pediu. */
    u32 gx_draws, gx_triangles, gx_tex_uploads, gx_arena_resets;
    u64 gx_texels;
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
