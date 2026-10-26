/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Janela (M6), entrada (M7) e, desde o M9 parte 5, a apresentação pelo device da GPU, tela cheia, VSync, atalhos,
 * gamepad escolhido, captura para o remapeamento e diálogos nativos. */
#include <stdio.h>
#include <string.h>

#include "am/platform.h"
#include "am/platform_gpu.h"

#include "platform_internal.h"

static struct {
    SDL_Window *window;
    SDL_Renderer *renderer; /* apresentação pelo SDL_Renderer */
    SDL_Texture *texture;
    u32 tex_w, tex_h;
    bool gpu; /* apresentação pelo swapchain do device da GPU */
    u32 base_w, base_h, scale;
    bool high_dpi, fullscreen, vsync;
    char presenter[64];
    SDL_Gamepad *pads[PLATFORM_MAX_GAMEPADS]; /* todos os conectados */
    u32 pad_count;
    char want_pad[128]; /* nome do gamepad da porta 1; vazio = o primeiro */
    SDL_Scancode keys[PLATFORM_MAX_KEYS];
    u32 key_count;
    bool keys_set;
    u32 hotkeys;
    bool capturing;
    PlatformCapture capture;
    char capture_name[64];
    PlatformPadButton capture_button;
    SDL_Mutex *dialog_lock;
    bool dialog_done;
    char dialog_path[1024];
} s_win;

/* Teclas padrão até platform_input_set_keys: as de am/pad_input.h, na ordem de PadControl. */
static const SDL_Scancode s_default_keys[] = {
    SDL_SCANCODE_X,    SDL_SCANCODE_Z,     SDL_SCANCODE_C,      SDL_SCANCODE_S, SDL_SCANCODE_D,  SDL_SCANCODE_RETURN,
    SDL_SCANCODE_T,    SDL_SCANCODE_G,     SDL_SCANCODE_F,      SDL_SCANCODE_H, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_I,      SDL_SCANCODE_K, SDL_SCANCODE_J,  SDL_SCANCODE_L,
    SDL_SCANCODE_Q,    SDL_SCANCODE_W,     SDL_SCANCODE_LSHIFT,
};

static const SDL_GamepadButton s_pad_codes[PLATFORM_PAD_LEFT_TRIGGER] = {
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
    [PLATFORM_PAD_LEFT_STICK] = SDL_GAMEPAD_BUTTON_LEFT_STICK,
    [PLATFORM_PAD_RIGHT_STICK] = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
};

static const SDL_GamepadAxis s_axis_codes[PLATFORM_AXIS_COUNT] = {
    [PLATFORM_AXIS_LEFT_X] = SDL_GAMEPAD_AXIS_LEFTX,
    [PLATFORM_AXIS_LEFT_Y] = SDL_GAMEPAD_AXIS_LEFTY,
    [PLATFORM_AXIS_RIGHT_X] = SDL_GAMEPAD_AXIS_RIGHTX,
    [PLATFORM_AXIS_RIGHT_Y] = SDL_GAMEPAD_AXIS_RIGHTY,
    [PLATFORM_AXIS_LEFT_TRIGGER] = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    [PLATFORM_AXIS_RIGHT_TRIGGER] = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

/* ---- forma de apresentação ----------------------------------------------------------------------------------------
 */

static void drop_renderer(void) {
    if (s_win.texture != NULL) {
        SDL_DestroyTexture(s_win.texture);
        s_win.texture = NULL;
        s_win.tex_w = 0;
        s_win.tex_h = 0;
    }
    if (s_win.renderer != NULL) {
        SDL_DestroyRenderer(s_win.renderer);
        s_win.renderer = NULL;
    }
}

static void drop_presenter(void) {
    if (s_win.renderer == NULL && !s_win.gpu) {
        return;
    }
    platform_ui_on_presenter(NULL, NULL, NULL, SDL_GPU_TEXTUREFORMAT_INVALID);
    if (s_win.gpu) {
        present_gpu_detach();
        s_win.gpu = false;
    }
    drop_renderer();
    s_win.presenter[0] = '\0';
}

static const char *pretty_driver(const char *driver) {
    if (strcmp(driver, "metal") == 0) {
        return "Metal";
    }
    if (strcmp(driver, "vulkan") == 0) {
        return "Vulkan";
    }
    return strcmp(driver, "direct3d12") == 0 ? "D3D12" : driver;
}

/* Com o device da GPU, o swapchain dele; senão (ou se falhar), o SDL_Renderer. */
static bool ensure_presenter(void) {
    SDL_GPUDevice *device = platform_gpu_device();
    if ((device != NULL && s_win.gpu) || (device == NULL && s_win.renderer != NULL)) {
        return true;
    }
    drop_presenter();
    if (device != NULL && present_gpu_attach(s_win.window, device, s_win.vsync)) {
        s_win.gpu = true;
        (void)snprintf(s_win.presenter, sizeof(s_win.presenter), "%s (SDL GPU)",
                       pretty_driver(SDL_GetGPUDeviceDriver(device)));
        platform_ui_on_presenter(s_win.window, NULL, device, present_gpu_format());
        return true;
    }
    s_win.renderer = SDL_CreateRenderer(s_win.window, NULL);
    if (s_win.renderer == NULL) {
        platform_set_error(SDL_GetError());
        return false;
    }
    (void)SDL_SetRenderVSync(s_win.renderer, s_win.vsync ? 1 : 0); /* desligado: o ritmo é o do VI emulado */
    (void)snprintf(s_win.presenter, sizeof(s_win.presenter), "%s (SDL_Renderer)", SDL_GetRendererName(s_win.renderer));
    platform_ui_on_presenter(s_win.window, s_win.renderer, NULL, SDL_GPU_TEXTUREFORMAT_INVALID);
    return true;
}

void platform_window_release_gpu(void) {
    if (s_win.gpu) {
        drop_presenter();
    }
}

bool platform_window_open(const char *title, u32 width, u32 height, u32 scale) {
    if (s_win.window != NULL) {
        return ensure_presenter();
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        platform_set_error(SDL_GetError());
        return false;
    }
    s_win.base_w = width;
    s_win.base_h = height;
    s_win.scale = scale;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    flags |= s_win.high_dpi ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0U;
    flags |= s_win.fullscreen ? SDL_WINDOW_FULLSCREEN : 0U;
    s_win.window = SDL_CreateWindow(title, (int)(width * scale), (int)(height * scale), flags);
    if (s_win.window == NULL || !ensure_presenter()) {
        platform_set_error(SDL_GetError());
        platform_window_close();
        return false;
    }
    return true;
}

bool platform_window_is_open(void) {
    return s_win.window != NULL;
}

void platform_window_set_high_dpi(bool on) {
    s_win.high_dpi = on;
}

void platform_window_set_fullscreen(bool on) {
    s_win.fullscreen = on;
    if (s_win.window != NULL) {
        (void)SDL_SetWindowFullscreen(s_win.window, on); /* tela cheia "de mesa", sem trocar o modo de vídeo */
    }
}

bool platform_window_fullscreen(void) {
    return s_win.fullscreen;
}

void platform_window_set_vsync(bool on) {
    s_win.vsync = on;
    if (s_win.renderer != NULL) {
        (void)SDL_SetRenderVSync(s_win.renderer, on ? 1 : 0);
    } else if (s_win.gpu) {
        present_gpu_set_vsync(on);
    }
}

void platform_window_set_scale(u32 scale) {
    s_win.scale = scale;
    if (s_win.window != NULL && !s_win.fullscreen && scale != 0U) {
        (void)SDL_SetWindowSize(s_win.window, (int)(s_win.base_w * scale), (int)(s_win.base_h * scale));
    }
}

const char *platform_window_presenter(void) {
    return s_win.presenter;
}

u64 platform_window_dropped_frames(void) {
    return present_gpu_dropped();
}

/* ---- eventos --------------------------------------------------------------------------------------------------------
 */

static void select_active_pad(void);

static void gamepad_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED && s_win.pad_count < PLATFORM_MAX_GAMEPADS) {
        SDL_Gamepad *g = SDL_OpenGamepad(ev->gdevice.which);
        if (g != NULL) {
            s_win.pads[s_win.pad_count++] = g;
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (u32 i = 0; i < s_win.pad_count; i++) {
            if (SDL_GetGamepadID(s_win.pads[i]) == ev->gdevice.which) {
                SDL_CloseGamepad(s_win.pads[i]);
                memmove(&s_win.pads[i], &s_win.pads[i + 1U], (s_win.pad_count - i - 1U) * sizeof(s_win.pads[0]));
                s_win.pad_count--;
                break;
            }
        }
    }
}

static bool reserved_key(SDL_Scancode sc) {
    return sc == SDL_SCANCODE_F3 || sc == SDL_SCANCODE_F9 || sc == SDL_SCANCODE_F10 || sc == SDL_SCANCODE_F11 ||
           sc == SDL_SCANCODE_KP_MINUS || sc == SDL_SCANCODE_KP_PLUS;
}

static void hotkey_event(const SDL_KeyboardEvent *k) {
    if (k->repeat) {
        return;
    }
    const bool alt_enter = k->scancode == SDL_SCANCODE_RETURN && (k->mod & SDL_KMOD_ALT) != 0U;
    if (k->scancode == SDL_SCANCODE_F11 || alt_enter) {
        platform_window_set_fullscreen(!s_win.fullscreen);
        s_win.hotkeys |= PLATFORM_HOTKEY_FULLSCREEN;
    } else if (k->scancode == SDL_SCANCODE_F3) {
        s_win.hotkeys |= PLATFORM_HOTKEY_OVERLAY;
    } else if (k->scancode == SDL_SCANCODE_F9 || k->scancode == SDL_SCANCODE_KP_MINUS) {
        s_win.hotkeys |= PLATFORM_HOTKEY_VOLUME_DOWN;
    } else if (k->scancode == SDL_SCANCODE_F10 || k->scancode == SDL_SCANCODE_KP_PLUS) {
        s_win.hotkeys |= PLATFORM_HOTKEY_VOLUME_UP;
    }
}

static PlatformPadButton pad_button_of(u8 sdl_button) {
    for (u32 i = 0; i < (u32)PLATFORM_PAD_LEFT_TRIGGER; i++) {
        if ((u8)s_pad_codes[i] == sdl_button) {
            return (PlatformPadButton)i;
        }
    }
    return PLATFORM_PAD_BUTTON_COUNT;
}

/* Remapeamento: a primeira tecla, botão ou gatilho (metade do curso) depois de platform_capture_start. */
static void capture_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        if (ev->key.scancode == SDL_SCANCODE_ESCAPE) {
            s_win.capture = PLATFORM_CAPTURE_CANCELLED;
        } else if (!reserved_key(ev->key.scancode)) {
            (void)snprintf(s_win.capture_name, sizeof(s_win.capture_name), "%s", SDL_GetScancodeName(ev->key.scancode));
            s_win.capture = PLATFORM_CAPTURE_KEY;
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const PlatformPadButton b = pad_button_of(ev->gbutton.button);
        if (b != PLATFORM_PAD_BUTTON_COUNT) {
            s_win.capture_button = b;
            s_win.capture = PLATFORM_CAPTURE_BUTTON;
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && ev->gaxis.value > 16384 &&
               (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        s_win.capture_button =
            ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PLATFORM_PAD_LEFT_TRIGGER : PLATFORM_PAD_RIGHT_TRIGGER;
        s_win.capture = PLATFORM_CAPTURE_BUTTON;
    }
    if (s_win.capture != PLATFORM_CAPTURE_WAITING) {
        s_win.capturing = false;
    }
}

/* false = pediram para fechar. */
static bool pump_events(void) {
    SDL_Event ev;
    bool open = true;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            open = false;
        }
        gamepad_event(&ev);
        if (s_win.capturing) {
            capture_event(&ev);
            /* a interface não vê o que foi capturado, mas vê as teclas soltas (senão fica com Enter "apertado") */
            if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_TEXT_INPUT ||
                ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                continue;
            }
            platform_ui_event(&ev);
            continue;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            hotkey_event(&ev.key);
        }
        platform_ui_event(&ev);
    }
    select_active_pad();
    return open;
}

u32 platform_hotkeys_take(void) {
    const u32 h = s_win.hotkeys;
    s_win.hotkeys = 0;
    return h;
}

/* ---- quadro pelo SDL_Renderer -------------------------------------------------------------------------------------
 */

static bool ensure_texture(u32 width, u32 height) {
    if (s_win.texture != NULL && s_win.tex_w == width && s_win.tex_h == height) {
        return true;
    }
    if (s_win.texture != NULL) {
        SDL_DestroyTexture(s_win.texture);
    }
    s_win.texture =
        SDL_CreateTexture(s_win.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, (int)width, (int)height);
    if (s_win.texture == NULL) {
        platform_set_error(SDL_GetError());
        return false;
    }
    (void)SDL_SetTextureScaleMode(s_win.texture, SDL_SCALEMODE_NEAREST);
    s_win.tex_w = width;
    s_win.tex_h = height;
    return true;
}

static bool save_render(const char *path) {
    SDL_Surface *shot = SDL_RenderReadPixels(s_win.renderer, NULL);
    if (shot == NULL) {
        platform_set_error(SDL_GetError());
        return false;
    }
    const bool ok = SDL_SavePNG(shot, path);
    if (!ok) {
        platform_set_error(SDL_GetError());
    }
    SDL_DestroySurface(shot);
    return ok;
}

static bool renderer_frame(const u8 *rgba, u32 width, u32 height, const char *save_png, bool ui) {
    SDL_Renderer *r = s_win.renderer;
    if (rgba != NULL) {
        if (!ensure_texture(width, height) || !SDL_UpdateTexture(s_win.texture, NULL, rgba, (int)(width * 4U))) {
            platform_set_error(SDL_GetError());
            return false;
        }
        (void)SDL_SetRenderLogicalPresentation(r, (int)width, (int)height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    (void)SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    (void)SDL_RenderClear(r);
    if (rgba != NULL) {
        (void)SDL_RenderTexture(r, s_win.texture, NULL, NULL);
    }
    if (save_png != NULL && !save_render(save_png)) {
        return false;
    }
    if (ui) { /* a interface desenha em pixels da janela, sem a escala da imagem do jogo */
        (void)SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        platform_ui_render_renderer(r);
    }
    (void)SDL_RenderPresent(r);
    return true;
}

PlatformWindowResult platform_window_present(const u8 *rgba, u32 width, u32 height, const char *save_png) {
    if (s_win.window == NULL) {
        platform_set_error("janela não aberta");
        return PLATFORM_WINDOW_ERROR;
    }
    if (!pump_events()) {
        return PLATFORM_WINDOW_CLOSED;
    }
    if (!ensure_presenter()) {
        return PLATFORM_WINDOW_ERROR;
    }
    const bool ui = platform_ui_build();
    const bool ok = s_win.gpu ? present_gpu_frame(rgba, width, height, save_png, ui)
                              : renderer_frame(rgba, width, height, save_png, ui);
    return ok ? PLATFORM_WINDOW_OK : PLATFORM_WINDOW_ERROR;
}

void platform_window_close(void) {
    for (u32 i = 0; i < s_win.pad_count; i++) {
        SDL_CloseGamepad(s_win.pads[i]);
    }
    s_win.pad_count = 0;
    drop_presenter();
    if (s_win.window != NULL) {
        SDL_DestroyWindow(s_win.window);
        s_win.window = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    }
}

/* ---- entrada ------------------------------------------------------------------------------------------------------
 */

bool platform_input_set_keys(const char *const *names, u32 count) {
    bool ok = true;
    s_win.key_count = count < PLATFORM_MAX_KEYS ? count : PLATFORM_MAX_KEYS;
    for (u32 i = 0; i < s_win.key_count; i++) {
        s_win.keys[i] =
            names[i] != NULL && names[i][0] != '\0' ? SDL_GetScancodeFromName(names[i]) : SDL_SCANCODE_UNKNOWN;
        ok = ok && (names[i] == NULL || names[i][0] == '\0' || s_win.keys[i] != SDL_SCANCODE_UNKNOWN);
    }
    s_win.keys_set = true;
    return ok;
}

static SDL_Gamepad *active_pad(void) {
    return s_win.pad_count != 0U ? s_win.pads[0] : NULL;
}

/* O gamepad pedido vai para a primeira posição da lista; os outros ficam na ordem em que chegaram. */
static void select_active_pad(void) {
    if (s_win.want_pad[0] == '\0') {
        return;
    }
    for (u32 i = 1; i < s_win.pad_count; i++) {
        const char *name = SDL_GetGamepadName(s_win.pads[i]);
        if (name != NULL && strcmp(name, s_win.want_pad) == 0) {
            SDL_Gamepad *g = s_win.pads[i];
            memmove(&s_win.pads[1], &s_win.pads[0], i * sizeof(s_win.pads[0]));
            s_win.pads[0] = g;
            return;
        }
    }
}

void platform_input_read(PlatformInput *out) {
    memset(out, 0, sizeof(*out));
    if (s_win.window == NULL) {
        return;
    }
    /* Teclas só contam com a janela em foco (a SDL zera o estado quando o foco sai). */
    const bool *keys = SDL_GetKeyboardState(NULL);
    const SDL_Scancode *codes = s_win.keys_set ? s_win.keys : s_default_keys;
    const u32 count = s_win.keys_set ? s_win.key_count : (u32)(sizeof(s_default_keys) / sizeof(s_default_keys[0]));
    for (u32 i = 0; i < count; i++) {
        if (codes[i] != SDL_SCANCODE_UNKNOWN && keys[codes[i]]) {
            out->keys |= 1U << i;
        }
    }
    SDL_Gamepad *pad = active_pad();
    if (pad == NULL) {
        return;
    }
    out->has_gamepad = true;
    for (u32 i = 0; i < (u32)PLATFORM_PAD_LEFT_TRIGGER; i++) {
        if (SDL_GetGamepadButton(pad, s_pad_codes[i])) {
            out->pad_buttons |= 1U << i;
        }
    }
    for (u32 i = 0; i < (u32)PLATFORM_AXIS_COUNT; i++) {
        out->pad_axes[i] = SDL_GetGamepadAxis(pad, s_axis_codes[i]);
    }
}

u32 platform_gamepad_count(void) {
    return s_win.pad_count;
}

const char *platform_gamepad_name(u32 index) {
    const char *name = index < s_win.pad_count ? SDL_GetGamepadName(s_win.pads[index]) : NULL;
    return name != NULL ? name : "";
}

void platform_gamepad_select(const char *name) {
    (void)snprintf(s_win.want_pad, sizeof(s_win.want_pad), "%s", name != NULL ? name : "");
    select_active_pad();
}

const char *platform_gamepad_active(void) {
    return platform_gamepad_name(0);
}

void platform_capture_start(void) {
    s_win.capturing = true;
    s_win.capture = PLATFORM_CAPTURE_WAITING;
}

PlatformCapture platform_capture_poll(char *name, size_t size, PlatformPadButton *button) {
    const PlatformCapture c = s_win.capture;
    if (c == PLATFORM_CAPTURE_KEY) {
        (void)snprintf(name, size, "%s", s_win.capture_name);
    } else if (c == PLATFORM_CAPTURE_BUTTON) {
        *button = s_win.capture_button;
    }
    if (c != PLATFORM_CAPTURE_WAITING) {
        s_win.capture = PLATFORM_CAPTURE_WAITING;
        s_win.capturing = false;
    }
    return c;
}

/* ---- diálogos -----------------------------------------------------------------------------------------------------
 */

/* Pode ser chamado de outra thread (depende do sistema). */
static void SDLCALL dialog_done(void *user, const char *const *files, int filter) {
    (void)user;
    (void)filter;
    SDL_LockMutex(s_win.dialog_lock);
    (void)snprintf(s_win.dialog_path, sizeof(s_win.dialog_path), "%s",
                   files != NULL && files[0] != NULL ? files[0] : "");
    s_win.dialog_done = true;
    SDL_UnlockMutex(s_win.dialog_lock);
}

static bool dialog_prepare(void) {
    if (s_win.dialog_lock == NULL) {
        s_win.dialog_lock = SDL_CreateMutex();
    }
    return s_win.dialog_lock != NULL;
}

void platform_dialog_pick_folder(const char *start) {
    if (dialog_prepare()) {
        SDL_ShowOpenFolderDialog(dialog_done, NULL, s_win.window, start, false);
    }
}

void platform_dialog_save_file(const char *start, const char *filter_name, const char *filter_ext) {
    if (dialog_prepare()) {
        const SDL_DialogFileFilter filter = {filter_name, filter_ext};
        SDL_ShowSaveFileDialog(dialog_done, NULL, s_win.window, &filter, 1, start);
    }
}

bool platform_dialog_take(char *out, size_t size) {
    if (s_win.dialog_lock == NULL) {
        return false;
    }
    SDL_LockMutex(s_win.dialog_lock);
    const bool done = s_win.dialog_done;
    if (done) {
        (void)snprintf(out, size, "%s", s_win.dialog_path);
        s_win.dialog_done = false;
    }
    SDL_UnlockMutex(s_win.dialog_lock);
    return done;
}
