/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform.h"

#include <string.h>

#include <SDL3/SDL.h>

static struct {
    bool ready;
    char error[256];
} s_platform;

bool platform_init(void) {
    if (s_platform.ready) {
        return true;
    }
    /* Nenhum subsistema ainda: janela, GPU, áudio e gamepad entram nos marcos M6 e M7. */
    if (!SDL_Init(0)) {
        (void)strncpy(s_platform.error, SDL_GetError(), sizeof(s_platform.error) - 1U);
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

int platform_sdl_version(void) {
    return SDL_GetVersion();
}
