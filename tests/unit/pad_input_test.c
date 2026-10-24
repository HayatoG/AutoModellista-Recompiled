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

#define K(k) (1U << (u32)(PLATFORM_KEY_##k))
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
    PlatformInput in = keys(K(X) | K(Z) | K(C) | K(S) | K(D) | K(RETURN));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_A | SI_PAD_B | SI_PAD_X | SI_PAD_Y | SI_PAD_Z | SI_PAD_START);
    in = keys(K(T) | K(H));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_UP | SI_PAD_RIGHT);
    in = keys(K(Q));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_L); /* gatilho de tecla: curso inteiro e clique */
    EXPECT_EQ(pad.trigger_l, 255U);
    EXPECT_EQ(pad.trigger_r, 0U);
}

TEST(keyboard_sticks_full_half_and_cancel) {
    SiPadState pad;
    PlatformInput in = keys(K(RIGHT) | K(UP) | K(J));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 255U);  /* MapFloat: +1 -> 255 */
    EXPECT_EQ(pad.stick_y, 255U);  /* cima = y maior no GameCube */
    EXPECT_EQ(pad.substick_x, 1U); /* -1 -> 1 */
    EXPECT_EQ(pad.substick_y, 0x80U);
    in = keys(K(LEFT) | K(SHIFT));
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.stick_x, 128U - 64U); /* meia inclinação: round(-63.5) = -64 */
    in = keys(K(LEFT) | K(RIGHT));
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
    PlatformInput in = keys(K(X) | K(LEFT));
    in.has_gamepad = true;
    in.pad_buttons = P(NORTH);
    in.pad_axes[PLATFORM_AXIS_LEFT_X] = 16000; /* menos inclinado que a seta: vale a seta */
    SiPadState pad;
    pad_input_map(&in, &pad);
    EXPECT_EQ(pad.buttons, SI_PAD_A | SI_PAD_Y);
    EXPECT_EQ(pad.stick_x, 1U);
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
    RUN_TEST(si_words_per_mode);
    return am_test_finish();
}
