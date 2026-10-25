/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sessão interativa (am/front.h): configurações, janela e tela inicial antes do jogo. */
#include <stdio.h>
#include <string.h>

#include "am/front.h"
#include "am/game.h"
#include "am/gamedir.h"
#include "am/log.h"
#include "am/platform.h"
#include "am/platform_gpu.h"
#include "am/platform_ui.h"

static void apply_overrides(Settings *s, const FrontOverrides *ov) {
    if (ov->sys_dir != NULL) { /* a raiz: <sys>/.. (sem o "/..", se a pasta termina em "sys") */
        const size_t n = strlen(ov->sys_dir);
        const bool sys = n >= 4U && strcmp(ov->sys_dir + n - 4U, "/sys") == 0;
        (void)snprintf(s->game_dir, sizeof(s->game_dir), sys ? "%.*s" : "%.*s/..", (int)(sys ? n - 4U : n),
                       ov->sys_dir);
    }
    if (ov->gpu >= 0) {
        s->gpu = ov->gpu == 1;
    }
    if (ov->window_scale != 0U) {
        s->window_scale = ov->window_scale > SETTINGS_SCALE_MAX ? SETTINGS_SCALE_MAX : ov->window_scale;
    }
    if (ov->fullscreen >= 0) {
        s->fullscreen = ov->fullscreen == 1;
    }
    if (ov->vsync >= 0) {
        s->vsync = ov->vsync == 1;
    }
    if (ov->fps_overlay >= 0) {
        s->fps_overlay = ov->fps_overlay == 1;
    }
    if (ov->volume >= 0) {
        s->volume = (u32)ov->volume;
    }
    if (ov->memcard != NULL) {
        (void)snprintf(s->memcard, sizeof(s->memcard), "%s", ov->memcard);
    }
}

static void default_card(char *out, size_t size) {
    char dir[900];
    if (platform_user_dir(dir, sizeof(dir))) {
        (void)snprintf(out, size, "%sMemoryCardA.raw", dir);
    } else {
        (void)snprintf(out, size, "MemoryCardA.raw");
    }
}

/* Janela e interface antes do jogo; com a GPU, o device já sobe aqui (a janela é apresentada por ele). */
static bool open_window(FrontSession *fs, const char *title) {
    platform_window_set_high_dpi(true);
    platform_window_set_fullscreen(fs->eff.fullscreen);
    platform_window_set_vsync(fs->eff.vsync);
    platform_audio_set_volume((float)fs->eff.volume / 100.0F);
    if (fs->eff.gpu && !platform_gpu_init((PlatformGpuShader)fs->ov.gpu_shader)) {
        AM_LOG_WARN("--gfx gpu: sem GPU utilizável (%s); a tela inicial vai pelo SDL_Renderer", platform_gpu_error());
    }
    if (!platform_ui_init() || !platform_window_open(title, GAME_XFB_WIDTH, GAME_XFB_HEIGHT, fs->eff.window_scale)) {
        AM_LOG_ERROR("janela: %s", platform_last_error());
        return false;
    }
    return true;
}

static bool resolve_sys(FrontSession *fs) {
    char root[SETTINGS_PATH_SIZE];
    char hash[SHA256_HEX_SIZE];
    const GameDirResult r = gamedir_check(fs->eff.game_dir, root, sizeof(root), hash);
    if (r != GAMEDIR_OK && fs->ov.sys_dir == NULL) {
        AM_LOG_ERROR("pasta do jogo \"%s\": %s (escolha na tela inicial ou passe --sys)", fs->eff.game_dir,
                     gamedir_result_text(r));
        return false;
    }
    if (r == GAMEDIR_OK) {
        (void)snprintf(fs->eff.game_dir, sizeof(fs->eff.game_dir), "%s", root);
        (void)snprintf(fs->sys_dir, sizeof(fs->sys_dir), "%s/sys", root);
    } else { /* --sys explícito: como antes, o boot é que reclama de um DOL diferente */
        AM_LOG_WARN("--sys %s: %s", fs->ov.sys_dir, gamedir_result_text(r));
        (void)snprintf(fs->sys_dir, sizeof(fs->sys_dir), "%s", fs->ov.sys_dir);
    }
    return true;
}

FrontStart front_session_begin(FrontSession *fs, const FrontOverrides *ov, const char *window_title) {
    memset(fs, 0, sizeof(*fs));
    fs->ov = *ov;
    (void)settings_load(&fs->file);
    fs->eff = fs->file;
    apply_overrides(&fs->eff, ov);
    fs->start = fs->eff;
    if (!ov->no_launcher) {
        if (!open_window(fs, window_title)) {
            return FRONT_ERROR;
        }
        char card[1024];
        default_card(card, sizeof(card));
        const LauncherInfo info = {.gpu_shader = ov->gpu_shader, .default_card = card};
        const LauncherResult r = launcher_run(&fs->eff, &info);
        if (r == LAUNCHER_ERROR) {
            AM_LOG_ERROR("tela inicial: %s", platform_last_error());
            return FRONT_ERROR;
        }
        if (r == LAUNCHER_QUIT) {
            AM_LOG_INFO("tela inicial: o jogador saiu");
            front_session_end(fs); /* grava o que o jogador mudou antes de sair */
            return FRONT_QUIT;
        }
        if (!fs->eff.gpu && platform_gpu_driver()[0] != '\0') {
            platform_gpu_shutdown();
        }
    } else {
        platform_window_set_high_dpi(true);
        platform_window_set_fullscreen(fs->eff.fullscreen);
        platform_window_set_vsync(fs->eff.vsync);
        (void)platform_ui_init();
    }
    return resolve_sys(fs) ? FRONT_PLAY : FRONT_ERROR;
}

/* Campo vindo só da linha de comando e não mexido pelo jogador: grava o valor do arquivo. */
#define KEEP_FILE_VALUE(given, field)                                                                                  \
    do {                                                                                                               \
        if ((given) && memcmp(&fs->eff.field, &fs->start.field, sizeof(fs->eff.field)) == 0) {                         \
            memcpy(&out.field, &fs->file.field, sizeof(out.field));                                                    \
        }                                                                                                              \
    } while (0)

void front_session_end(FrontSession *fs) {
    Settings out = fs->eff;
    KEEP_FILE_VALUE(fs->ov.sys_dir != NULL, game_dir);
    KEEP_FILE_VALUE(fs->ov.gpu >= 0, gpu);
    KEEP_FILE_VALUE(fs->ov.window_scale != 0U, window_scale);
    KEEP_FILE_VALUE(fs->ov.fullscreen >= 0, fullscreen);
    KEEP_FILE_VALUE(fs->ov.vsync >= 0, vsync);
    KEEP_FILE_VALUE(fs->ov.fps_overlay >= 0, fps_overlay);
    KEEP_FILE_VALUE(fs->ov.volume >= 0, volume);
    KEEP_FILE_VALUE(fs->ov.memcard != NULL, memcard);
    (void)settings_save(&out);
    platform_ui_shutdown();
}
