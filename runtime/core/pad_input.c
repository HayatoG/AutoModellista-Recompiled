/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/pad_input.h"

#include <math.h>

#define KEY(k)  (1U << (u32)(PLATFORM_KEY_##k))
#define PADB(b) (1U << (u32)(PLATFORM_PAD_##b))

typedef struct ButtonMap {
    u32 keys;
    u32 pad;
    u16 gc;
} ButtonMap;

static const ButtonMap s_buttons[] = {
    {KEY(X), PADB(SOUTH), SI_PAD_A},
    {KEY(Z), PADB(WEST), SI_PAD_B},
    {KEY(C), PADB(EAST), SI_PAD_X},
    {KEY(S), PADB(NORTH), SI_PAD_Y},
    {KEY(D), PADB(LEFT_SHOULDER) | PADB(RIGHT_SHOULDER), SI_PAD_Z},
    {KEY(RETURN), PADB(START), SI_PAD_START},
    {KEY(T), PADB(DPAD_UP), SI_PAD_UP},
    {KEY(G), PADB(DPAD_DOWN), SI_PAD_DOWN},
    {KEY(F), PADB(DPAD_LEFT), SI_PAD_LEFT},
    {KEY(H), PADB(DPAD_RIGHT), SI_PAD_RIGHT},
};

/* Dolphin GCPad::GetInput, MapFloat<u8>(x, 0x80, 1, 255): -1 -> 1, 0 -> 128, +1 -> 255. */
static u8 stick_byte(float v) {
    const float c = v < -1.0F ? -1.0F : fminf(v, 1.0F);
    return (u8)((int)SI_PAD_STICK_CENTER + (int)lroundf(c * 127.0F));
}

/* Teclas opostas: as duas juntas se anulam, como no ControlGroup do Dolphin. */
static float key_axis(u32 keys, u32 neg, u32 pos) {
    return ((keys & pos) != 0U ? 1.0F : 0.0F) - ((keys & neg) != 0U ? 1.0F : 0.0F);
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

static void map_sticks(const PlatformInput *in, SiPadState *out) {
    const float half = (in->keys & KEY(SHIFT)) != 0U ? 0.5F : 1.0F;
    float mx = key_axis(in->keys, KEY(LEFT), KEY(RIGHT)) * half;
    float my = key_axis(in->keys, KEY(DOWN), KEY(UP)) * half;
    float cx = key_axis(in->keys, KEY(J), KEY(L));
    float cy = key_axis(in->keys, KEY(K), KEY(I));
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

/* Gatilho: tecla = curso inteiro + clique; gamepad = analógico, com clique a partir de PAD_INPUT_TRIGGER_CLICK. */
static void map_trigger(bool key, bool has_pad, s16 axis, u8 *analog, u16 *buttons, u16 click) {
    float v = key ? 1.0F : 0.0F;
    if (has_pad && axis > 0) {
        const float a = (float)axis / 32767.0F;
        v = a > v ? a : v;
    }
    v = v > 1.0F ? 1.0F : v;
    *analog = (u8)lroundf(v * 255.0F);
    if (v >= PAD_INPUT_TRIGGER_CLICK) {
        *buttons = (u16)(*buttons | click);
    }
}

void pad_input_map(const PlatformInput *in, SiPadState *out) {
    *out = si_pad_neutral();
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        const ButtonMap *m = &s_buttons[i];
        if ((in->keys & m->keys) != 0U || (in->has_gamepad && (in->pad_buttons & m->pad) != 0U)) {
            out->buttons = (u16)(out->buttons | m->gc);
        }
    }
    map_sticks(in, out);
    map_trigger((in->keys & KEY(Q)) != 0U, in->has_gamepad, in->pad_axes[PLATFORM_AXIS_LEFT_TRIGGER], &out->trigger_l,
                &out->buttons, SI_PAD_L);
    map_trigger((in->keys & KEY(W)) != 0U, in->has_gamepad, in->pad_axes[PLATFORM_AXIS_RIGHT_TRIGGER], &out->trigger_r,
                &out->buttons, SI_PAD_R);
}
