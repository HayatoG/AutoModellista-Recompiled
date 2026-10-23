/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Imagem de disco a partir de uma árvore extraída sintética (sys/ + files/ com FST escrito aqui). */
#include <stdio.h>
#include <string.h>

#include "am/disc.h"
#include "am/platform.h"

#include "am_test.h"

#define ROOT "disc_test_tree"

static void put32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static bool write_file(const char *rel, const u8 *data, size_t size) {
    char path[256];
    (void)snprintf(path, sizeof(path), "%s/%s", ROOT, rel);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    const bool ok = fwrite(data, 1, size, f) == size;
    (void)fclose(f);
    return ok;
}

static bool fill_file(const char *rel, size_t size, u8 seed) {
    static u8 s_buf[0x2000];
    for (size_t i = 0; i < size; i++) {
        s_buf[i] = (u8)(seed + i);
    }
    return write_file(rel, s_buf, size);
}

/* FST: raiz (4 entradas), a.bin em 0x10000 (0x30 bytes), dir/, dir/b.bin em 0x10040 (0x10 bytes). */
static size_t build_fst(u8 *fst) {
    memset(fst, 0, 0x80);
    static const char s_names[] = "a.bin\0dir\0b.bin";
    fst[0] = 1;
    put32(&fst[8], 4);
    put32(&fst[12], 0);
    put32(&fst[16], 0x10000U);
    put32(&fst[20], 0x30U);
    put32(&fst[24], 0x01000006U);
    put32(&fst[32], 4);
    put32(&fst[36], 10);
    put32(&fst[40], 0x10040U);
    put32(&fst[44], 0x10U);
    memcpy(&fst[48], s_names, sizeof(s_names));
    return 48U + sizeof(s_names);
}

static bool build_tree(void) {
    u8 boot[0x440] = {0};
    u8 fst[0x80];
    const size_t fst_size = build_fst(fst);
    put32(&boot[0x420], 0x8000U); /* main.dol */
    put32(&boot[0x424], 0x9000U); /* fst.bin */
    put32(&boot[0x428], (u32)fst_size);
    memcpy(boot, "GTST01", 6);
    return platform_make_dir(ROOT "/sys") && platform_make_dir(ROOT "/files/dir") &&
           write_file("sys/boot.bin", boot, sizeof(boot)) && fill_file("sys/bi2.bin", 0x2000, 1) &&
           fill_file("sys/apploader.img", 0x100, 2) && fill_file("sys/main.dol", 0x200, 3) &&
           write_file("sys/fst.bin", fst, fst_size) && fill_file("files/a.bin", 0x30, 0x40) &&
           fill_file("files/dir/b.bin", 0x10, 0x80);
}

TEST(reads_files_padding_and_system_area) {
    EXPECT_TRUE(build_tree());
    EXPECT_EQ(disc_open(ROOT), DISC_OK);
    EXPECT_EQ(disc_stats()->regions, 7U);
    u8 buf[0x60];
    EXPECT_TRUE(disc_read(0, buf, 6));
    EXPECT_TRUE(memcmp(buf, "GTST01", 6) == 0);
    EXPECT_TRUE(disc_read(0x2440U, buf, 2)); /* apploader */
    EXPECT_EQ(buf[0], 2U);
    EXPECT_EQ(buf[1], 3U);
    EXPECT_TRUE(disc_read(0x10020U, buf, 0x30)); /* fim de a.bin, enchimento e começo de b.bin */
    EXPECT_EQ(buf[0], (u8)(0x40U + 0x20U));
    EXPECT_EQ(buf[0x0F], (u8)(0x40U + 0x2FU));
    EXPECT_EQ(buf[0x10], 0U);
    EXPECT_EQ(buf[0x1F], 0U);
    EXPECT_EQ(buf[0x20], 0x80U);
    EXPECT_EQ(disc_stats()->padding_bytes, 0x10U);
    u64 in_file = 0;
    EXPECT_STREQ(disc_region_at(0x10045U, &in_file), "files/dir/b.bin");
    EXPECT_EQ(in_file, 5U);
    EXPECT_NULL(disc_region_at(0x10035U, &in_file));
    EXPECT_STREQ(disc_region_at(0x9000U, &in_file), "sys/fst.bin");
    disc_close();
}

TEST(rejects_size_mismatch) {
    EXPECT_TRUE(build_tree());
    EXPECT_TRUE(fill_file("files/a.bin", 0x31, 0));
    EXPECT_EQ(disc_open(ROOT), DISC_ERR_FILE);
    EXPECT_TRUE(!disc_is_open());
    EXPECT_EQ(disc_open("disc_test_nao_existe"), DISC_ERR_SYS);
}

int main(void) {
    RUN_TEST(reads_files_padding_and_system_area);
    RUN_TEST(rejects_size_mismatch);
    return am_test_finish();
}
