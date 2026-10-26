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
/* Pular o desenho de um quadro (M7, janela em tempo real atrasada): os comandos continuam sendo lidos (tokens,
 * DrawDone, estado), mas os desenhos não são rasterizados e as cópias para o XFB (com a limpeza) não acontecem: o
 * EFB fica limpo para o próximo quadro e o XFB guarda a imagem anterior. Cópias para textura continuam (quem pula
 * garante que o jogo não as está usando). */
void gx_fifo_set_skip(bool skip);

/* Alimenta bytes do fluxo, na ordem; comandos podem atravessar chamadas. */
void gx_fifo_feed(const u8 *data, u32 size);

/* Tamanho em bytes de um vértice com o VCD atual e o formato `vat` (0..7). Exposto para teste. */
u32 gx_fifo_vertex_size(u32 vat);

/* Com a thread do GX, sincroniza antes de devolver. */
const GxFifoStats *gx_fifo_stats(void);

/* M8: o GX numa thread própria. A thread da CPU lê o fluxo só por cima (CP, BP, display lists) e dispara os eventos
 * de BP no mesmo ponto de antes, depois de esperar o GX terminar o que veio antes; a outra thread faz o resto. Os
 * objetos vêm da plataforma (am/platform.h), que o núcleo não conhece. Ligar logo depois de gx_fifo_reset. */
typedef struct GxFifoThreadApi {
    void *(*thread_start)(const char *name, int (*fn)(void *user), void *user);
    void (*thread_join)(void *thread);
    void *(*lock_create)(void);
    void (*lock_destroy)(void *lock);
    void (*lock)(void *lock);
    void (*unlock)(void *lock);
    void *(*cond_create)(void);
    void (*cond_destroy)(void *cond);
    void (*cond_wait)(void *cond, void *lock);
    void (*cond_signal)(void *cond);
    u64 (*ticks_ns)(void);
} GxFifoThreadApi;
/* `depth` (parte 2b): quantos eventos (DrawDone, tokens) o GX pode ficar atrás da CPU, com a cópia na escrita de
 * mem.h; 0 = a CPU espera o GX em cada evento (parte 2a; obrigatório com o software, que lê texturas da memória
 * durante o desenho). */
bool gx_fifo_thread_start(const GxFifoThreadApi *api, u32 depth);
/* Termina o que está na fila e para a thread (gx_fifo_reset também para). */
void gx_fifo_thread_stop(void);
bool gx_fifo_threaded(void);
/* Espera a thread do GX terminar tudo o que já foi alimentado (sem thread, nada). Antes de ler o XFB ou o EFB. */
void gx_fifo_sync(void);
/* Sincronizações pedidas, quantas encontraram o GX ainda trabalhando e o tempo de relógio esperado por motivo
 * (0 = DrawDone, 1 = token, 2 = leitura de fora: XFB, estatísticas). */
void gx_fifo_thread_counters(u64 *syncs, u64 *waits, u64 wait_ns[3]);
/* Tempo de relógio que a thread do GX passou trabalhando (o resto ela esperou a CPU). */
u64 gx_fifo_thread_busy_ns(void);
/* M9 parte 6: tempo da CPU esperando lugar na fila de blocos cheia. */
u64 gx_fifo_thread_full_ns(void);
/* 60 FPS (--frame-trace): maior atraso do GX em épocas (no instante de cada evento, antes de a CPU esperar) e maior
 * número de blocos em voo desde a última chamada; zera os dois. Só a thread da CPU chama. */
void gx_fifo_thread_take_peaks(u32 *lag, u32 *blocks);
/* 60 FPS (--frame-trace): quanto a thread da CPU gasta no caminho do GX, sem as esperas. Liga depois de
 * gx_fifo_thread_start (sem a thread não faz nada). feed = queue_push + leitura por cima dos bytes diretos; dl = só a
 * leitura das display lists (as duas medidas não se sobrepõem). Totais desde que ligou; só a thread da CPU chama. */
void gx_fifo_cpu_timing(bool on, u64 clock_floor_ns);
void gx_fifo_cpu_cost(u64 *feed_ns, u64 *feed_calls, u64 *dl_ns, u64 *dl_calls);
/* Janela com a thread do GX: pede que o GX converta o XFB em RGBA quando chegar a este ponto do fluxo, sem a CPU
 * esperar (false sem a thread: o chamador converte). gx_fifo_xfb_latest copia a última conversão pronta. */
bool gx_fifo_present_xfb(GuestAddr addr, u32 width, u32 height, u32 stride);
bool gx_fifo_xfb_latest(u8 *rgba, u32 width, u32 height);
/* M9 parte 6: quantas conversões do XFB o GX já entregou (muda = a janela tem imagem nova; --frame-trace). */
u64 gx_fifo_xfb_seq(void);

#endif /* AM_GX_FIFO_H */
