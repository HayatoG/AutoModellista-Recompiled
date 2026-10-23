/* SPDX-License-Identifier: GPL-3.0-or-later */
/* DOL sintético montado aqui (nenhum byte do jogo). */
#include <string.h>

#include "am/dol.h"
#include "am/mem.h"

#include "am_test.h"

#define FILE_SIZE 0x400U

static u8 s_file[FILE_SIZE];

static void put32(u32 off, u32 v) {
    s_file[off] = (u8)(v >> 24);
    s_file[off + 1U] = (u8)(v >> 16);
    s_file[off + 2U] = (u8)(v >> 8);
    s_file[off + 3U] = (u8)v;
}

/* texto 0 em 0x80003100 (0x100 bytes, arquivo 0x100), dados 0 em 0x80010000 (0x40 bytes, arquivo 0x200),
 * .bss 0x8000F000..0x80011000 cobrindo a seção de dados (layout do CodeWarrior), entry 0x80003104. */
static void build(void) {
    memset(s_file, 0, sizeof(s_file));
    put32(0x00, 0x100);
    put32(0x48, 0x80003100U);
    put32(0x90, 0x100);
    put32(0x1C, 0x200);
    put32(0x48 + 0x1C, 0x80010000U);
    put32(0x90 + 0x1C, 0x40);
    put32(0xD8, 0x8000F000U);
    put32(0xDC, 0x2000);
    put32(0xE0, 0x80003104U);
    for (u32 i = 0; i < 0x100U; i++) {
        s_file[0x100 + i] = (u8)i;
    }
    for (u32 i = 0; i < 0x40U; i++) {
        s_file[0x200 + i] = (u8)(0xA0U + i);
    }
}

TEST(parse_reads_header) {
    build();
    DolImage img;
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_OK);
    EXPECT_EQ(img.sections[0].addr, 0x80003100U);
    EXPECT_EQ(img.sections[7].offset, 0x200U);
    EXPECT_EQ(img.bss_size, 0x2000U);
    EXPECT_EQ(img.entry, 0x80003104U);
    EXPECT_EQ(dol_text_section_of(&img, 0x800031FCU), 0);
    EXPECT_TRUE(dol_text_section_of(&img, 0x80003200U) == -1);
}

TEST(parse_rejects_bad_files) {
    DolImage img;
    build();
    EXPECT_EQ(dol_parse(s_file, 0x80, &img), DOL_ERR_TRUNCATED);
    put32(0x90, 0x400);
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_ERR_SECTION_FILE);
    build();
    put32(0x48, 0x81800000U);
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_ERR_SECTION_MEM1);
    build();
    put32(0x48 + 0x1C, 0x80003180U);
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_ERR_SECTION_OVERLAP);
    build();
    put32(0x90, 0x102);
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_ERR_SECTION_ALIGN);
    build();
    put32(0xE0, 0x80010000U);
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_ERR_ENTRY);
    EXPECT_STREQ(dol_result_name(DOL_ERR_ENTRY), "entry point fora das seções de texto");
}

TEST(load_zeroes_bss_but_keeps_sections_inside_it) {
    EXPECT_TRUE(mem_init());
    build();
    mem_write32(NULL, 0x8000F000U, 0xDEADBEEFU);
    mem_write32(NULL, 0x80010FFCU, 0xDEADBEEFU);
    DolImage img;
    EXPECT_EQ(dol_parse(s_file, FILE_SIZE, &img), DOL_OK);
    EXPECT_EQ(dol_load(&img, s_file, FILE_SIZE), DOL_OK);
    EXPECT_EQ(mem_read32(NULL, 0x80003100U), 0x00010203U);
    EXPECT_EQ(mem_read32(NULL, 0x8001003CU), 0xDCDDDEDFU);
    EXPECT_EQ(mem_read32(NULL, 0x8000F000U), 0U);
    EXPECT_EQ(mem_read32(NULL, 0x80010FFCU), 0U);
}

int main(void) {
    RUN_TEST(parse_reads_header);
    RUN_TEST(parse_rejects_bad_files);
    RUN_TEST(load_zeroes_bss_but_keeps_sections_inside_it);
    mem_shutdown();
    return am_test_finish();
}
