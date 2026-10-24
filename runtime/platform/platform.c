/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/platform.h"

#include <string.h>

#include <SDL3/SDL.h>

static struct {
    bool ready;
    char error[256];
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    u32 tex_w, tex_h;
} s_platform;

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

void platform_shutdown(void) {
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
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        set_error(SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, (int)(width * scale), (int)(height * scale), SDL_WINDOW_RESIZABLE,
                                     &s_platform.window, &s_platform.renderer)) {
        set_error(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    (void)SDL_SetRenderVSync(s_platform.renderer, 0); /* o ritmo é o do VI emulado, não o do monitor */
    return true;
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
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
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
