/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/pad_script.h"

#include <stdlib.h>
#include <string.h>

/* Máscaras do SDK (PAD_BUTTON_*), as mesmas de am/si.h. */
static const struct {
    const char *name;
    u16 mask;
} s_buttons[] = {
    {"D_LEFT", 0x0001U}, {"D_RIGHT", 0x0002U}, {"D_DOWN", 0x0004U}, {"D_UP", 0x0008U}, {"Z", 0x0010U},
    {"A", 0x0100U},      {"B", 0x0200U},       {"X", 0x0400U},      {"Y", 0x0800U},    {"START", 0x1000U},
};

static bool button_mask(const char *name, size_t len, u16 *mask) {
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        if (strlen(s_buttons[i].name) == len && strncmp(s_buttons[i].name, name, len) == 0) {
            *mask = s_buttons[i].mask;
            return true;
        }
    }
    return false;
}

static bool parse_number(const char **p, u64 *out) {
    char *end = NULL;
    const unsigned long long v = strtoull(*p, &end, 10);
    if (end == *p) {
        return false;
    }
    *out = (u64)v;
    *p = end;
    return true;
}

static bool parse_step(const char **p, PadScriptStep *step) {
    if (!parse_number(p, &step->frame) || **p != ':') {
        return false;
    }
    (*p)++;
    step->buttons = 0;
    for (;;) {
        const char *start = *p;
        while (**p != '\0' && **p != '+' && **p != ':') {
            (*p)++;
        }
        u16 mask = 0;
        if (!button_mask(start, (size_t)(*p - start), &mask)) {
            return false;
        }
        step->buttons = (u16)(step->buttons | mask);
        if (**p != '+') {
            break;
        }
        (*p)++;
    }
    if (**p != ':') {
        return false;
    }
    (*p)++;
    return parse_number(p, &step->hold) && step->hold > 0U;
}

bool pad_script_parse(const char *text, PadScript *out) {
    memset(out, 0, sizeof(*out));
    const char *p = text;
    while (*p != '\0') {
        if (out->count == PAD_SCRIPT_MAX || !parse_step(&p, &out->steps[out->count])) {
            return false;
        }
        out->count++;
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return false;
        }
    }
    return true;
}

u16 pad_script_buttons(const PadScript *script, u64 frame) {
    u16 buttons = 0;
    for (u32 i = 0; i < script->count; i++) {
        const PadScriptStep *s = &script->steps[i];
        if (frame >= s->frame && frame - s->frame < s->hold) {
            buttons = (u16)(buttons | s->buttons);
        }
    }
    return buttons;
}
