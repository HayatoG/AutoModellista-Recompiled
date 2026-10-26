/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Camada de plataforma (SDL3). Único lugar com código de SO; só depende de am/types.h. */
#ifndef AM_PLATFORM_H
#define AM_PLATFORM_H

#include <stddef.h>

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa a SDL sem subsistemas; o vídeo só sobe quando a janela é aberta. Em falha, platform_last_error() diz o
 * motivo. */
bool platform_init(void);
void platform_shutdown(void);
const char *platform_last_error(void);
/* Lê o arquivo inteiro. Devolve NULL (e platform_last_error) em falha; liberar com platform_free_file. */
u8 *platform_load_file(const char *path, size_t *size);
void platform_free_file(u8 *data);
/* Grava o arquivo inteiro (por um temporário e rename). false em falha (platform_last_error). */
bool platform_save_file(const char *path, const u8 *data, size_t size);
/* Diretório de dados do usuário (fora do repositório; criado se faltar), com a barra final. */
bool platform_user_dir(char *out, size_t size);

/* Cria o diretório (e os pais que faltarem); true se já existe. */
bool platform_make_dir(const char *path);

/* Diretório das configurações (M9 parte 5): SDL_GetPrefPath sem organização em todos os sistemas
 * (~/Library/Application Support/AutoModellista/, ~/.local/share/AutoModellista/, %APPDATA%\\AutoModellista\\), com a
 * barra final. No Linux e no Windows é o mesmo de platform_user_dir; no macOS o memory card continua onde estava. */
bool platform_config_dir(char *out, size_t size);
/* O arquivo (ou diretório) existe. */
bool platform_path_exists(const char *path);

/* Janela do M6: mostra o XFB (RGBA) escalado para caber, sem filtro, com barras pretas. Com o GX na GPU
 * (platform_gpu_init antes de abrir), a janela é apresentada pelo próprio device da SDL3 GPU (swapchain); senão, por
 * SDL_Renderer. `scale` multiplica o tamanho inicial. Se a janela já existe (aberta pela tela inicial), só confere a
 * forma de apresentação. Em falha devolve false (platform_last_error). */
bool platform_window_open(const char *title, u32 width, u32 height, u32 scale);
typedef enum PlatformWindowResult {
    PLATFORM_WINDOW_OK,
    PLATFORM_WINDOW_CLOSED, /* o usuário fechou a janela */
    PLATFORM_WINDOW_ERROR,  /* ver platform_last_error */
} PlatformWindowResult;

/* Atualiza a imagem e trata os eventos da janela. Com `save_png`, grava também o que a janela desenhou (lido de
 * volta antes da interface e de apresentar, no tamanho real em pixels). rgba NULL = sem imagem do jogo (fundo preto),
 * como na tela inicial. A interface (am/platform_ui.h), se ligada, é desenhada por cima. */
PlatformWindowResult platform_window_present(const u8 *rgba, u32 width, u32 height, const char *save_png);
void platform_window_close(void);
bool platform_window_is_open(void);

/* M9 parte 5: opções da janela. Valem antes de abrir (guardadas) e depois (aplicadas na hora). high_dpi só antes de
 * abrir: com ele a janela usa a densidade real de pixels (Retina), para o texto da interface ficar nítido. */
void platform_window_set_high_dpi(bool on);
void platform_window_set_fullscreen(bool on);
bool platform_window_fullscreen(void);
/* VSync: a apresentação espera o retraço do monitor. Padrão desligado (o ritmo é o do VI emulado). */
void platform_window_set_vsync(bool on);
/* Tamanho da janela fora da tela cheia: width x height x scale (os de platform_window_open). */
void platform_window_set_scale(u32 scale);
/* "Metal (SDL GPU)", "metal (SDL_Renderer)"...; "" sem janela. */
const char *platform_window_presenter(void);
/* 60 FPS (--frame-trace): quantas chamadas de platform_window_present não mostraram nada (swapchain sem textura,
 * janela minimizada). Só o caminho da GPU descarta; o SDL_Renderer sempre mostra. */
u64 platform_window_dropped_frames(void);

/* Atalhos da janela (M9 parte 5), tratados nos eventos e entregues uma vez: F3, F9/F10 e -/+ do teclado numérico.
 * F11 e Alt+Enter alternam a tela cheia na própria plataforma e também são entregues. */
typedef enum PlatformHotkey {
    PLATFORM_HOTKEY_OVERLAY = 1U << 0,
    PLATFORM_HOTKEY_VOLUME_DOWN = 1U << 1,
    PLATFORM_HOTKEY_VOLUME_UP = 1U << 2,
    PLATFORM_HOTKEY_FULLSCREEN = 1U << 3,
} PlatformHotkey;
u32 platform_hotkeys_take(void);

/* Diálogos nativos da SDL3 (assíncronos): o resultado chega depois, por platform_dialog_take (true quando chegou;
 * texto vazio = cancelado). Um por vez. */
void platform_dialog_pick_folder(const char *start);
void platform_dialog_save_file(const char *start, const char *filter_name, const char *filter_ext);
bool platform_dialog_take(char *out, size_t size);

/* Grava uma imagem RGBA (linhas contíguas) em PNG. Não altera `rgba` (não é const porque a SDL pede void*). */
bool platform_save_png(const char *path, u8 *rgba, u32 width, u32 height);

/* Entrada da janela (M7): estado bruto do teclado (só as teclas ligadas por platform_input_set_keys) e do gamepad
 * escolhido, no vocabulário da SDL3 (SDL_Gamepad: botões por posição, eixos de -32768 a 32767, gatilhos de 0 a 32767).
 * A tradução para o controle de GameCube fica em core/pad_input.c, que dá para testar sem SDL. */
#define PLATFORM_MAX_KEYS 32U
/* Teclas pelo nome da SDL (SDL_GetScancodeFromName: "X", "Return", "Left Shift"); o bit i de PlatformInput.keys é a
 * tecla names[i]. Nome vazio ou desconhecido = sem tecla (devolve false se algum era desconhecido). */
bool platform_input_set_keys(const char *const *names, u32 count);

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
    PLATFORM_PAD_LEFT_STICK,    /* clique */
    PLATFORM_PAD_RIGHT_STICK,   /* clique */
    PLATFORM_PAD_LEFT_TRIGGER,  /* não vem em pad_buttons (é eixo); só para o mapeamento e a captura */
    PLATFORM_PAD_RIGHT_TRIGGER, /* idem */
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
    u32 keys;        /* bit i = tecla i de platform_input_set_keys */
    u32 pad_buttons; /* bit (1 << PlatformPadButton) */
    s16 pad_axes[PLATFORM_AXIS_COUNT];
    bool has_gamepad;
} PlatformInput;

/* Estado atual da entrada (os eventos são tratados em platform_window_present). Sem janela, tudo solto. */
void platform_input_read(PlatformInput *out);

/* Gamepads conectados (até PLATFORM_MAX_GAMEPADS) e o da porta 1: pelo nome, ou NULL/"" = o primeiro conectado. */
#define PLATFORM_MAX_GAMEPADS 8U
u32 platform_gamepad_count(void);
const char *platform_gamepad_name(u32 index);
void platform_gamepad_select(const char *name);
const char *platform_gamepad_active(void); /* "" sem gamepad */

/* Captura para o remapeamento: a próxima tecla ou botão (gatilho a partir da metade do curso) apertados depois do
 * início. Enquanto captura, a interface não recebe a entrada. Esc cancela. */
typedef enum PlatformCapture {
    PLATFORM_CAPTURE_WAITING,
    PLATFORM_CAPTURE_KEY,    /* name = nome da tecla na SDL */
    PLATFORM_CAPTURE_BUTTON, /* button = PlatformPadButton */
    PLATFORM_CAPTURE_CANCELLED,
} PlatformCapture;
void platform_capture_start(void);
PlatformCapture platform_capture_poll(char *name, size_t size, PlatformPadButton *button);

/* Áudio (M7): saída estéreo PCM 16 bits na taxa `rate_hz`, pelo dispositivo padrão (SDL_AudioStream, que converte
 * para a taxa do dispositivo). push enfileira quadros L, R; queued diz quantos quadros ainda não tocaram. Sem
 * dispositivo (ou sem abrir), push não faz nada e queued é 0. */
bool platform_audio_open(u32 rate_hz);
void platform_audio_push(const s16 *lr, u32 frames);
u32 platform_audio_queued_frames(void);
void platform_audio_close(void);
/* M9 parte 5: volume geral da saída (0 a 1), aplicado no SDL_AudioStream (ganho); nada muda no AX/DSP emulados nem
 * no --audio-dump. Vale antes e depois de abrir. */
void platform_audio_set_volume(float gain);

/* Trabalhadores (M7): um conjunto fixo de threads que executa job(índice, trabalhador, user) para índice em [0, jobs),
 * distribuindo os índices sob demanda; quem chama é o trabalhador 0 e só volta quando todos terminaram. `workers`
 * inclui quem chama; 1 = nenhuma thread extra. */
#define PLATFORM_MAX_WORKERS 16U
typedef void (*PlatformJobFn)(u32 index, u32 worker, void *user);
bool platform_parallel_init(u32 workers);
void platform_parallel_run(u32 jobs, PlatformJobFn job, void *user);
void platform_parallel_shutdown(void);
/* Thread, trava e condição (M8, thread do GX): ponteiros opacos para os objetos da SDL3; NULL = falhou. */
typedef int (*PlatformThreadFn)(void *user);
void *platform_thread_start(const char *name, PlatformThreadFn fn, void *user);
void platform_thread_join(void *thread);
void *platform_lock_create(void);
void platform_lock_destroy(void *lock);
void platform_lock(void *lock);
void platform_unlock(void *lock);
void *platform_cond_create(void);
void platform_cond_destroy(void *cond);
void platform_cond_wait(void *cond, void *lock);
void platform_cond_signal(void *cond);
/* Núcleos lógicos da máquina. */
u32 platform_cpu_count(void);

/* Milissegundos de relógio monotônico desde platform_init. */
u64 platform_ticks_ms(void);
/* Nanossegundos de relógio monotônico (para o ritmo em tempo real) e espera. */
u64 platform_ticks_ns(void);
void platform_sleep_ns(u64 ns);

/* Versão da SDL em uso, no formato major*1000000 + minor*1000 + patch. */
int platform_sdl_version(void);

#ifdef __cplusplus
}
#endif

#endif /* AM_PLATFORM_H */
