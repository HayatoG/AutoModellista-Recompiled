/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>

#include "am/profile.h"

#include "am_test.h"

/* Fatias montadas à mão: dois trechos em duas threads; a saída sai ordenada por ciclos. */
TEST(profile_sums_and_sorts) {
    profile_add(0x80001000U, 0x80200000U, 50U); /* fora de begin/end: ignorado */
    profile_begin();
    EXPECT_TRUE(profile_active());
    profile_add(0x80001000U, 0x80200000U, 30U);
    profile_add(0x80002000U, 0x80200000U, 100U);
    profile_add(0x80001000U, 0x00000000U, 30U);
    FILE *f = tmpfile();
    EXPECT_TRUE(f != NULL);
    profile_end(f, 10U);
    EXPECT_TRUE(!profile_active());
    if (f == NULL) {
        return;
    }
    char buf[512] = {0};
    size_t n = 0;
    if (fseek(f, 0, SEEK_SET) == 0) {
        n = fread(buf, 1, sizeof(buf) - 1U, f);
    }
    (void)fclose(f);
    EXPECT_TRUE(n > 0U);
    EXPECT_TRUE(strstr(buf, "# total 160 ciclos") != NULL);
    EXPECT_TRUE(strstr(buf, "80200000 130 2 81.25") != NULL);
    const char *a = strstr(buf, "80002000 100 1");
    const char *b = strstr(buf, "80001000 60 2");
    EXPECT_TRUE(a != NULL && b != NULL && a < b);
}

int main(void) {
    RUN_TEST(profile_sums_and_sorts);
    return am_test_finish();
}
