/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/pad_input.h"

#include <math.h>
#include <string.h>

#define KEY(c)  (1U << (u32)(PAD_CONTROL_##c))
#define PADB(b) (1U << (u32)(PLATFORM_PAD_##b))

typedef struct ControlInfo {
    const char *id;
    const char *label;
    const char *key; /* tecla padrão (nome da SDL) */
    u32 pad;         /* botões padrão do gamepad */
    u16 gc;          /* botão do GameCube (0 = eixo ou modificador) */
} ControlInfo;

/* Padrões do Dolphin (GCPadEmu.cpp) e de docs/M7_MENUS.md §3. */
static const ControlInfo s_controls[PAD_CONTROL_COUNT] = {
    [PAD_CONTROL_A] = {"A", "A", "X", PADB(SOUTH), SI_PAD_A},
    [PAD_CONTROL_B] = {"B", "B", "Z", PADB(WEST), SI_PAD_B},
    [PAD_CONTROL_X] = {"X", "X", "C", PADB(EAST), SI_PAD_X},
    [PAD_CONTROL_Y] = {"Y", "Y", "S", PADB(NORTH), SI_PAD_Y},
    [PAD_CONTROL_Z] = {"Z", "Z", "D", PADB(LEFT_SHOULDER) | PADB(RIGHT_SHOULDER), SI_PAD_Z},
    [PAD_CONTROL_START] = {"START", "START", "Return", PADB(START), SI_PAD_START},
    [PAD_CONTROL_UP] = {"DPAD_UP", "Direcional: cima", "T", PADB(DPAD_UP), SI_PAD_UP},
    [PAD_CONTROL_DOWN] = {"DPAD_DOWN", "Direcional: baixo", "G", PADB(DPAD_DOWN), SI_PAD_DOWN},
    [PAD_CONTROL_LEFT] = {"DPAD_LEFT", "Direcional: esquerda", "F", PADB(DPAD_LEFT), SI_PAD_LEFT},
    [PAD_CONTROL_RIGHT] = {"DPAD_RIGHT", "Direcional: direita", "H", PADB(DPAD_RIGHT), SI_PAD_RIGHT},
    [PAD_CONTROL_STICK_UP] = {"STICK_UP", "Stick: cima", "Up", 0, 0},
    [PAD_CONTROL_STICK_DOWN] = {"STICK_DOWN", "Stick: baixo", "Down", 0, 0},
    [PAD_CONTROL_STICK_LEFT] = {"STICK_LEFT", "Stick: esquerda", "Left", 0, 0},
    [PAD_CONTROL_STICK_RIGHT] = {"STICK_RIGHT", "Stick: direita", "Right", 0, 0},
    [PAD_CONTROL_C_UP] = {"C_UP", "Stick C: cima", "I", 0, 0},
    [PAD_CONTROL_C_DOWN] = {"C_DOWN", "Stick C: baixo", "K", 0, 0},
    [PAD_CONTROL_C_LEFT] = {"C_LEFT", "Stick C: esquerda", "J", 0, 0},
    [PAD_CONTROL_C_RIGHT] = {"C_RIGHT", "Stick C: direita", "L", 0, 0},
    [PAD_CONTROL_L] = {"L", "L", "Q", PADB(LEFT_TRIGGER), SI_PAD_L},
    [PAD_CONTROL_R] = {"R", "R", "W", PADB(RIGHT_TRIGGER), SI_PAD_R},
    [PAD_CONTROL_HALF] = {"HALF", "Meia inclinação do stick", "Left Shift", 0, 0},
};

static const struct {
    const char *id;
    const char *label;
} s_buttons[PLATFORM_PAD_BUTTON_COUNT] = {
    [PLATFORM_PAD_SOUTH] = {"south", "Sul (A no Xbox)"},
    [PLATFORM_PAD_EAST] = {"east", "Leste (B no Xbox)"},
    [PLATFORM_PAD_WEST] = {"west", "Oeste (X no Xbox)"},
    [PLATFORM_PAD_NORTH] = {"north", "Norte (Y no Xbox)"},
    [PLATFORM_PAD_BACK] = {"back", "Back/Select"},
    [PLATFORM_PAD_START] = {"start", "Start"},
    [PLATFORM_PAD_LEFT_SHOULDER] = {"leftshoulder", "Ombro esquerdo"},
    [PLATFORM_PAD_RIGHT_SHOULDER] = {"rightshoulder", "Ombro direito"},
    [PLATFORM_PAD_DPAD_UP] = {"dpup", "Direcional: cima"},
    [PLATFORM_PAD_DPAD_DOWN] = {"dpdown", "Direcional: baixo"},
    [PLATFORM_PAD_DPAD_LEFT] = {"dpleft", "Direcional: esquerda"},
    [PLATFORM_PAD_DPAD_RIGHT] = {"dpright", "Direcional: direita"},
    [PLATFORM_PAD_LEFT_STICK] = {"leftstick", "Clique do stick esquerdo"},
    [PLATFORM_PAD_RIGHT_STICK] = {"rightstick", "Clique do stick direito"},
    [PLATFORM_PAD_LEFT_TRIGGER] = {"lefttrigger", "Gatilho esquerdo"},
    [PLATFORM_PAD_RIGHT_TRIGGER] = {"righttrigger", "Gatilho direito"},
};

static PadBindings s_bind = {
    .pad = {PADB(SOUTH),
            PADB(WEST),
            PADB(EAST),
            PADB(NORTH),
            PADB(LEFT_SHOULDER) | PADB(RIGHT_SHOULDER),
            PADB(START),
            PADB(DPAD_UP),
            PADB(DPAD_DOWN),
            PADB(DPAD_LEFT),
            PADB(DPAD_RIGHT),
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            PADB(LEFT_TRIGGER),
            PADB(RIGHT_TRIGGER),
            0},
};

void pad_input_default_bindings(PadBindings *out) {
    for (u32 i = 0; i < (u32)PAD_CONTROL_COUNT; i++) {
        out->pad[i] = s_controls[i].pad;
    }
}

void pad_input_set_bindings(const PadBindings *b) {
    s_bind = *b;
}

/* Dolphin GCPad::GetInput, MapFloat<u8>(x, 0x80, 1, 255): -1 -> 1, 0 -> 128, +1 -> 255. */
static u8 stick_byte(float v) {
    const float c = v < -1.0F ? -1.0F : fminf(v, 1.0F);
    return (u8)((int)SI_PAD_STICK_CENTER + (int)lroundf(c * 127.0F));
}

/* Controle apertado pelo teclado ou pelo gamepad (`pad` já com os gatilhos como botões). */
static bool held(const PlatformInput *in, u32 pad, PadControl c) {
    return (in->keys & (1U << (u32)c)) != 0U || (in->has_gamepad && (pad & s_bind.pad[c]) != 0U);
}

/* Direções opostas: as duas juntas se anulam, como no ControlGroup do Dolphin. */
static float digital_axis(const PlatformInput *in, u32 pad, PadControl neg, PadControl pos) {
    return (held(in, pad, pos) ? 1.0F : 0.0F) - (held(in, pad, neg) ? 1.0F : 0.0F);
}

/* Stick do gamepad: eixo y da SDL cresce para baixo, o do GameCube para cima. Zona morta radial. */
static void pad_stick(const s16 *axes, PlatformPadAxis ax, PlatformPadAxis ay, float *x, float *y) {
    const float fx = (float)axes[ax] / 32767.0F;
    const float fy = -(float)axes[ay] / 32767.0F;
    const float r = sqrtf((fx * fx) + (fy * fy));
    if (r <= PAD_INPUT_DEADZONE) {
        *x = 0.0F;
        *y = 0.0F;
        return;
    }
    const float scale = ((r > 1.0F ? 1.0F : r) - PAD_INPUT_DEADZONE) / (1.0F - PAD_INPUT_DEADZONE) / r;
    *x = fx * scale;
    *y = fy * scale;
}

static float stronger(float a, float b) {
    return fabsf(b) > fabsf(a) ? b : a;
}

static void map_sticks(const PlatformInput *in, u32 pad, SiPadState *out) {
    const float half = held(in, pad, PAD_CONTROL_HALF) ? 0.5F : 1.0F;
    float mx = digital_axis(in, pad, PAD_CONTROL_STICK_LEFT, PAD_CONTROL_STICK_RIGHT) * half;
    float my = digital_axis(in, pad, PAD_CONTROL_STICK_DOWN, PAD_CONTROL_STICK_UP) * half;
    float cx = digital_axis(in, pad, PAD_CONTROL_C_LEFT, PAD_CONTROL_C_RIGHT);
    float cy = digital_axis(in, pad, PAD_CONTROL_C_DOWN, PAD_CONTROL_C_UP);
    if (in->has_gamepad) {
        float px = 0.0F;
        float py = 0.0F;
        pad_stick(in->pad_axes, PLATFORM_AXIS_LEFT_X, PLATFORM_AXIS_LEFT_Y, &px, &py);
        mx = stronger(mx, px);
        my = stronger(my, py);
        pad_stick(in->pad_axes, PLATFORM_AXIS_RIGHT_X, PLATFORM_AXIS_RIGHT_Y, &px, &py);
        cx = stronger(cx, px);
        cy = stronger(cy, py);
    }
    out->stick_x = stick_byte(mx);
    out->stick_y = stick_byte(my);
    out->substick_x = stick_byte(cx);
    out->substick_y = stick_byte(cy);
}

static float trigger_value(const PlatformInput *in, PlatformPadAxis axis) {
    const s16 v = in->pad_axes[axis];
    return v > 0 ? (float)v / 32767.0F : 0.0F;
}

/* Gatilho do GameCube: analógico se ligado a um gatilho do gamepad (o mais apertado), senão curso inteiro quando a
 * tecla ou o botão estão apertados; clique a partir de PAD_INPUT_TRIGGER_CLICK. */
static void map_trigger(const PlatformInput *in, u32 pad, PadControl c, u8 *analog, u16 *buttons) {
    const u32 triggers = PADB(LEFT_TRIGGER) | PADB(RIGHT_TRIGGER);
    const bool digital =
        (in->keys & (1U << (u32)c)) != 0U || (in->has_gamepad && (pad & s_bind.pad[c] & ~triggers) != 0U);
    float v = digital ? 1.0F : 0.0F;
    if (in->has_gamepad && (s_bind.pad[c] & PADB(LEFT_TRIGGER)) != 0U) {
        v = fmaxf(v, trigger_value(in, PLATFORM_AXIS_LEFT_TRIGGER));
    }
    if (in->has_gamepad && (s_bind.pad[c] & PADB(RIGHT_TRIGGER)) != 0U) {
        v = fmaxf(v, trigger_value(in, PLATFORM_AXIS_RIGHT_TRIGGER));
    }
    v = v > 1.0F ? 1.0F : v;
    *analog = (u8)lroundf(v * 255.0F);
    if (v >= PAD_INPUT_TRIGGER_CLICK) {
        *buttons = (u16)(*buttons | s_controls[c].gc);
    }
}

void pad_input_map(const PlatformInput *in, SiPadState *out) {
    *out = si_pad_neutral();
    u32 pad = in->pad_buttons;
    if (trigger_value(in, PLATFORM_AXIS_LEFT_TRIGGER) >= PAD_INPUT_TRIGGER_BUTTON) {
        pad |= PADB(LEFT_TRIGGER);
    }
    if (trigger_value(in, PLATFORM_AXIS_RIGHT_TRIGGER) >= PAD_INPUT_TRIGGER_BUTTON) {
        pad |= PADB(RIGHT_TRIGGER);
    }
    for (u32 c = 0; c <= (u32)PAD_CONTROL_RIGHT; c++) {
        if (held(in, pad, (PadControl)c)) {
            out->buttons = (u16)(out->buttons | s_controls[c].gc);
        }
    }
    map_sticks(in, pad, out);
    map_trigger(in, pad, PAD_CONTROL_L, &out->trigger_l, &out->buttons);
    map_trigger(in, pad, PAD_CONTROL_R, &out->trigger_r, &out->buttons);
}

const char *pad_input_control_id(PadControl c) {
    return s_controls[c].id;
}

const char *pad_input_control_label(PadControl c) {
    return s_controls[c].label;
}

bool pad_input_control_from_id(const char *id, PadControl *out) {
    for (u32 i = 0; i < (u32)PAD_CONTROL_COUNT; i++) {
        if (strcmp(id, s_controls[i].id) == 0) {
            *out = (PadControl)i;
            return true;
        }
    }
    return false;
}

const char *pad_input_default_key(PadControl c) {
    return s_controls[c].key;
}

const char *pad_input_button_id(PlatformPadButton b) {
    return s_buttons[b].id;
}

const char *pad_input_button_label(PlatformPadButton b) {
    return s_buttons[b].label;
}

bool pad_input_button_from_id(const char *id, PlatformPadButton *out) {
    for (u32 i = 0; i < (u32)PLATFORM_PAD_BUTTON_COUNT; i++) {
        if (strcmp(id, s_buttons[i].id) == 0) {
            *out = (PlatformPadButton)i;
            return true;
        }
    }
    return false;
}
