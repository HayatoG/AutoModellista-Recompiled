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

/* Entrada da janela (M7): estado bruto do teclado (só as teclas que o mapeamento usa) e do primeiro gamepad
 * conectado, no vocabulário da SDL3 (SDL_Gamepad: botões por posição, eixos de -32768 a 32767, gatilhos de 0 a 32767).
 * A tradução para o controle de GameCube fica em core/pad_input.c, que dá para testar sem SDL. */
typedef enum PlatformKey {
    PLATFORM_KEY_X,
    PLATFORM_KEY_Z,
    PLATFORM_KEY_C,
    PLATFORM_KEY_S,
    PLATFORM_KEY_D,
    PLATFORM_KEY_RETURN,
    PLATFORM_KEY_UP,
    PLATFORM_KEY_DOWN,
    PLATFORM_KEY_LEFT,
    PLATFORM_KEY_RIGHT,
    PLATFORM_KEY_SHIFT,
    PLATFORM_KEY_I,
    PLATFORM_KEY_J,
    PLATFORM_KEY_K,
    PLATFORM_KEY_L,
    PLATFORM_KEY_Q,
    PLATFORM_KEY_W,
    PLATFORM_KEY_T,
    PLATFORM_KEY_F,
    PLATFORM_KEY_G,
    PLATFORM_KEY_H,
    PLATFORM_KEY_COUNT,
} PlatformKey;

typedef enum PlatformPadButton {
    PLATFORM_PAD_SOUTH, /* A do Xbox, X do PlayStation */
    PLATFORM_PAD_EAST,
    PLATFORM_PAD_WEST,
    PLATFORM_PAD_NORTH,
    PLATFORM_PAD_BACK,
    PLATFORM_PAD_START,
    PLATFORM_PAD_LEFT_SHOULDER,
    PLATFORM_PAD_RIGHT_SHOULDER,
    PLATFORM_PAD_DPAD_UP,
    PLATFORM_PAD_DPAD_DOWN,
    PLATFORM_PAD_DPAD_LEFT,
    PLATFORM_PAD_DPAD_RIGHT,
    PLATFORM_PAD_BUTTON_COUNT,
} PlatformPadButton;

typedef enum PlatformPadAxis {
    PLATFORM_AXIS_LEFT_X,
    PLATFORM_AXIS_LEFT_Y, /* positivo = para baixo, como na SDL */
    PLATFORM_AXIS_RIGHT_X,
    PLATFORM_AXIS_RIGHT_Y,
    PLATFORM_AXIS_LEFT_TRIGGER,
    PLATFORM_AXIS_RIGHT_TRIGGER,
    PLATFORM_AXIS_COUNT,
} PlatformPadAxis;

typedef struct PlatformInput {
    u32 keys;        /* bit (1 << PlatformKey) */
    u32 pad_buttons; /* bit (1 << PlatformPadButton) */
    s16 pad_axes[PLATFORM_AXIS_COUNT];
    bool has_gamepad;
} PlatformInput;

/* Estado atual da entrada (os eventos são tratados em platform_window_present). Sem janela, tudo solto. */
void platform_input_read(PlatformInput *out);

/* Milissegundos de relógio monotônico desde platform_init. */
u64 platform_ticks_ms(void);

/* Versão da SDL em uso, no formato major*1000000 + minor*1000 + patch. */
int platform_sdl_version(void);

#endif /* AM_PLATFORM_H */
