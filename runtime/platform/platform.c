/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform.h"

#include <stdint.h>
#include <string.h>

#include <SDL3/SDL.h>

static struct {
    bool ready;
    char error[256];
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    u32 tex_w, tex_h;
    SDL_Gamepad *gamepad; /* o primeiro conectado; os outros são ignorados */
    SDL_AudioStream *audio;
} s_platform;

/* Conjunto de trabalhadores: cada execução tem uma geração; os trabalhadores acordam com ela, pegam índices num
 * contador atômico e o último a terminar avisa quem chamou. */
static struct {
    SDL_Thread *threads[PLATFORM_MAX_WORKERS];
    u32 count; /* inclui quem chama */
    SDL_Mutex *lock;
    SDL_Condition *start;
    SDL_Condition *done;
    u64 generation;
    bool quit;
    u32 jobs;
    PlatformJobFn job;
    void *user;
    SDL_AtomicInt next;
    u32 pending;
} s_pool;

static void set_error(const char *msg) {
    (void)strncpy(s_platform.error, msg, sizeof(s_platform.error) - 1U);
}

bool platform_init(void) {
    if (s_platform.ready) {
        return true;
    }
    /* Nenhum subsistema aqui: o vídeo sobe com a janela (M6); áudio e gamepad entram no M7. */
    if (!SDL_Init(0)) {
        set_error(SDL_GetError());
        return false;
    }
    s_platform.ready = true;
    return true;
}

static void pool_take(u32 worker) {
    for (;;) {
        const int i = SDL_AddAtomicInt(&s_pool.next, 1);
        if (i < 0 || (u32)i >= s_pool.jobs) {
            return;
        }
        s_pool.job((u32)i, worker, s_pool.user);
    }
}

static int SDLCALL pool_thread(void *arg) {
    const u32 worker = (u32)(uintptr_t)arg;
    u64 seen = 0;
    SDL_LockMutex(s_pool.lock);
    for (;;) {
        while (!s_pool.quit && s_pool.generation == seen) {
            SDL_WaitCondition(s_pool.start, s_pool.lock);
        }
        if (s_pool.quit) {
            break;
        }
        seen = s_pool.generation;
        SDL_UnlockMutex(s_pool.lock);
        pool_take(worker);
        SDL_LockMutex(s_pool.lock);
        if (--s_pool.pending == 0U) {
            SDL_SignalCondition(s_pool.done);
        }
    }
    SDL_UnlockMutex(s_pool.lock);
    return 0;
}

bool platform_parallel_init(u32 workers) {
    platform_parallel_shutdown();
    workers = workers > PLATFORM_MAX_WORKERS ? PLATFORM_MAX_WORKERS : (workers == 0U ? 1U : workers);
    s_pool.lock = SDL_CreateMutex();
    s_pool.start = SDL_CreateCondition();
    s_pool.done = SDL_CreateCondition();
    if (s_pool.lock == NULL || s_pool.start == NULL || s_pool.done == NULL) {
        set_error(SDL_GetError());
        platform_parallel_shutdown();
        return false;
    }
    s_pool.count = 1;
    for (u32 i = 1; i < workers; i++) {
        s_pool.threads[i] = SDL_CreateThread(pool_thread, "gx", (void *)(uintptr_t)i);
        if (s_pool.threads[i] == NULL) {
            set_error(SDL_GetError());
            platform_parallel_shutdown();
            return false;
        }
        s_pool.count++;
    }
    return true;
}

void platform_parallel_run(u32 jobs, PlatformJobFn job, void *user) {
    if (s_pool.count <= 1U) {
        for (u32 i = 0; i < jobs; i++) {
            job(i, 0, user);
        }
        return;
    }
    SDL_LockMutex(s_pool.lock);
    s_pool.jobs = jobs;
    s_pool.job = job;
    s_pool.user = user;
    SDL_SetAtomicInt(&s_pool.next, 0);
    s_pool.pending = s_pool.count - 1U;
    s_pool.generation++;
    SDL_BroadcastCondition(s_pool.start);
    SDL_UnlockMutex(s_pool.lock);
    pool_take(0);
    SDL_LockMutex(s_pool.lock);
    while (s_pool.pending != 0U) {
        SDL_WaitCondition(s_pool.done, s_pool.lock);
    }
    SDL_UnlockMutex(s_pool.lock);
}

void platform_parallel_shutdown(void) {
    if (s_pool.lock != NULL) {
        SDL_LockMutex(s_pool.lock);
        s_pool.quit = true;
        SDL_BroadcastCondition(s_pool.start);
        SDL_UnlockMutex(s_pool.lock);
    }
    for (u32 i = 1; i < PLATFORM_MAX_WORKERS; i++) {
        if (s_pool.threads[i] != NULL) {
            SDL_WaitThread(s_pool.threads[i], NULL);
        }
    }
    if (s_pool.lock != NULL) {
        SDL_DestroyMutex(s_pool.lock);
    }
    if (s_pool.start != NULL) {
        SDL_DestroyCondition(s_pool.start);
    }
    if (s_pool.done != NULL) {
        SDL_DestroyCondition(s_pool.done);
    }
    memset(&s_pool, 0, sizeof(s_pool));
}

u32 platform_cpu_count(void) {
    const int n = SDL_GetNumLogicalCPUCores();
    return n > 0 ? (u32)n : 1U;
}

void platform_shutdown(void) {
    platform_parallel_shutdown();
    platform_audio_close();
    platform_window_close();
    if (s_platform.ready) {
        SDL_Quit();
        s_platform.ready = false;
    }
}

const char *platform_last_error(void) {
    return s_platform.error;
}

u8 *platform_load_file(const char *path, size_t *size) {
    void *data = SDL_LoadFile(path, size);
    if (data == NULL) {
        set_error(SDL_GetError());
    }
    return (u8 *)data;
}

void platform_free_file(u8 *data) {
    SDL_free(data);
}

u64 platform_ticks_ms(void) {
    return (u64)SDL_GetTicks();
}

u64 platform_ticks_ns(void) {
    return (u64)SDL_GetTicksNS();
}

void platform_sleep_ns(u64 ns) {
    SDL_DelayPrecise(ns);
}

bool platform_audio_open(u32 rate_hz) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        set_error(SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec = {.format = SDL_AUDIO_S16, .channels = 2, .freq = (int)rate_hz};
    s_platform.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (s_platform.audio == NULL) {
        set_error(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    (void)SDL_ResumeAudioStreamDevice(s_platform.audio);
    return true;
}

void platform_audio_push(const s16 *lr, u32 frames) {
    if (s_platform.audio != NULL) {
        (void)SDL_PutAudioStreamData(s_platform.audio, lr, (int)(frames * 4U));
    }
}

u32 platform_audio_queued_frames(void) {
    if (s_platform.audio == NULL) {
        return 0;
    }
    const int bytes = SDL_GetAudioStreamQueued(s_platform.audio);
    return bytes > 0 ? (u32)bytes / 4U : 0U;
}

void platform_audio_close(void) {
    if (s_platform.audio != NULL) {
        SDL_DestroyAudioStream(s_platform.audio);
        s_platform.audio = NULL;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

int platform_sdl_version(void) {
    return SDL_GetVersion();
}

bool platform_make_dir(const char *path) {
    if (!SDL_CreateDirectory(path)) {
        set_error(SDL_GetError());
        return false;
    }
    return true;
}

bool platform_window_open(const char *title, u32 width, u32 height, u32 scale) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        set_error(SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, (int)(width * scale), (int)(height * scale), SDL_WINDOW_RESIZABLE,
                                     &s_platform.window, &s_platform.renderer)) {
        set_error(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        return false;
    }
    (void)SDL_SetRenderVSync(s_platform.renderer, 0); /* o ritmo é o do VI emulado, não o do monitor */
    return true;
}

/* Gamepads: a SDL anuncia os já conectados com SDL_EVENT_GAMEPAD_ADDED logo depois do SDL_INIT_GAMEPAD. */
static void gamepad_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED && s_platform.gamepad == NULL) {
        s_platform.gamepad = SDL_OpenGamepad(ev->gdevice.which);
    } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED && s_platform.gamepad != NULL &&
               SDL_GetGamepadID(s_platform.gamepad) == ev->gdevice.which) {
        SDL_CloseGamepad(s_platform.gamepad);
        s_platform.gamepad = NULL;
    }
}

static const SDL_Scancode s_key_codes[PLATFORM_KEY_COUNT] = {
    [PLATFORM_KEY_X] = SDL_SCANCODE_X,          [PLATFORM_KEY_Z] = SDL_SCANCODE_Z,
    [PLATFORM_KEY_C] = SDL_SCANCODE_C,          [PLATFORM_KEY_S] = SDL_SCANCODE_S,
    [PLATFORM_KEY_D] = SDL_SCANCODE_D,          [PLATFORM_KEY_RETURN] = SDL_SCANCODE_RETURN,
    [PLATFORM_KEY_UP] = SDL_SCANCODE_UP,        [PLATFORM_KEY_DOWN] = SDL_SCANCODE_DOWN,
    [PLATFORM_KEY_LEFT] = SDL_SCANCODE_LEFT,    [PLATFORM_KEY_RIGHT] = SDL_SCANCODE_RIGHT,
    [PLATFORM_KEY_SHIFT] = SDL_SCANCODE_LSHIFT, [PLATFORM_KEY_I] = SDL_SCANCODE_I,
    [PLATFORM_KEY_J] = SDL_SCANCODE_J,          [PLATFORM_KEY_K] = SDL_SCANCODE_K,
    [PLATFORM_KEY_L] = SDL_SCANCODE_L,          [PLATFORM_KEY_Q] = SDL_SCANCODE_Q,
    [PLATFORM_KEY_W] = SDL_SCANCODE_W,          [PLATFORM_KEY_T] = SDL_SCANCODE_T,
    [PLATFORM_KEY_F] = SDL_SCANCODE_F,          [PLATFORM_KEY_G] = SDL_SCANCODE_G,
    [PLATFORM_KEY_H] = SDL_SCANCODE_H,
};

static const SDL_GamepadButton s_pad_codes[PLATFORM_PAD_BUTTON_COUNT] = {
    [PLATFORM_PAD_SOUTH] = SDL_GAMEPAD_BUTTON_SOUTH,
    [PLATFORM_PAD_EAST] = SDL_GAMEPAD_BUTTON_EAST,
    [PLATFORM_PAD_WEST] = SDL_GAMEPAD_BUTTON_WEST,
    [PLATFORM_PAD_NORTH] = SDL_GAMEPAD_BUTTON_NORTH,
    [PLATFORM_PAD_BACK] = SDL_GAMEPAD_BUTTON_BACK,
    [PLATFORM_PAD_START] = SDL_GAMEPAD_BUTTON_START,
    [PLATFORM_PAD_LEFT_SHOULDER] = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    [PLATFORM_PAD_RIGHT_SHOULDER] = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    [PLATFORM_PAD_DPAD_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP,
    [PLATFORM_PAD_DPAD_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    [PLATFORM_PAD_DPAD_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    [PLATFORM_PAD_DPAD_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};

static const SDL_GamepadAxis s_axis_codes[PLATFORM_AXIS_COUNT] = {
    [PLATFORM_AXIS_LEFT_X] = SDL_GAMEPAD_AXIS_LEFTX,
    [PLATFORM_AXIS_LEFT_Y] = SDL_GAMEPAD_AXIS_LEFTY,
    [PLATFORM_AXIS_RIGHT_X] = SDL_GAMEPAD_AXIS_RIGHTX,
    [PLATFORM_AXIS_RIGHT_Y] = SDL_GAMEPAD_AXIS_RIGHTY,
    [PLATFORM_AXIS_LEFT_TRIGGER] = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    [PLATFORM_AXIS_RIGHT_TRIGGER] = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

void platform_input_read(PlatformInput *out) {
    memset(out, 0, sizeof(*out));
    if (s_platform.window == NULL) {
        return;
    }
    /* Teclas só contam com a janela em foco (a SDL zera o estado quando o foco sai). */
    const bool *keys = SDL_GetKeyboardState(NULL);
    for (u32 i = 0; i < (u32)PLATFORM_KEY_COUNT; i++) {
        if (keys[s_key_codes[i]]) {
            out->keys |= 1U << i;
        }
    }
    if (s_platform.gamepad == NULL) {
        return;
    }
    out->has_gamepad = true;
    for (u32 i = 0; i < (u32)PLATFORM_PAD_BUTTON_COUNT; i++) {
        if (SDL_GetGamepadButton(s_platform.gamepad, s_pad_codes[i])) {
            out->pad_buttons |= 1U << i;
        }
    }
    for (u32 i = 0; i < (u32)PLATFORM_AXIS_COUNT; i++) {
        out->pad_axes[i] = SDL_GetGamepadAxis(s_platform.gamepad, s_axis_codes[i]);
    }
}

static bool ensure_texture(u32 width, u32 height) {
    if (s_platform.texture != NULL && s_platform.tex_w == width && s_platform.tex_h == height) {
        return true;
    }
    if (s_platform.texture != NULL) {
        SDL_DestroyTexture(s_platform.texture);
    }
    s_platform.texture = SDL_CreateTexture(s_platform.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                           (int)width, (int)height);
    if (s_platform.texture == NULL) {
        set_error(SDL_GetError());
        return false;
    }
    (void)SDL_SetTextureScaleMode(s_platform.texture, SDL_SCALEMODE_NEAREST);
    (void)SDL_SetRenderLogicalPresentation(s_platform.renderer, (int)width, (int)height,
                                           SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s_platform.tex_w = width;
    s_platform.tex_h = height;
    return true;
}

static bool save_render(const char *path) {
    SDL_Surface *shot = SDL_RenderReadPixels(s_platform.renderer, NULL);
    if (shot == NULL) {
        set_error(SDL_GetError());
        return false;
    }
    const bool ok = SDL_SavePNG(shot, path);
    if (!ok) {
        set_error(SDL_GetError());
    }
    SDL_DestroySurface(shot);
    return ok;
}

PlatformWindowResult platform_window_present(const u8 *rgba, u32 width, u32 height, const char *save_png) {
    if (s_platform.renderer == NULL) {
        set_error("janela não aberta");
        return PLATFORM_WINDOW_ERROR;
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            return PLATFORM_WINDOW_CLOSED;
        }
        gamepad_event(&ev);
    }
    if (!ensure_texture(width, height) || !SDL_UpdateTexture(s_platform.texture, NULL, rgba, (int)(width * 4U))) {
        set_error(SDL_GetError());
        return PLATFORM_WINDOW_ERROR;
    }
    (void)SDL_SetRenderDrawColor(s_platform.renderer, 0, 0, 0, 255);
    (void)SDL_RenderClear(s_platform.renderer);
    (void)SDL_RenderTexture(s_platform.renderer, s_platform.texture, NULL, NULL);
    if (save_png != NULL && !save_render(save_png)) {
        return PLATFORM_WINDOW_ERROR;
    }
    (void)SDL_RenderPresent(s_platform.renderer);
    return PLATFORM_WINDOW_OK;
}

void platform_window_close(void) {
    if (s_platform.gamepad != NULL) {
        SDL_CloseGamepad(s_platform.gamepad);
        s_platform.gamepad = NULL;
    }
    if (s_platform.texture != NULL) {
        SDL_DestroyTexture(s_platform.texture);
        s_platform.texture = NULL;
    }
    if (s_platform.renderer != NULL) {
        SDL_DestroyRenderer(s_platform.renderer);
        s_platform.renderer = NULL;
    }
    if (s_platform.window != NULL) {
        SDL_DestroyWindow(s_platform.window);
        s_platform.window = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    }
}

bool platform_save_png(const char *path, u8 *rgba, u32 width, u32 height) {
    SDL_Surface *s = SDL_CreateSurfaceFrom((int)width, (int)height, SDL_PIXELFORMAT_RGBA32, rgba, (int)(width * 4U));
    if (s == NULL) {
        set_error(SDL_GetError());
        return false;
    }
    const bool ok = SDL_SavePNG(s, path);
    if (!ok) {
        set_error(SDL_GetError());
    }
    SDL_DestroySurface(s);
    return ok;
}
