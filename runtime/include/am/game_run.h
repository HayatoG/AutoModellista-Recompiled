/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ligação do jogo além de main (M5): disco, OSLink dos RELs, contagem de quadros do laço principal e o relatório
 * final. Fica no executável (usa am/recomp.h, que só existe com o C gerado ligado). */
#ifndef AM_GAME_RUN_H
#define AM_GAME_RUN_H

#include "am/types.h"

typedef struct GameRunConfig {
    const char *disc_root;  /* contém sys/ e files/ */
    u64 frames;             /* 0 = sem parada por quadros */
    const char *events;     /* NULL ou arquivo do log de eventos (mesmo formato de dolphin_trace.py events) */
    const char *pad_script; /* NULL ou roteiro de botões (am/pad_script.h) */
    bool render;            /* M6: rasterizar e copiar o EFB (--gfx soft) ou só contar (--gfx off) */
    const char *shots;      /* NULL ou "quadro:arquivo.png,...": XFB do quadro em PNG (como dolphin_trace --shots) */
    u32 window_scale;       /* 0 = sem janela; senão a janela SDL3 mostra o XFB com esse fator */
} GameRunConfig;

/* Abre o disco, registra os ganchos (OSLink, quadros) e confere que todos passam pelo chassi. */
bool game_run_init(const GameRunConfig *config);
void game_run_shutdown(void);

/* Quadros completos do laço fn_80051034. */
u64 game_run_frames(void);
bool game_run_frame_target_reached(void);

/* Para ChassisConfig.on_tick: se o laço de quadros não anda há GAME_RUN_STALL_FIELDS campos de VI, mostra as
 * threads do SO e para com am_fatal (um jogo esperando algo que a runtime nunca entrega). */
#define GAME_RUN_STALL_FIELDS 1200U
void game_run_tick(const CPUState *cpu);

/* A janela foi fechada pelo usuário (a execução parou por isso). */
bool game_run_window_closed(void);

/* Resumo no log: módulos ligados, DVD, VI, DSP, GX. */
void game_run_report(void);

#endif /* AM_GAME_RUN_H */
