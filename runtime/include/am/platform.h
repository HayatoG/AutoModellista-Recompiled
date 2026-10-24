/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Camada de plataforma (SDL3). Único lugar com código de SO; só depende de am/types.h. */
#ifndef AM_PLATFORM_H
#define AM_PLATFORM_H

#include "am/types.h"

/* Inicializa a SDL sem subsistemas; o vídeo só sobe quando a janela é aberta. Em falha, platform_last_error() diz o
 * motivo. */
bool platform_init(void);
void platform_shutdown(void);
const char *platform_last_error(void);
/* Lê o arquivo inteiro. Devolve NULL (e platform_last_error) em falha; liberar com platform_free_file. */
u8 *platform_load_file(const char *path, size_t *size);
void platform_free_file(u8 *data);

/* Cria o diretório (e os pais que faltarem); true se já existe. */
bool platform_make_dir(const char *path);

/* Janela do M6: mostra o XFB (RGBA) com SDL_Renderer, escalada para caber, sem filtro. `scale` multiplica o tamanho
 * inicial. Em falha devolve false (platform_last_error). */
bool platform_window_open(const char *title, u32 width, u32 height, u32 scale);
typedef enum PlatformWindowResult {
    PLATFORM_WINDOW_OK,
    PLATFORM_WINDOW_CLOSED, /* o usuário fechou a janela */
    PLATFORM_WINDOW_ERROR,  /* ver platform_last_error */
} PlatformWindowResult;

/* Atualiza a imagem e trata os eventos da janela. Com `save_png`, grava também o que o renderer desenhou (lido de
 * volta antes de apresentar, no tamanho real em pixels). */
PlatformWindowResult platform_window_present(const u8 *rgba, u32 width, u32 height, const char *save_png);
void platform_window_close(void);

/* Grava uma imagem RGBA (linhas contíguas) em PNG. Não altera `rgba` (não é const porque a SDL pede void*). */
bool platform_save_png(const char *path, u8 *rgba, u32 width, u32 height);

/* Milissegundos de relógio monotônico desde platform_init. */
u64 platform_ticks_ms(void);

/* Versão da SDL em uso, no formato major*1000000 + minor*1000 + patch. */
int platform_sdl_version(void);

#endif /* AM_PLATFORM_H */
