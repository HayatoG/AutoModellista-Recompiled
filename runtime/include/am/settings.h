/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Configurações do jogador (M9 parte 5): um arquivo INI simples, settings.ini em platform_config_dir. A linha de
 * comando tem prioridade sobre o arquivo; os roteiros (--pad-script, sem janela) nem o leem. Formato e chaves em
 * docs/M9_PARTE5_LAUNCHER.md §4. */
#ifndef AM_SETTINGS_H
#define AM_SETTINGS_H

#include "am/pad_input.h"
#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_FILE      "settings.ini"
#define SETTINGS_PATH_SIZE 1024U
#define SETTINGS_KEY_SIZE  32U
#define SETTINGS_SCALE_MAX 6U

typedef struct Settings {
    char game_dir[SETTINGS_PATH_SIZE]; /* raiz da árvore extraída (sys/ e files/); vazio = perguntar */
    char memcard[SETTINGS_PATH_SIZE];  /* .raw do slot A; vazio = o padrão (MemoryCardA.raw no diretório de dados) */
    bool gpu;                          /* --gfx gpu (true) ou soft */
    u32 window_scale;                  /* 1..SETTINGS_SCALE_MAX */
    bool fullscreen;
    bool vsync;
    u32 filter; /* PlatformFilter (am/platform.h): 0 nearest, 1 linear, 2 sharp */
    bool fps_overlay;
    u32 volume;        /* 0..100 */
    char gamepad[128]; /* nome do gamepad da porta 1; vazio = o primeiro conectado */
    char keys[PAD_CONTROL_COUNT][SETTINGS_KEY_SIZE];
    PadBindings pad;
} Settings;

void settings_defaults(Settings *s);
/* Nome do filtro da imagem no INI e na linha de comando ("nearest", "linear", "sharp"); fora da faixa, "nearest". */
const char *settings_filter_name(u32 filter);
/* Lê o texto INI por cima de `s` (chaves ausentes ficam como estão). Devolve quantas linhas foram ignoradas por não
 * serem entendidas. */
u32 settings_parse(const char *text, size_t size, Settings *s);
/* Escreve o INI em `out`; devolve o tamanho (sem o '\0') ou 0 se não coube. */
size_t settings_format(const Settings *s, char *out, size_t size);
/* Caminho do arquivo. */
bool settings_path(char *out, size_t size);
/* false = arquivo ausente (s fica com os padrões). */
bool settings_load(Settings *s);
bool settings_save(const Settings *s);

#ifdef __cplusplus
}
#endif

#endif /* AM_SETTINGS_H */
