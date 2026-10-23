/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform.h"

#include <string.h>

#include <SDL3/SDL.h>

static struct {
    bool ready;
    char error[256];
} s_platform;

static void set_error(const char *msg) {
    (void)strncpy(s_platform.error, msg, sizeof(s_platform.error) - 1U);
}

bool platform_init(void) {
    if (s_platform.ready) {
        return true;
    }
    /* Nenhum subsistema ainda: janela, GPU, áudio e gamepad entram nos marcos M6 e M7. */
    if (!SDL_Init(0)) {
        set_error(SDL_GetError());
        return false;
    }
    s_platform.ready = true;
    return true;
}

void platform_shutdown(void) {
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
