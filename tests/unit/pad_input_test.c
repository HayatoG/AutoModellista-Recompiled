/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mapeamento teclado/gamepad -> controle de GameCube (core/pad_input.c) e as palavras que o SI entrega por modo
 * (hw/si.c, CSIDevice_GCController::GetData do Dolphin). Entradas montadas à mão. */
#include "am/pad_input.h"
#include "am/si.h"

#include "am_test.h"

static PlatformInput keys(u32 bits) {
    PlatformInput in = {0};
    in.keys = bits;
    return in;
}

/* Bit da tecla de cada controle (com as teclas padrão: X = A, Z = B, C = X, S = Y, D = Z, Return = START, T G F H =
 * direcional, setas = stick, I K J L = stick C, Q W = L R, Shift = meia inclinação). */
#define K(c) (1U << (u32)(PAD_CONTROL_##c))
#define P(b) (1U << (u32)(PLATFORM_PAD_##b))

TEST(nothing_pressed_is_neutral) {
    const PlatformInput in = {0};
    SiPadState pad;
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, 0U);
    EXPECT_EQ(pad.stick_x, 0x80U);
    EXPECT_EQ(pad.stick_y, 0x80U);
    EXPECT_EQ(pad.substick_x, 0x80U);
    EXPECT_EQ(pad.substick_y, 0x80U);
    EXPECT_EQ(pad.trigger_l, 0U);
    EXPECT_EQ(pad.trigger_r, 0U);
}

TEST(keyboard_buttons_follow_dolphin_defaults) {
    SiPadState pad;
    PlatformInput in = keys(K(A) | K(B) | K(X) | K(Y) | K(Z) | K(START));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_A | SI_PAD_B | SI_PAD_X | SI_PAD_Y | SI_PAD_Z | SI_PAD_START);
    in = keys(K(UP) | K(RIGHT));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_UP | SI_PAD_RIGHT);
    in = keys(K(L));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_L); /* gatilho de tecla: curso inteiro e clique */
    EXPECT_EQ(pad.trigger_l, 255U);
    EXPECT_EQ(pad.trigger_r, 0U);
}

TEST(keyboard_sticks_full_half_and_cancel) {
    SiPadState pad;
    PlatformInput in = keys(K(STICK_RIGHT) | K(STICK_UP) | K(C_LEFT));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 255U);  /* MapFloat: +1 -> 255 */
    EXPECT_EQ(pad.stick_y, 255U);  /* cima = y maior no GameCube */
    EXPECT_EQ(pad.substick_x, 1U); /* -1 -> 1 */
    EXPECT_EQ(pad.substick_y, 0x80U);
    in = keys(K(STICK_LEFT) | K(HALF));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 128U - 64U); /* meia inclinação: round(-63.5) = -64 */
    in = keys(K(STICK_LEFT) | K(STICK_RIGHT));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 0x80U);
}

TEST(gamepad_positions_deadzone_and_triggers) {
    PlatformInput in = {0};
    in.has_gamepad = true;
    in.pad_buttons = P(SOUTH) | P(WEST) | P(RIGHT_SHOULDER) | P(DPAD_DOWN);
    in.pad_axes[PLATFORM_AXIS_LEFT_Y] = -32767; /* para cima na SDL */
    in.pad_axes[PLATFORM_AXIS_RIGHT_X] = 32767;
    in.pad_axes[PLATFORM_AXIS_LEFT_TRIGGER] = 16384;  /* meio curso: sem clique */
    in.pad_axes[PLATFORM_AXIS_RIGHT_TRIGGER] = 32000; /* 97%: clique */
    SiPadState pad;
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_A | SI_PAD_B | SI_PAD_Z | SI_PAD_DOWN | SI_PAD_R);
    EXPECT_EQ(pad.stick_y, 255U);
    EXPECT_EQ(pad.stick_x, 0x80U);
    EXPECT_EQ(pad.substick_x, 255U);
    EXPECT_EQ(pad.trigger_l, 128U);
    EXPECT_EQ(pad.trigger_r, 249U);
    in.pad_axes[PLATFORM_AXIS_LEFT_X] = 3000; /* 9%: dentro da zona morta radial */
    in.pad_axes[PLATFORM_AXIS_LEFT_Y] = 0;
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 0x80U);
    EXPECT_EQ(pad.stick_y, 0x80U);
    in.pad_axes[PLATFORM_AXIS_LEFT_X] = 32767; /* curso inteiro depois da zona morta: 255 */
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 255U);
    in.has_gamepad = false; /* sem gamepad, os campos dele não contam */
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, 0U);
    EXPECT_EQ(pad.stick_y, 0x80U);
}

TEST(keyboard_and_gamepad_combine) {
    PlatformInput in = keys(K(A) | K(STICK_LEFT));
    in.has_gamepad = true;
    in.pad_buttons = P(NORTH);
    in.pad_axes[PLATFORM_AXIS_LEFT_X] = 16000; /* menos inclinado que a seta: vale a seta */
    SiPadState pad;
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_A | SI_PAD_Y);
    EXPECT_EQ(pad.stick_x, 1U);
}

TEST(remapped_bindings) {
    PadBindings b;
    pad_input_default_bindings(&b);
    b.pad[PAD_CONTROL_A] = P(EAST);
    b.pad[PAD_CONTROL_X] = P(SOUTH);
    b.pad[PAD_CONTROL_Z] = P(RIGHT_SHOULDER);
    b.pad[PAD_CONTROL_UP] = 0;
    b.pad[PAD_CONTROL_STICK_UP] = P(DPAD_UP);    /* direcional do gamepad no stick */
    b.pad[PAD_CONTROL_L] = P(LEFT_SHOULDER);     /* L digital num botão: curso inteiro e clique */
    b.pad[PAD_CONTROL_START] = P(RIGHT_TRIGGER); /* gatilho como botão: a partir da metade */
    pad_input_set_bindings(&b);
    PlatformInput in = {0};
    in.has_gamepad = true;
    in.pad_buttons = P(EAST) | P(DPAD_UP) | P(LEFT_SHOULDER);
    in.pad_axes[PLATFORM_AXIS_RIGHT_TRIGGER] = 20000;
    SiPadState pad;
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_A | SI_PAD_L | SI_PAD_START);
    EXPECT_EQ(pad.stick_y, 255U);
    EXPECT_EQ(pad.trigger_l, 255U);
    EXPECT_EQ(pad.trigger_r, 156U); /* o gatilho direito vale para os dois: R analógico (61%, sem clique) e START */
    in.pad_buttons = P(SOUTH);
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_X | SI_PAD_START); /* sul agora é o X */
    pad_input_default_bindings(&b);
    pad_input_set_bindings(&b);
}

TEST(names_round_trip) {
    for (u32 c = 0; c < (u32)PAD_CONTROL_COUNT; c++) {
        PadControl back = PAD_CONTROL_COUNT;
        EXPECT_TRUE(pad_input_control_from_id(pad_input_control_id((PadControl)c), &back) && back == (PadControl)c);
        EXPECT_TRUE(pad_input_default_key((PadControl)c)[0] != '\0');
    }
    for (u32 b = 0; b < (u32)PLATFORM_PAD_BUTTON_COUNT; b++) {
        PlatformPadButton back = PLATFORM_PAD_BUTTON_COUNT;
        EXPECT_TRUE(pad_input_button_from_id(pad_input_button_id((PlatformPadButton)b), &back) &&
                    back == (PlatformPadButton)b);
    }
}

TEST(si_words_per_mode) {
    SiPadState pad = si_pad_neutral();
    u32 hi = 0;
    u32 lo = 0;
    si_pad_encode(&pad, 3, &hi, &lo);
    EXPECT_EQ(hi, SI_PAD_NEUTRAL_HI);
    EXPECT_EQ(lo, SI_PAD_NEUTRAL_LO_MODE3);
    pad.buttons = SI_PAD_A | SI_PAD_L;
    pad.stick_x = 0x12;
    pad.stick_y = 0x34;
    pad.substick_x = 0x56;
    pad.substick_y = 0x78;
    pad.trigger_l = 0x9A;
    pad.trigger_r = 0xBC;
    si_pad_encode(&pad, 3, &hi, &lo);
    EXPECT_EQ(hi, 0x01C01234U); /* botões | PAD_USE_ORIGIN, x, y */
    EXPECT_EQ(lo, 0x56789ABCU);
    si_pad_encode(&pad, 0, &hi, &lo);
    EXPECT_EQ(lo, 0x56789B00U); /* gatilhos com 4 bits; analogA/B = 0 */
    si_pad_encode(&pad, 1, &hi, &lo);
    EXPECT_EQ(lo, 0x579ABC00U);
    si_pad_encode(&pad, 2, &hi, &lo);
    EXPECT_EQ(lo, 0x579B0000U);
    si_pad_encode(&pad, 4, &hi, &lo);
    EXPECT_EQ(lo, 0x56780000U);
}

int main(void) {
    RUN_TEST(nothing_pressed_is_neutral);
    RUN_TEST(keyboard_buttons_follow_dolphin_defaults);
    RUN_TEST(keyboard_sticks_full_half_and_cancel);
    RUN_TEST(gamepad_positions_deadzone_and_triggers);
    RUN_TEST(keyboard_and_gamepad_combine);
    RUN_TEST(remapped_bindings);
    RUN_TEST(names_round_trip);
    RUN_TEST(si_words_per_mode);
    return am_test_finish();
}
