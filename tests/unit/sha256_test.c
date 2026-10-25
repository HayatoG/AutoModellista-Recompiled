/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SHA-256 contra os vetores do FIPS 180-4 (exemplos do NIST) e a entrada em pedaços. */
#include <string.h>

#include "am/sha256.h"

#include "am_test.h"

static void expect_hex(const char *text, const char *hex) {
    char out[SHA256_HEX_SIZE];
    sha256_hex((const u8 *)text, strlen(text), out);
    EXPECT_TRUE(strcmp(out, hex) == 0);
}

TEST(nist_vectors) {
    expect_hex("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_hex("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(million_a_in_pieces) {
    static u8 chunk[1000];
    memset(chunk, 'a', sizeof(chunk));
    Sha256 s;
    sha256_init(&s);
    for (u32 i = 0; i < 1000U; i++) {
        sha256_update(&s, chunk, (i % 2U) == 0U ? 1000U : 0U);
        sha256_update(&s, chunk, (i % 2U) == 0U ? 0U : 1000U);
    }
    u8 d[SHA256_DIGEST_SIZE];
    sha256_final(&s, d);
    static const u8 want[SHA256_DIGEST_SIZE] = {0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7,
                                                0xe2, 0x84, 0xd7, 0x3e, 0x67, 0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97,
                                                0x20, 0x0e, 0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0};
    EXPECT_TRUE(memcmp(d, want, sizeof(want)) == 0);
}

int main(void) {
    RUN_TEST(nist_vectors);
    RUN_TEST(million_a_in_pieces);
    return am_test_finish();
}
