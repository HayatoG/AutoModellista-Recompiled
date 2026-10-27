/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/log.h"
#include "am/platform.h"

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->gpu = true;
    s->window_scale = 2;
    s->volume = 100;
    for (u32 i = 0; i < (u32)PAD_CONTROL_COUNT; i++) {
        (void)snprintf(s->keys[i], sizeof(s->keys[i]), "%s", pad_input_default_key((PadControl)i));
    }
    pad_input_default_bindings(&s->pad);
}

const char *settings_filter_name(u32 filter) {
    static const char *const names[] = {"nearest", "linear", "sharp"};
    return names[filter < (u32)PLATFORM_FILTER_COUNT ? filter : 0U];
}

static bool parse_bool(const char *v, bool *out) {
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0) {
        *out = true;
    } else if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0) {
        *out = false;
    } else {
        return false;
    }
    return true;
}

static bool parse_uint(const char *v, u32 lo, u32 hi, u32 *out) {
    char *end = NULL;
    const unsigned long n = strtoul(v, &end, 10);
    if (end == v || *end != '\0' || n < lo || n > hi) {
        return false;
    }
    *out = (u32)n;
    return true;
}

/* "south+leftshoulder" -> bits; vazio = nenhum botão. */
static bool parse_buttons(const char *v, u32 *out) {
    u32 mask = 0;
    char buf[256];
    (void)snprintf(buf, sizeof(buf), "%s", v);
    for (char *tok = buf; *tok != '\0';) {
        char *plus = strchr(tok, '+');
        if (plus != NULL) {
            *plus = '\0';
        }
        PlatformPadButton b;
        if (!pad_input_button_from_id(tok, &b)) {
            return false;
        }
        mask |= 1U << (u32)b;
        tok = plus != NULL ? plus + 1 : tok + strlen(tok);
    }
    *out = mask;
    return true;
}

static bool copy_value(char *dst, size_t size, const char *v) {
    const int n = snprintf(dst, size, "%s", v);
    return n >= 0 && (size_t)n < size;
}

static bool parse_control(Settings *s, const char *section, const char *key, const char *v) {
    PadControl c;
    if (!pad_input_control_from_id(key, &c)) {
        return false;
    }
    if (strcmp(section, "keyboard") == 0) {
        return copy_value(s->keys[c], sizeof(s->keys[c]), v);
    }
    return strcmp(section, "gamepad") == 0 && parse_buttons(v, &s->pad.pad[c]);
}

static bool parse_pair(Settings *s, const char *section, const char *key, const char *v) {
    if (strcmp(section, "keyboard") == 0 || strcmp(section, "gamepad") == 0) {
        return parse_control(s, section, key, v);
    }
    char full[96];
    (void)snprintf(full, sizeof(full), "%s.%s", section, key);
    if (strcmp(full, "game.dir") == 0) {
        return copy_value(s->game_dir, sizeof(s->game_dir), v);
    }
    if (strcmp(full, "game.memcard") == 0) {
        return copy_value(s->memcard, sizeof(s->memcard), v);
    }
    if (strcmp(full, "video.backend") == 0 && (strcmp(v, "gpu") == 0 || strcmp(v, "soft") == 0)) {
        s->gpu = strcmp(v, "gpu") == 0;
        return true;
    }
    if (strcmp(full, "video.scale") == 0) {
        return parse_uint(v, 1, SETTINGS_SCALE_MAX, &s->window_scale);
    }
    if (strcmp(full, "video.fullscreen") == 0) {
        return parse_bool(v, &s->fullscreen);
    }
    if (strcmp(full, "video.vsync") == 0) {
        return parse_bool(v, &s->vsync);
    }
    if (strcmp(full, "video.filter") == 0) {
        for (u32 f = 0; f < (u32)PLATFORM_FILTER_COUNT; f++) {
            if (strcmp(v, settings_filter_name(f)) == 0) {
                s->filter = f;
                return true;
            }
        }
        return false;
    }
    if (strcmp(full, "video.fps_overlay") == 0) {
        return parse_bool(v, &s->fps_overlay);
    }
    if (strcmp(full, "audio.volume") == 0) {
        return parse_uint(v, 0, 100, &s->volume);
    }
    if (strcmp(full, "input.gamepad") == 0) {
        return copy_value(s->gamepad, sizeof(s->gamepad), v);
    }
    return false;
}

static char *trim(char *p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    size_t n = strlen(p);
    while (n != 0U && (p[n - 1U] == ' ' || p[n - 1U] == '\t' || p[n - 1U] == '\r')) {
        p[--n] = '\0';
    }
    return p;
}

/* Uma linha já sem o '\n'. Comentário com '#' ou ';' no começo; seção "[nome]"; "chave = valor". */
static bool parse_line(Settings *s, char *line, char *section, size_t section_size) {
    char *p = trim(line);
    if (*p == '\0' || *p == '#' || *p == ';') {
        return true;
    }
    if (*p == '[') {
        char *end = strchr(p, ']');
        if (end == NULL) {
            return false;
        }
        *end = '\0';
        return copy_value(section, section_size, trim(p + 1));
    }
    char *eq = strchr(p, '=');
    if (eq == NULL) {
        return false;
    }
    *eq = '\0';
    return parse_pair(s, section, trim(p), trim(eq + 1));
}

u32 settings_parse(const char *text, size_t size, Settings *s) {
    u32 bad = 0;
    char section[32] = "";
    size_t at = 0;
    while (at < size) {
        size_t end = at;
        while (end < size && text[end] != '\n') {
            end++;
        }
        char line[SETTINGS_PATH_SIZE + 64U];
        const size_t len = end - at;
        if (len < sizeof(line)) {
            memcpy(line, text + at, len);
            line[len] = '\0';
            bad += parse_line(s, line, section, sizeof(section)) ? 0U : 1U;
        } else {
            bad++;
        }
        at = end + 1U;
    }
    return bad;
}

typedef struct Out {
    char *buf;
    size_t size;
    size_t used;
    bool full;
} Out;

static void put(Out *o, const char *fmt, ...) AM_PRINTF(2, 3);

static void put(Out *o, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(o->buf + o->used, o->size - o->used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->size - o->used) {
        o->full = true;
        return;
    }
    o->used += (size_t)n;
}

static void put_buttons(Out *o, const char *id, u32 mask) {
    put(o, "%s = ", id);
    bool first = true;
    for (u32 b = 0; b < (u32)PLATFORM_PAD_BUTTON_COUNT; b++) {
        if ((mask & (1U << b)) != 0U) {
            put(o, "%s%s", first ? "" : "+", pad_input_button_id((PlatformPadButton)b));
            first = false;
        }
    }
    put(o, "\n");
}

size_t settings_format(const Settings *s, char *out, size_t size) {
    Out o = {out, size, 0, size == 0U};
    put(&o, "# Auto Modellista Recompiled: configurações (docs/M9_PARTE5_LAUNCHER.md). A linha de comando tem\n"
            "# prioridade. Teclas pelo nome da SDL; botões do gamepad por posição, juntos com '+'.\n");
    put(&o, "\n[game]\ndir = %s\nmemcard = %s\n", s->game_dir, s->memcard);
    put(&o, "\n[video]\nbackend = %s\nscale = %u\nfullscreen = %d\nvsync = %d\nfilter = %s\nfps_overlay = %d\n",
        s->gpu ? "gpu" : "soft", s->window_scale, s->fullscreen ? 1 : 0, s->vsync ? 1 : 0,
        settings_filter_name(s->filter), s->fps_overlay ? 1 : 0);
    put(&o, "\n[audio]\nvolume = %u\n", s->volume);
    put(&o, "\n[input]\ngamepad = %s\n", s->gamepad);
    put(&o, "\n[keyboard]\n");
    for (u32 i = 0; i < (u32)PAD_CONTROL_COUNT; i++) {
        put(&o, "%s = %s\n", pad_input_control_id((PadControl)i), s->keys[i]);
    }
    put(&o, "\n[gamepad]\n");
    for (u32 i = 0; i < (u32)PAD_CONTROL_COUNT; i++) {
        put_buttons(&o, pad_input_control_id((PadControl)i), s->pad.pad[i]);
    }
    return o.full ? 0U : o.used;
}

bool settings_path(char *out, size_t size) {
    char dir[SETTINGS_PATH_SIZE];
    if (!platform_config_dir(dir, sizeof(dir))) {
        return false;
    }
    const int n = snprintf(out, size, "%s%s", dir, SETTINGS_FILE);
    return n > 0 && (size_t)n < size;
}

bool settings_load(Settings *s) {
    settings_defaults(s);
    char path[SETTINGS_PATH_SIZE + 32U];
    size_t size = 0;
    u8 *text = settings_path(path, sizeof(path)) ? platform_load_file(path, &size) : NULL;
    if (text == NULL) {
        return false;
    }
    const u32 bad = settings_parse((const char *)text, size, s);
    platform_free_file(text);
    if (bad != 0U) {
        AM_LOG_WARN("%s: %u linha(s) ignorada(s)", path, bad);
    }
    AM_LOG_INFO("configurações lidas de %s", path);
    return true;
}

bool settings_save(const Settings *s) {
    char path[SETTINGS_PATH_SIZE + 32U];
    static char text[16384];
    const size_t n = settings_format(s, text, sizeof(text));
    if (n == 0U || !settings_path(path, sizeof(path)) || !platform_save_file(path, (const u8 *)text, n)) {
        AM_LOG_WARN("não consegui gravar as configurações: %s", platform_last_error());
        return false;
    }
    return true;
}
