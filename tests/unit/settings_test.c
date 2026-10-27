/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9 parte 5: o settings.ini (leitura, escrita e volta) e o banner do disco decodificado pelo GX (bytes sintéticos). */
#include <string.h>

#include "am/banner.h"
#include "am/pad_input.h"
#include "am/settings.h"

#include "am_test.h"

TEST(defaults_follow_dolphin) {
    Settings s;
    settings_defaults(&s);
    EXPECT_TRUE(s.gpu);
    EXPECT_EQ(s.window_scale, 2U);
    EXPECT_EQ(s.volume, 100U);
    EXPECT_EQ(s.filter, 0U); /* nearest: a janela estica a imagem como antes */
    EXPECT_TRUE(s.ui_glass); /* Liquid Glass, 24 px: o padrão do design */
    EXPECT_EQ(s.ui_blur, 24U);
    EXPECT_TRUE(strcmp(s.keys[PAD_CONTROL_A], "X") == 0);
    EXPECT_TRUE(strcmp(s.keys[PAD_CONTROL_START], "Return") == 0);
    EXPECT_TRUE(strcmp(s.keys[PAD_CONTROL_HALF], "Left Shift") == 0);
    EXPECT_EQ(s.pad.pad[PAD_CONTROL_Z], (1U << PLATFORM_PAD_LEFT_SHOULDER) | (1U << PLATFORM_PAD_RIGHT_SHOULDER));
}

TEST(parse_overrides_and_counts_bad_lines) {
    static const char text[] = "# comentário\n"
                               "[game]\n dir = /jogos/am \n"
                               "[video]\nbackend = soft\nscale = 3\nfullscreen = 1\nvsync = on\nfps_overlay = true\n"
                               "scale = 99\n"
                               "filter = sharp\nfilter = pixelart\n"
                               "[audio]\nvolume = 35\n"
                               "[input]\ngamepad = Xbox Wireless Controller\n"
                               "[keyboard]\nA = Space\n"
                               "[gamepad]\nA = east+south\nB =\nZ = nada\n"
                               "linha sem igual\n";
    Settings s;
    settings_defaults(&s);
    const u32 bad = settings_parse(text, sizeof(text) - 1U, &s);
    EXPECT_EQ(bad, 4U);      /* scale = 99, filter = pixelart, Z = nada, linha sem igual */
    EXPECT_EQ(s.filter, 2U); /* sharp; o valor inválido não mexe */
    EXPECT_TRUE(strcmp(s.game_dir, "/jogos/am") == 0);
    EXPECT_TRUE(!s.gpu);
    EXPECT_EQ(s.window_scale, 3U);
    EXPECT_TRUE(s.fullscreen && s.vsync && s.fps_overlay);
    EXPECT_EQ(s.volume, 35U);
    EXPECT_TRUE(strcmp(s.gamepad, "Xbox Wireless Controller") == 0);
    EXPECT_TRUE(strcmp(s.keys[PAD_CONTROL_A], "Space") == 0);
    EXPECT_EQ(s.pad.pad[PAD_CONTROL_A], (1U << PLATFORM_PAD_EAST) | (1U << PLATFORM_PAD_SOUTH));
    EXPECT_EQ(s.pad.pad[PAD_CONTROL_B], 0U);
    EXPECT_EQ(s.pad.pad[PAD_CONTROL_Z], (1U << PLATFORM_PAD_LEFT_SHOULDER) | (1U << PLATFORM_PAD_RIGHT_SHOULDER));
}

TEST(ui_style_keys) {
    Settings s;
    settings_defaults(&s);
    static const char ok[] = "[ui]\nstyle = solid\nglass = 40\n";
    EXPECT_EQ(settings_parse(ok, sizeof(ok) - 1U, &s), 0U);
    EXPECT_TRUE(!s.ui_glass);
    EXPECT_EQ(s.ui_blur, 40U);
    /* fora da faixa ou desconhecido: ignorado e contado, sem mexer no valor */
    static const char bad[] = "[ui]\nstyle = acrilico\nglass = 3\nglass = 49\nglass = muito\n";
    EXPECT_EQ(settings_parse(bad, sizeof(bad) - 1U, &s), 4U);
    EXPECT_TRUE(!s.ui_glass);
    EXPECT_EQ(s.ui_blur, 40U);
    static const char edges[] = "[ui]\nstyle = glass\nglass = 4\n";
    EXPECT_EQ(settings_parse(edges, sizeof(edges) - 1U, &s), 0U);
    EXPECT_TRUE(s.ui_glass && s.ui_blur == SETTINGS_BLUR_MIN);
    static char text[16384];
    EXPECT_TRUE(settings_format(&s, text, sizeof(text)) > 0U);
    EXPECT_TRUE(strstr(text, "[ui]\nstyle = glass\nglass = 4\n") != NULL);
    /* "sem cartão" é o memcard = none que a linha de comando já aceita */
    static const char card[] = "[game]\nmemcard = none\n";
    EXPECT_EQ(settings_parse(card, sizeof(card) - 1U, &s), 0U);
    EXPECT_TRUE(strcmp(s.memcard, "none") == 0);
}

TEST(format_round_trip) {
    Settings a;
    settings_defaults(&a);
    (void)strcpy(a.game_dir, "C:\\Jogos\\Auto Modellista");
    (void)strcpy(a.memcard, "/tmp/cartao.raw");
    a.gpu = false;
    a.volume = 0;
    a.filter = 1;
    a.ui_glass = false;
    a.ui_blur = 48;
    a.pad.pad[PAD_CONTROL_L] = 1U << PLATFORM_PAD_LEFT_SHOULDER;
    (void)strcpy(a.keys[PAD_CONTROL_C_UP], "Keypad 8");
    static char text[16384];
    const size_t n = settings_format(&a, text, sizeof(text));
    EXPECT_TRUE(n > 0U);
    Settings b;
    settings_defaults(&b);
    EXPECT_EQ(settings_parse(text, n, &b), 0U);
    EXPECT_TRUE(memcmp(&a, &b, sizeof(a)) == 0);
    EXPECT_EQ(settings_format(&a, text, 64), 0U); /* não coube */
}

/* Banner de 96x32 em RGB5A3 (blocos 4x4): o texel (0,0) opaco vermelho, o (5,1) com alfa; o nome longo em 0x1860. */
TEST(banner_decodes_rgb5a3_tiles) {
    static u8 bnr[0x1960];
    memset(bnr, 0, sizeof(bnr));
    memcpy(bnr, "BNR1", 4);
    bnr[0x20] = 0xFC; /* 1 11111 00000 00000: vermelho opaco */
    bnr[0x21] = 0x00;
    const u32 tile = 1U; /* (5,1): bloco 1 (x 4..7), linha 1, coluna 1 */
    const u32 at = 0x20U + (tile * 32U) + ((1U * 4U) + 1U) * 2U;
    bnr[at] = 0x30; /* 0 011 0000 0000 1111: alfa 3/7, azul */
    bnr[at + 1U] = 0x0F;
    memcpy(bnr + 0x1860, "  auto modellista  ", 19);
    Banner b;
    EXPECT_TRUE(banner_decode(bnr, sizeof(bnr), &b));
    EXPECT_EQ(b.rgba[0], 255U);
    EXPECT_EQ(b.rgba[1], 0U);
    EXPECT_EQ(b.rgba[3], 255U);
    const u8 *p = &b.rgba[((1U * BANNER_WIDTH) + 5U) * 4U];
    EXPECT_EQ(p[2], 255U);
    EXPECT_TRUE(p[3] > 0U && p[3] < 255U);
    EXPECT_TRUE(strcmp(b.name, "auto modellista") == 0);
    bnr[0] = 'X';
    EXPECT_TRUE(!banner_decode(bnr, sizeof(bnr), &b));
}

int main(void) {
    RUN_TEST(defaults_follow_dolphin);
    RUN_TEST(parse_overrides_and_counts_bad_lines);
    RUN_TEST(ui_style_keys);
    RUN_TEST(format_round_trip);
    RUN_TEST(banner_decodes_rgb5a3_tiles);
    return am_test_finish();
}
