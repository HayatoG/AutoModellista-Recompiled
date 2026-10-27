/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M9 parte 5: contas da tela inicial (escala do layout, cover do fundo, miniatura do vidro). */
#include <math.h>
#include <string.h>

#include "am/launcher_gfx.h"

#include "am_test.h"

static bool near(float a, float b) {
    return fabsf(a - b) < 1e-3F;
}

TEST(scale_follows_the_smaller_axis) {
    EXPECT_TRUE(near(launcher_scale(1280.0F, 760.0F), 1.0F));
    EXPECT_TRUE(near(launcher_scale(2560.0F, 1520.0F), 2.0F));
    EXPECT_TRUE(near(launcher_scale(3840.0F, 2160.0F), 2160.0F / 760.0F)); /* a altura manda */
    EXPECT_TRUE(near(launcher_scale(1280.0F, 896.0F), 1.0F));              /* janela 2x do jogo: a largura manda */
    EXPECT_TRUE(near(launcher_scale(100.0F, 100.0F), 0.4F));               /* piso */
    EXPECT_TRUE(near(launcher_scale(99999.0F, 99999.0F), 8.0F));           /* teto */
}

TEST(cover_fills_and_centres) {
    /* 16:9 numa janela 4:3: sobra dos lados, a altura enche */
    LauncherRect r = launcher_cover(640.0F, 480.0F, 1920U, 1080U);
    EXPECT_TRUE(near(r.h, 480.0F));
    EXPECT_TRUE(near(r.w, 480.0F * 16.0F / 9.0F));
    EXPECT_TRUE(near(r.x, (640.0F - r.w) * 0.5F) && r.x < 0.0F);
    EXPECT_TRUE(near(r.y, 0.0F));
    /* janela mais larga que a imagem: a largura enche e corta em cima e embaixo */
    r = launcher_cover(1920.0F, 600.0F, 1920U, 1080U);
    EXPECT_TRUE(near(r.w, 1920.0F) && near(r.h, 1080.0F) && near(r.y, -240.0F));
    /* mesma proporção: exatamente a janela */
    r = launcher_cover(1280.0F, 720.0F, 3840U, 2160U);
    EXPECT_TRUE(near(r.x, 0.0F) && near(r.y, 0.0F) && near(r.w, 1280.0F) && near(r.h, 720.0F));
}

TEST(glass_width_shrinks_with_blur) {
    EXPECT_TRUE(launcher_glass_width(4U) > launcher_glass_width(24U));
    EXPECT_TRUE(launcher_glass_width(24U) > launcher_glass_width(48U));
    EXPECT_EQ(launcher_glass_width(0U), launcher_glass_width(4U)); /* fora da faixa: aperta */
    EXPECT_EQ(launcher_glass_width(500U), launcher_glass_width(48U));
    EXPECT_TRUE(launcher_glass_width(24U) >= 64U && launcher_glass_width(24U) <= 256U);
}

TEST(glass_passes_grow_with_blur_and_width) {
    EXPECT_TRUE(launcher_glass_passes(48U, 200U) > launcher_glass_passes(24U, 200U));
    EXPECT_TRUE(launcher_glass_passes(24U, 400U) > launcher_glass_passes(24U, 200U));
    EXPECT_TRUE(launcher_glass_passes(4U, 20U) >= 1U && launcher_glass_passes(48U, 4000U) <= 40U);
}

TEST(box_reduce_averages_exactly) {
    /* 4x2: esquerda preta, direita branca; para 2x1 cada metade é uma média de 2x2 */
    u8 src[4 * 2 * 4];
    for (u32 i = 0; i < 8U; i++) {
        const u8 v = (i % 4U) < 2U ? 0U : 200U;
        src[i * 4U + 0U] = v;
        src[i * 4U + 1U] = v;
        src[i * 4U + 2U] = v;
        src[i * 4U + 3U] = 255U;
    }
    u8 dst[2 * 1 * 4];
    launcher_box_reduce(src, 4U, 2U, dst, 2U, 1U);
    EXPECT_EQ(dst[0], 0U);
    EXPECT_EQ(dst[4], 200U);
    EXPECT_EQ(dst[3], 255U);
    /* razão fracionária: 3 pixels (0, 90, 180) viram 2: pesos 1 e 0.5 / 0.5 e 1 */
    u8 s3[3 * 4] = {0, 0, 0, 255, 90, 90, 90, 255, 180, 180, 180, 255};
    u8 d2[2 * 4];
    launcher_box_reduce(s3, 3U, 1U, d2, 2U, 1U);
    EXPECT_EQ(d2[0], 30U);  /* (0*1 + 90*0.5) / 1.5 */
    EXPECT_EQ(d2[4], 150U); /* (90*0.5 + 180*1) / 1.5 */
    /* ampliar repete o pixel */
    u8 one[4] = {10, 20, 30, 255};
    u8 big[2 * 2 * 4];
    launcher_box_reduce(one, 1U, 1U, big, 2U, 2U);
    EXPECT_TRUE(memcmp(big, one, 3) == 0 && memcmp(big + 12, one, 3) == 0);
}

TEST(glass_finish_keeps_grey_and_saturates) {
    u8 grey[3 * 3 * 4];
    for (u32 i = 0; i < 9U; i++) {
        memset(&grey[i * 4U], 100, 3U);
        grey[i * 4U + 3U] = 255U;
    }
    launcher_glass_finish(grey, 3U, 3U, 2U, 1.7F);
    EXPECT_EQ(grey[16], 100U); /* cinza não muda com a saturação */
    EXPECT_EQ(grey[19], 255U);
    u8 red[1 * 1 * 4] = {200, 100, 100, 255};
    launcher_glass_finish(red, 1U, 1U, 1U, 1.7F);
    EXPECT_TRUE(red[0] > 200U && red[1] < 100U); /* o vermelho fica mais vermelho */
}

int main(void) {
    RUN_TEST(scale_follows_the_smaller_axis);
    RUN_TEST(cover_fills_and_centres);
    RUN_TEST(glass_width_shrinks_with_blur);
    RUN_TEST(glass_passes_grow_with_blur_and_width);
    RUN_TEST(box_reduce_averages_exactly);
    RUN_TEST(glass_finish_keeps_grey_and_saturates);
    return am_test_finish();
}
