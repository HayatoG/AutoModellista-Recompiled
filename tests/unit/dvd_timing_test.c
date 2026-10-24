/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Valores esperados calculados à parte com as fórmulas de DVDMath.cpp / ScheduleReads do Dolphin 2609a. */
#include <math.h>

#include "am/dvd_timing.h"

#include "am_test.h"

#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

TEST(dvd_math) {
    EXPECT_TRUE(NEAR(dvd_timing_seek_seconds(0, 0), 0.035));                         /* busca curta, distância 0 */
    EXPECT_TRUE(NEAR(dvd_timing_seek_seconds(0, 0x40000000U), 0.12372261918128828)); /* busca longa */
    EXPECT_TRUE(NEAR(dvd_timing_raw_read_seconds(0x10000000U, 0x100000U), 0.42139291497225995));
    const double r = dvd_timing_rotational_latency(0x10000000U, 0.0);
    EXPECT_TRUE(r >= 0.0 && r < 1.0 / 28.5);
}

TEST(seek_then_buffered_read) {
    dvd_timing_reset();
    /* Sem buffer e cabeça em 0: 600 µs + busca + rotação (tempo em segundos inteiros) + 1 bloco sem buffer. */
    const u64 first = dvd_timing_read(1000, 0x10000000U, 0x10000U);
    EXPECT_EQ(first, 52823843U);
    /* Depois de o buffer encher, o bloco seguinte vem a 32 MiB/s: 291600 + 0x8000 * 486e6 / 32 MiB. */
    const u64 later = 1000U + first + DVD_TIMING_TICKS_PER_SECOND;
    EXPECT_EQ(dvd_timing_read(later, 0x10010000U, 0x8000U), 291600U + 474609U);
    /* Ler antes do buffer destrói o buffer e exige busca: bem mais lento. */
    EXPECT_TRUE(dvd_timing_read(later + 1000000U, 0x01000000U, 0x8000U) > 291600U + 17010000U); /* 600 µs + 35 ms */
}

int main(void) {
    RUN_TEST(dvd_math);
    RUN_TEST(seek_then_buffered_read);
    return am_test_finish();
}
