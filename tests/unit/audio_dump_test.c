/* SPDX-License-Identifier: GPL-3.0-or-later */
/* --audio-dump (audio/audio_dump.c): cabeçalho WAV de 44 bytes como o WaveFileWriter do Dolphin e amostras L, R em
 * little-endian; os tamanhos são corrigidos no fechamento. */
#include <stdio.h>
#include <string.h>

#include "am/audio_dump.h"

#include "am_test.h"

static u32 le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

TEST(header_fields) {
    u8 h[44];
    audio_dump_header(h, 32028, 400);
    EXPECT_EQ(memcmp(h, "RIFF", 4), 0);
    EXPECT_EQ(le32(h + 4), 436U);
    EXPECT_EQ(memcmp(h + 8, "WAVEfmt ", 8), 0);
    EXPECT_EQ(le32(h + 16), 16U);
    EXPECT_EQ(le32(h + 20), 0x00020001U); /* PCM, estéreo */
    EXPECT_EQ(le32(h + 24), 32028U);
    EXPECT_EQ(le32(h + 28), 32028U * 4U);
    EXPECT_EQ(le32(h + 32), 0x00100004U);
    EXPECT_EQ(memcmp(h + 36, "data", 4), 0);
    EXPECT_EQ(le32(h + 40), 400U);
}

TEST(file_round_trip) {
    const char *path = "audio_dump_test.wav";
    EXPECT_TRUE(audio_dump_open(path, 32028, 1024));
    const s16 a[4] = {1, -2, 0x1234, -0x8000};
    audio_dump_write(a, 2);
    audio_dump_write(a, 1);
    EXPECT_EQ(audio_dump_frames(), 3U);
    audio_dump_close();
    FILE *f = fopen(path, "rb");
    EXPECT_NOT_NULL(f);
    u8 buf[64] = {0};
    const size_t n = f != NULL ? fread(buf, 1, sizeof(buf), f) : 0U;
    if (f != NULL) {
        (void)fclose(f);
    }
    (void)remove(path);
    EXPECT_EQ(n, 44U + 12U);
    EXPECT_EQ(le32(buf + 4), 36U + 12U);
    EXPECT_EQ(le32(buf + 24), 32028U);
    EXPECT_EQ(le32(buf + 40), 12U);
    const u8 expected[12] = {0x01, 0x00, 0xFE, 0xFF, 0x34, 0x12, 0x00, 0x80, 0x01, 0x00, 0xFE, 0xFF};
    EXPECT_EQ(memcmp(buf + 44, expected, sizeof(expected)), 0);
}

int main(void) {
    RUN_TEST(header_fields);
    RUN_TEST(file_round_trip);
    return am_test_finish();
}
