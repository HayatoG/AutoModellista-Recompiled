/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Frente do jogo (M9 parte 5): a tela inicial e o contador de quadros sobre a janela, desenhados com o Dear ImGui pela
 * plataforma (am/platform_ui.h). Só existem no modo interativo: os roteiros (--pad-script, sem janela) não passam por
 * aqui. */
#ifndef AM_FRONT_H
#define AM_FRONT_H

#include "am/settings.h"
#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LauncherInfo {
    int gpu_shader;           /* PlatformGpuShader, para religar a GPU se o jogador trocar o backend */
    const char *default_card; /* caminho do memory card padrão (para mostrar) */
} LauncherInfo;

typedef enum LauncherResult {
    LAUNCHER_PLAY,
    LAUNCHER_QUIT,
    LAUNCHER_ERROR, /* a janela falhou (platform_last_error) */
    LAUNCHER_SHOT,  /* captura de desenvolvimento (AM_LAUNCHER_SHOT) terminada: sair sem gravar nada */
} LauncherResult;

/* Tela inicial na janela já aberta (com platform_ui_init feito). Edita `s` conforme o jogador mexe; aplica na hora
 * tela cheia, escala, VSync e o backend (liga ou desliga o device da GPU). Volta quando o jogador escolhe JOGAR (com
 * uma pasta do jogo conferida em s->game_dir) ou Sair/fecha a janela. */
LauncherResult launcher_run(Settings *s, const LauncherInfo *info);

/* Contador de quadros sobre o jogo (F3 liga e desliga) e os atalhos de volume (F9/F10) e tela cheia (F11), que
 * atualizam `s`. overlay_frame é chamado a cada quadro do jogo: drawn = a imagem desse quadro foi desenhada. */
void overlay_start(Settings *s);
void overlay_frame(bool drawn);
void overlay_stop(void);

/* Sessão interativa (front/session.c): lê o settings.ini, põe por cima o que veio da linha de comando, abre a janela e
 * mostra a tela inicial; no fim grava o arquivo (sem gravar o que só a linha de comando mudou). */
typedef struct FrontOverrides {
    const char *sys_dir;                        /* --sys */
    int gpu;                                    /* -1 = do arquivo; 0/1 = --gfx soft/gpu */
    u32 window_scale;                           /* 0 = do arquivo */
    int fullscreen, vsync, fps_overlay, volume; /* -1 = do arquivo */
    int filter;                                 /* -1 = do arquivo; senão PlatformFilter (--present-filter) */
    const char *memcard;                        /* NULL = do arquivo */
    bool no_launcher;
    int gpu_shader;
} FrontOverrides;

typedef struct FrontSession {
    Settings eff;   /* o que vale nesta execução */
    Settings file;  /* o que estava no arquivo */
    Settings start; /* eff antes da tela inicial: o que o jogador não mexeu não é gravado se veio da linha de comando */
    FrontOverrides ov;
    char sys_dir[SETTINGS_PATH_SIZE + 8U];
} FrontSession;

typedef enum FrontStart {
    FRONT_PLAY,
    FRONT_QUIT,  /* o jogador saiu pela tela inicial (já gravado: não chamar front_session_end) */
    FRONT_ERROR, /* sem janela, ou sem pasta do jogo com --no-launcher; o motivo vai no log */
} FrontStart;
FrontStart front_session_begin(FrontSession *fs, const FrontOverrides *ov, const char *window_title);
void front_session_end(FrontSession *fs);

#ifdef __cplusplus
}
#endif

#endif /* AM_FRONT_H */
