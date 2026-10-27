/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Extração de imagem (core/disc_extract.c, disc_image.c, disc_rvz.c) com um mini-disco GameCube sintético gerado aqui:
 * o mesmo disco como ISO cru e como RVZ (grupos sem compressão, com quadros zstd de bloco "raw" e "RLE" feitos à mão,
 * grupos empacotados com segmentos de dados e de lixo, grupo de zeros). Nada do jogo.
 *
 * Mapa do disco (blocos de 0x8000, tamanho total 0x39000):
 *   g0 0x00000 sistema: cabeçalho, bi2, apploader, DOL (0x3000), FST (0x4000)   cru
 *   g1 0x08000 a.bin (0x4800), d.bin (0x800 em 0xC800), lixo A no resto          empacotado, sem compressão
 *   g2 0x10000 lixo B                                                            empacotado, sem compressão
 *   g3 0x18000 b.bin (0x8000)                                                    zstd (bloco raw)
 *   g4 0x20000 zeros                                                             zstd (bloco RLE)
 *   g5 0x28000 c.bin (0x3000), lixo C no resto                                   empacotado + zstd
 *   g6 0x30000 lixo D                                                            empacotado + zstd
 *   g7 0x38000 zeros (grupo curto, 0x1000)                                       tamanho 0 (zeros) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/disc_extract.h"
#include "am/disc_image.h"
#include "am/platform_fs.h"

#include "am_test.h"

#define IMG_SIZE   0x39000U
#define CHUNK      0x8000U
#define GROUPS     8U
#define FST_OFFSET 0x4000U
#define DOL_OFFSET 0x3000U
#define DOL_SIZE   0x300U
#define APP_SIZE   0x160U

static u8 s_iso[IMG_SIZE];
static u8 s_rvz[0x80000];
static size_t s_rvz_size;

static void put32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void put64(u8 *p, u64 v) {
    put32(p, (u32)(v >> 32));
    put32(p + 4, (u32)v);
}

static u8 pattern(u8 seed, size_t i) {
    return (u8)(seed + i * 7U);
}

static void fill_pattern(u8 *dst, size_t size, u8 seed) {
    for (size_t i = 0; i < size; i++) {
        dst[i] = pattern(seed, i);
    }
}

/* Sementes de lixo (17 palavras); a de zeros não é usada de propósito: a semente real do Dolphin é qualquer uma. */
static void make_seed(u8 seed[68], u8 tag) {
    for (u32 i = 0; i < 68U; i++) {
        seed[i] = (u8)(tag * 31U + i * 13U + (i >> 2));
    }
}

static void put_junk(u8 *iso, u32 offset, u32 size, u8 tag) {
    u8 seed[68];
    make_seed(seed, tag);
    disc_rvz_junk(seed, offset % CHUNK, iso + offset, size);
}

static size_t build_fst(u8 *fst) {
    static const char names[] = "a.bin\0sub\0b.bin\0c.bin\0d.bin\0e.bin";
    memset(fst, 0, 0x100);
    put32(fst, 0x01000000U);
    put32(fst + 8, 7);
    /* 1 a.bin */ put32(fst + 12, 0U);
    put32(fst + 16, 0x8000U);
    put32(fst + 20, 0x4800U);
    /* 2 sub (pai 0, próximo 6) */ put32(fst + 24, 0x01000006U);
    put32(fst + 28, 0);
    put32(fst + 32, 6);
    /* 3 b.bin */ put32(fst + 36, 10U);
    put32(fst + 40, 0x18000U);
    put32(fst + 44, 0x8000U);
    /* 4 c.bin */ put32(fst + 48, 16U);
    put32(fst + 52, 0x28000U);
    put32(fst + 56, 0x3000U);
    /* 5 d.bin */ put32(fst + 60, 22U);
    put32(fst + 64, 0xC800U);
    put32(fst + 68, 0x800U);
    /* 6 e.bin (vazio) */ put32(fst + 72, 28U);
    put32(fst + 76, 0x9000U);
    put32(fst + 80, 0);
    memcpy(fst + 84, names, sizeof(names));
    return 84U + sizeof(names);
}

static void build_iso(u8 *iso) {
    memset(iso, 0, IMG_SIZE);
    memcpy(iso, "GTST01", 6);
    put32(iso + 0x1C, 0xC2339F3DU);
    memcpy(iso + 0x20, "mini disco de teste", 20);
    put32(iso + 0x420, DOL_OFFSET);
    put32(iso + 0x424, FST_OFFSET);
    fill_pattern(iso + 0x440, 0x2000, 0x5A); /* bi2 */
    put32(iso + 0x2440 + 0x14, 0x100);       /* apploader: código */
    put32(iso + 0x2440 + 0x18, 0x40);        /* trailer */
    fill_pattern(iso + 0x2440 + 0x20, APP_SIZE - 0x20U, 0x6B);
    put32(iso + DOL_OFFSET, 0x100);        /* seção de texto 0: deslocamento */
    put32(iso + DOL_OFFSET + 0x90, 0x200); /* e tamanho */
    fill_pattern(iso + DOL_OFFSET + 0x100, 0x200, 0x7C);
    const size_t fst_size = build_fst(iso + FST_OFFSET);
    put32(iso + 0x428, (u32)fst_size);
    fill_pattern(iso + 0x8000, 0x4800, 0x11); /* a.bin */
    fill_pattern(iso + 0xC800, 0x800, 0x44);  /* d.bin */
    put_junk(iso, 0xD000, 0x3000, 0xA);
    put_junk(iso, 0x10000, 0x8000, 0xB);
    fill_pattern(iso + 0x18000, 0x8000, 0x22); /* b.bin */
    fill_pattern(iso + 0x28000, 0x3000, 0x33); /* c.bin */
    put_junk(iso, 0x2B000, 0x5000, 0xC);
    put_junk(iso, 0x30000, 0x8000, 0xD);
}

/* --- Codificador mínimo de RVZ ----------------------------------------------------------------------------- */

/* Quadro zstd com um único bloco: raw (tipo 0) ou RLE (tipo 1, repete `data[0]` `size` vezes). Sem soma de controle. */
static size_t zstd_frame(u8 *out, const u8 *data, size_t size, bool rle) {
    static const u8 magic[4] = {0x28, 0xB5, 0x2F, 0xFD};
    memcpy(out, magic, 4);
    out[4] = 0xA0; /* conteúdo de 4 bytes, segmento único */
    out[5] = (u8)size;
    out[6] = (u8)(size >> 8);
    out[7] = (u8)(size >> 16);
    out[8] = (u8)(size >> 24);
    const u32 block = ((u32)size << 3) | (rle ? 2U : 0U) | 1U;
    out[9] = (u8)block;
    out[10] = (u8)(block >> 8);
    out[11] = (u8)(block >> 16);
    if (rle) {
        out[12] = data[0];
        return 13;
    }
    memcpy(out + 12, data, size);
    return 12U + size;
}

typedef struct Packer {
    u8 buf[0x9000];
    size_t len;
} Packer;

static void pack_data(Packer *p, const u8 *data, u32 size) {
    put32(p->buf + p->len, size);
    memcpy(p->buf + p->len + 4, data, size);
    p->len += 4U + size;
}

static void pack_junk(Packer *p, u32 size, u8 tag) {
    put32(p->buf + p->len, size | 0x80000000U);
    make_seed(p->buf + p->len + 4, tag);
    p->len += 4U + 68U;
}

typedef struct Group {
    u8 data[0x9000];
    u32 size;
    u32 packed;
    bool compressed;
} Group;

/* Descreve os 8 grupos (veja o mapa no topo). `zstd` falso: tudo sem compressão. */
static void make_groups(const u8 *iso, bool zstd, Group *g) {
    Packer *p = (Packer *)calloc(1, sizeof(*p));
    memset(g, 0, sizeof(Group) * GROUPS);
    /* g0: cru */
    memcpy(g[0].data, iso, CHUNK);
    g[0].size = CHUNK;
    /* g1: dados 0x5000 + lixo A 0x3000, empacotado, sem compressão */
    pack_data(p, iso + 0x8000, 0x5000);
    pack_junk(p, 0x3000, 0xA);
    memcpy(g[1].data, p->buf, p->len);
    g[1].size = g[1].packed = (u32)p->len;
    /* g2: lixo B */
    p->len = 0;
    pack_junk(p, 0x8000, 0xB);
    memcpy(g[2].data, p->buf, p->len);
    g[2].size = g[2].packed = (u32)p->len;
    /* g3: b.bin */
    if (zstd) {
        g[3].size = (u32)zstd_frame(g[3].data, iso + 0x18000, CHUNK, false);
        g[3].compressed = true;
    } else {
        memcpy(g[3].data, iso + 0x18000, CHUNK);
        g[3].size = CHUNK;
    }
    /* g4: zeros */
    if (zstd) {
        g[4].size = (u32)zstd_frame(g[4].data, iso + 0x20000, CHUNK, true);
        g[4].compressed = true;
    } else {
        g[4].size = CHUNK;
    }
    /* g5: c.bin + lixo C, empacotado e (com zstd) comprimido */
    p->len = 0;
    pack_data(p, iso + 0x28000, 0x3000);
    pack_junk(p, 0x5000, 0xC);
    g[5].packed = (u32)p->len;
    g[5].compressed = zstd;
    g[5].size = zstd ? (u32)zstd_frame(g[5].data, p->buf, p->len, false) : (u32)p->len;
    if (!zstd) {
        memcpy(g[5].data, p->buf, p->len);
    }
    /* g6: lixo D */
    p->len = 0;
    pack_junk(p, 0x8000, 0xD);
    g[6].packed = (u32)p->len;
    g[6].compressed = zstd;
    g[6].size = zstd ? (u32)zstd_frame(g[6].data, p->buf, p->len, false) : (u32)p->len;
    if (!zstd) {
        memcpy(g[6].data, p->buf, p->len);
    }
    /* g7: tamanho 0 (zeros) */
    free(p);
}

/* compression: 0 = nenhuma, 5 = zstd (valor do campo do cabeçalho 2). */
static void build_rvz(const u8 *iso, u32 compression) {
    Group *g = (Group *)calloc(GROUPS, sizeof(Group));
    make_groups(iso, compression == 5U, g);
    u8 raw_entry[0x18];
    put64(raw_entry, 0x80);
    put64(raw_entry + 8, IMG_SIZE - 0x80U);
    put32(raw_entry + 16, 0);
    put32(raw_entry + 20, GROUPS);
    size_t pos = 0x48U + 0xDCU;
    const size_t raw_offset = pos;
    size_t raw_len = sizeof(raw_entry);
    memcpy(s_rvz + pos, raw_entry, raw_len);
    if (compression == 5U) {
        raw_len = zstd_frame(s_rvz + pos, raw_entry, sizeof(raw_entry), false);
    }
    pos = (pos + raw_len + 3U) & ~(size_t)3U;
    const size_t group_table = pos;
    pos += GROUPS * 12U; /* tabela de grupos: preenchida depois */
    u8 table[GROUPS * 12U];
    for (u32 i = 0; i < GROUPS; i++) {
        pos = (pos + 3U) & ~(size_t)3U;
        memcpy(s_rvz + pos, g[i].data, g[i].size);
        put32(table + i * 12U, (u32)(pos >> 2));
        put32(table + i * 12U + 4, i == 7U ? 0U : (g[i].size | (g[i].compressed ? 0x80000000U : 0U)));
        put32(table + i * 12U + 8, g[i].packed);
        pos += g[i].size;
    }
    /* A tabela cabe no espaço reservado se estiver sem compressão; com zstd ela vai no fim do arquivo. */
    size_t group_stored = sizeof(table);
    size_t group_offset = group_table;
    if (compression == 5U) {
        group_offset = (pos + 3U) & ~(size_t)3U;
        group_stored = zstd_frame(s_rvz + group_offset, table, sizeof(table), false);
        pos = group_offset + group_stored;
    } else {
        memcpy(s_rvz + group_table, table, sizeof(table));
    }
    s_rvz_size = pos;
    /* Cabeçalhos */
    memset(s_rvz, 0, 0x124);
    memcpy(s_rvz, "RVZ\x01", 4);
    put32(s_rvz + 4, 0x01000000U);
    put32(s_rvz + 8, 0x00030000U);
    put32(s_rvz + 12, 0xDC);
    put64(s_rvz + 0x24, IMG_SIZE);
    put64(s_rvz + 0x2C, s_rvz_size);
    u8 *h2 = s_rvz + 0x48;
    put32(h2, 1);
    put32(h2 + 4, compression);
    put32(h2 + 12, CHUNK);
    memcpy(h2 + 0x10, iso, 0x80);
    put32(h2 + 0x94, 0x30);
    put32(h2 + 0xB4, 1);
    put64(h2 + 0xB8, raw_offset);
    put32(h2 + 0xC0, (u32)raw_len);
    put32(h2 + 0xC4, GROUPS);
    put64(h2 + 0xC8, group_offset);
    put32(h2 + 0xD0, (u32)group_stored);
    free(g);
}

/* --- Arquivos e árvores do teste ---------------------------------------------------------------------------- */

static bool write_file(const char *path, const u8 *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    const bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static u8 *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    u8 *buf = (u8 *)malloc(0x10000);
    *size = buf != NULL ? fread(buf, 1, 0x10000, f) : 0;
    (void)fclose(f);
    return buf;
}

static const char *const s_tree_files[] = {"sys/boot.bin",    "sys/bi2.bin",    "sys/apploader.img", "sys/fst.bin",
                                           "sys/main.dol",    "files/a.bin",    "files/e.bin",       "files/sub/b.bin",
                                           "files/sub/c.bin", "files/sub/d.bin"};
static const char *const s_tree_dirs[] = {"files/sub", "files", "sys", ""};

static void remove_tree(const char *root) {
    char path[256];
    for (size_t pass = 0; pass < 2; pass++) {
        (void)snprintf(path, sizeof(path), pass == 0 ? "%s" : "%s.parte", root);
        char base[256];
        (void)snprintf(base, sizeof(base), "%s", path);
        for (size_t i = 0; i < sizeof(s_tree_files) / sizeof(s_tree_files[0]); i++) {
            (void)snprintf(path, sizeof(path), "%s/%s", base, s_tree_files[i]);
            (void)platform_fs_remove(path);
        }
        for (size_t i = 0; i < sizeof(s_tree_dirs) / sizeof(s_tree_dirs[0]); i++) {
            (void)snprintf(path, sizeof(path), s_tree_dirs[i][0] != 0 ? "%s/%s" : "%s%s", base, s_tree_dirs[i]);
            (void)platform_fs_remove(path);
        }
    }
}

static bool file_equals(const char *root, const char *rel, const u8 *expect, size_t size) {
    char path[256];
    (void)snprintf(path, sizeof(path), "%s/%s", root, rel);
    size_t got = 0;
    u8 *data = read_file(path, &got);
    const bool ok = data != NULL && got == size && (size == 0 || memcmp(data, expect, size) == 0);
    free(data);
    return ok;
}

/* Confere a árvore extraída do mini-disco byte a byte contra o ISO de referência. */
static void expect_tree(const char *root, const u8 *iso) {
    u8 pat[0x8000];
    const size_t fst_size = disc_be32(iso + 0x428);
    EXPECT_TRUE(file_equals(root, "sys/boot.bin", iso, 0x440));
    EXPECT_TRUE(file_equals(root, "sys/bi2.bin", iso + 0x440, 0x2000));
    EXPECT_TRUE(file_equals(root, "sys/apploader.img", iso + 0x2440, APP_SIZE));
    EXPECT_TRUE(file_equals(root, "sys/fst.bin", iso + FST_OFFSET, fst_size));
    EXPECT_TRUE(file_equals(root, "sys/main.dol", iso + DOL_OFFSET, DOL_SIZE));
    fill_pattern(pat, 0x4800, 0x11);
    EXPECT_TRUE(file_equals(root, "files/a.bin", pat, 0x4800));
    fill_pattern(pat, 0x8000, 0x22);
    EXPECT_TRUE(file_equals(root, "files/sub/b.bin", pat, 0x8000));
    fill_pattern(pat, 0x3000, 0x33);
    EXPECT_TRUE(file_equals(root, "files/sub/c.bin", pat, 0x3000));
    fill_pattern(pat, 0x800, 0x44);
    EXPECT_TRUE(file_equals(root, "files/sub/d.bin", pat, 0x800));
    EXPECT_TRUE(file_equals(root, "files/e.bin", pat, 0));
}

static DiscExtractResult extract_keep(const char *image, const char *out, u64 write_limit, DiscExtractProgressFn fn,
                                      void *user) {
    DiscExtractOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.keep_other_game = true; /* o DOL sintético não tem o SHA-256 do jogo */
    opt.progress = fn;
    opt.user = user;
    opt.test_write_limit = write_limit;
    return disc_extract_ex(image, out, &opt);
}

static bool ensure_dir(const char *dir) {
    return platform_fs_exists(dir) || platform_fs_mkdir(dir) >= 0;
}

/* --- Testes ------------------------------------------------------------------------------------------------ */

TEST(junk_generator_is_seekable_and_zero_seed_is_zero) {
    u8 seed[68];
    make_seed(seed, 7);
    u8 all[300];
    u8 part[100];
    disc_rvz_junk(seed, 0, all, sizeof(all));
    disc_rvz_junk(seed, 123, part, sizeof(part));
    EXPECT_TRUE(memcmp(all + 123, part, sizeof(part)) == 0);
    disc_rvz_junk(seed, 0x8000U + 5U, part, 10); /* o deslocamento é tomado módulo o setor */
    EXPECT_TRUE(memcmp(all + 5, part, 10) == 0);
    u8 zero_seed[68] = {0};
    disc_rvz_junk(zero_seed, 0, all, sizeof(all));
    u8 zeros[300] = {0};
    EXPECT_TRUE(memcmp(all, zeros, sizeof(all)) == 0);
    /* Todo par de palavras de saída respeita (x & 0xC00000) == ((x >> 2) & 0xC00000) em big-endian (o gerador
     * "shift por 18"): propriedade que o Dolphin usa para reconhecer lixo. */
    u8 out[2084];
    disc_rvz_junk(seed, 0, out, sizeof(out));
    for (size_t i = 0; i + 4 <= sizeof(out); i += 4) {
        const u32 x = disc_be32(out + i);
        EXPECT_EQ(x & 0x00C00000U, (x >> 2) & 0x00C00000U);
    }
}

TEST(iso_and_rvz_read_the_same_image) {
    build_iso(s_iso);
    EXPECT_TRUE(write_file("dxt_mini.iso", s_iso, IMG_SIZE));
    const u32 modes[2] = {0, 5};
    for (u32 m = 0; m < 2; m++) {
        build_rvz(s_iso, modes[m]);
        EXPECT_TRUE(write_file("dxt_mini.rvz", s_rvz, s_rvz_size));
        DiscSource *src = NULL;
        EXPECT_EQ(disc_image_open("dxt_mini.rvz", &src), DISC_IMAGE_OK);
        if (src == NULL) {
            continue;
        }
        EXPECT_EQ(src->size, IMG_SIZE);
        u8 *back = (u8 *)malloc(IMG_SIZE);
        EXPECT_EQ(disc_image_read(src, 0, back, IMG_SIZE), DISC_IMAGE_OK);
        EXPECT_TRUE(memcmp(back, s_iso, IMG_SIZE) == 0); /* inclusive o lixo e o grupo de zeros */
        /* leituras pequenas cruzando grupos, de trás para frente */
        for (u32 off = IMG_SIZE - 0x123U; off > 0x2000U; off -= 0x1F00U) {
            u8 chunk[0x123];
            EXPECT_EQ(disc_image_read(src, off, chunk, sizeof(chunk)), DISC_IMAGE_OK);
            EXPECT_TRUE(memcmp(chunk, s_iso + off, sizeof(chunk)) == 0);
        }
        EXPECT_EQ(disc_image_read(src, IMG_SIZE - 4U, back, 8), DISC_IMAGE_ERR_RANGE);
        free(back);
        disc_image_close(src);
    }
}

typedef struct Count {
    u32 calls;
    u64 last;
    bool monotonic;
    u64 done;
    u64 total;
} Count;

static bool count_progress(const DiscExtractProgress *p, void *user) {
    Count *c = (Count *)user;
    c->calls++;
    c->monotonic = c->monotonic && p->bytes_done >= c->last && p->current != NULL;
    c->last = p->bytes_done;
    c->done = p->bytes_done;
    c->total = p->bytes_total;
    return true;
}

TEST(extracts_iso_and_rvz_to_the_same_tree) {
    build_iso(s_iso);
    EXPECT_TRUE(write_file("dxt_mini.iso", s_iso, IMG_SIZE));
    const char *const outs[3] = {"dxt_out_iso", "dxt_out_rvz0", "dxt_out_rvz5"};
    for (u32 i = 0; i < 3; i++) {
        remove_tree(outs[i]);
        if (i > 0) {
            build_rvz(s_iso, i == 1 ? 0U : 5U);
            EXPECT_TRUE(write_file("dxt_mini.rvz", s_rvz, s_rvz_size));
        }
        Count c = {0, 0, true, 0, 0};
        const DiscExtractResult res =
            extract_keep(i == 0 ? "dxt_mini.iso" : "dxt_mini.rvz", outs[i], 0, count_progress, &c);
        EXPECT_EQ(res, DISC_EXTRACT_ERR_OTHER_GAME); /* árvore completa, mas o DOL não é o do jogo */
        expect_tree(outs[i], s_iso);
        EXPECT_TRUE(!platform_fs_exists("dxt_out_iso.parte"));
        EXPECT_TRUE(c.monotonic);
        EXPECT_TRUE(c.calls >= 15U);
        EXPECT_EQ(c.done, c.total);
        EXPECT_EQ(c.total, 0x440U + 0x2000U + APP_SIZE + disc_be32(s_iso + 0x428) + DOL_SIZE + 0x4800U + 0x8000U +
                               0x3000U + 0x800U);
        remove_tree(outs[i]);
    }
}

TEST(other_game_without_keep_writes_nothing) {
    build_iso(s_iso);
    EXPECT_TRUE(write_file("dxt_mini.iso", s_iso, IMG_SIZE));
    remove_tree("dxt_out_other");
    EXPECT_EQ(disc_extract("dxt_mini.iso", "dxt_out_other", NULL, NULL), DISC_EXTRACT_ERR_OTHER_GAME);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_other"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_other.parte"));
}

TEST(existing_target_is_refused_and_empty_target_is_filled) {
    build_iso(s_iso);
    EXPECT_TRUE(write_file("dxt_mini.iso", s_iso, IMG_SIZE));
    remove_tree("dxt_out_exists");
    EXPECT_TRUE(ensure_dir("dxt_out_exists")); /* pasta vazia já existente: o rename direto falha no Windows */
    EXPECT_EQ(extract_keep("dxt_mini.iso", "dxt_out_exists", 0, NULL, NULL), DISC_EXTRACT_ERR_OTHER_GAME);
    expect_tree("dxt_out_exists", s_iso);
    EXPECT_EQ(extract_keep("dxt_mini.iso", "dxt_out_exists", 0, NULL, NULL), DISC_EXTRACT_ERR_EXISTS);
    expect_tree("dxt_out_exists", s_iso); /* a recusa não mexeu em nada */
    remove_tree("dxt_out_exists");
}

TEST(rejects_bad_images_with_distinct_errors) {
    u8 junk[0x600];
    memset(junk, 0x41, sizeof(junk));
    EXPECT_TRUE(write_file("dxt_bad.img", junk, sizeof(junk)));
    EXPECT_EQ(disc_extract("dxt_nao_existe.iso", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_OPEN);
    EXPECT_EQ(disc_extract("dxt_bad.img", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_FORMAT);
    memcpy(junk, "WIA\x01", 4);
    EXPECT_TRUE(write_file("dxt_bad.img", junk, sizeof(junk)));
    EXPECT_EQ(disc_extract("dxt_bad.img", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_WIA);
    memset(junk, 0, sizeof(junk));
    put32(junk + 0x18, 0x5D1C9EA3U); /* disco do Wii */
    EXPECT_TRUE(write_file("dxt_bad.img", junk, sizeof(junk)));
    EXPECT_EQ(disc_extract("dxt_bad.img", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_NOT_GAMECUBE);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_bad"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_bad.parte"));
    EXPECT_TRUE(strlen(disc_extract_result_text(DISC_EXTRACT_ERR_COMPRESSION)) > 20U);
}

TEST(rejects_damaged_rvz) {
    build_iso(s_iso);
    build_rvz(s_iso, 5);
    const size_t good = s_rvz_size;
    /* truncado */
    EXPECT_TRUE(write_file("dxt_bad.rvz", s_rvz, good - 40U));
    EXPECT_EQ(disc_extract("dxt_bad.rvz", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_CORRUPT);
    /* compressão LZMA, LZMA2 e bzip2 */
    const u32 unsupported[3] = {3, 4, 2};
    for (u32 i = 0; i < 3; i++) {
        put32(s_rvz + 0x48 + 4, unsupported[i]);
        EXPECT_TRUE(write_file("dxt_bad.rvz", s_rvz, good));
        EXPECT_EQ(disc_extract("dxt_bad.rvz", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_COMPRESSION);
    }
    put32(s_rvz + 0x48 + 4, 5);
    /* RVZ de Wii */
    put32(s_rvz + 0x48, 2);
    EXPECT_TRUE(write_file("dxt_bad.rvz", s_rvz, good));
    EXPECT_EQ(disc_extract("dxt_bad.rvz", "dxt_out_bad", NULL, NULL), DISC_EXTRACT_ERR_NOT_GAMECUBE);
    put32(s_rvz + 0x48, 1);
    /* quadro zstd do grupo 3 (b.bin) com a magia estragada: o erro aparece na extração, que não deixa nada */
    build_rvz(s_iso, 5);
    for (size_t i = 0x124; i + 4 < s_rvz_size; i++) {
        if (memcmp(s_rvz + i, "\x28\xB5\x2F\xFD", 4) == 0 && s_rvz[i + 5] == 0 && s_rvz[i + 6] == 0x80) {
            s_rvz[i] ^= 0xFF; /* primeiro quadro de 0x8000 bytes = grupo 3 */
            break;
        }
    }
    EXPECT_TRUE(write_file("dxt_bad.rvz", s_rvz, s_rvz_size));
    remove_tree("dxt_out_bad");
    EXPECT_EQ(extract_keep("dxt_bad.rvz", "dxt_out_bad", 0, NULL, NULL), DISC_EXTRACT_ERR_CORRUPT);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_bad"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_bad.parte"));
}

TEST(rejects_unsafe_fst_names) {
    build_iso(s_iso);
    u8 *names = s_iso + FST_OFFSET + 84U;
    names[6] = '.'; /* "sub" vira ".." */
    names[7] = '.';
    names[8] = 0;
    EXPECT_TRUE(write_file("dxt_bad.iso", s_iso, IMG_SIZE));
    remove_tree("dxt_out_bad");
    EXPECT_EQ(extract_keep("dxt_bad.iso", "dxt_out_bad", 0, NULL, NULL), DISC_EXTRACT_ERR_TREE);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_bad"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_bad.parte"));
}

typedef struct Cancel {
    u32 calls;
    u32 cancel_at;
} Cancel;

static bool cancel_progress(const DiscExtractProgress *p, void *user) {
    (void)p;
    Cancel *c = (Cancel *)user;
    return ++c->calls < c->cancel_at;
}

TEST(cancel_leaves_no_folder) {
    build_iso(s_iso);
    EXPECT_TRUE(write_file("dxt_mini.iso", s_iso, IMG_SIZE));
    remove_tree("dxt_out_cancel");
    /* cancela no meio (já com sys/ e parte de files/ gravados) */
    Cancel c = {0, 9};
    EXPECT_EQ(extract_keep("dxt_mini.iso", "dxt_out_cancel", 0, cancel_progress, &c), DISC_EXTRACT_ERR_CANCELLED);
    EXPECT_TRUE(c.calls >= 9U);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_cancel"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_cancel.parte"));
    /* pela flag, de outra thread (aqui já levantada antes) */
    volatile int flag = 1;
    DiscExtractOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.keep_other_game = true;
    opt.cancel = &flag;
    EXPECT_EQ(disc_extract_ex("dxt_mini.iso", "dxt_out_cancel", &opt), DISC_EXTRACT_ERR_CANCELLED);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_cancel"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_cancel.parte"));
    /* e depois da recusa dá para extrair de novo */
    EXPECT_EQ(extract_keep("dxt_mini.iso", "dxt_out_cancel", 0, NULL, NULL), DISC_EXTRACT_ERR_OTHER_GAME);
    expect_tree("dxt_out_cancel", s_iso);
    remove_tree("dxt_out_cancel");
}

TEST(full_disk_is_reported_and_cleaned) {
    build_iso(s_iso);
    EXPECT_TRUE(write_file("dxt_mini.iso", s_iso, IMG_SIZE));
    remove_tree("dxt_out_full");
    EXPECT_EQ(extract_keep("dxt_mini.iso", "dxt_out_full", 0x6000, NULL, NULL), DISC_EXTRACT_ERR_DISK_FULL);
    EXPECT_TRUE(!platform_fs_exists("dxt_out_full"));
    EXPECT_TRUE(!platform_fs_exists("dxt_out_full.parte"));
}

int main(void) {
    RUN_TEST(junk_generator_is_seekable_and_zero_seed_is_zero);
    RUN_TEST(iso_and_rvz_read_the_same_image);
    RUN_TEST(extracts_iso_and_rvz_to_the_same_tree);
    RUN_TEST(other_game_without_keep_writes_nothing);
    RUN_TEST(existing_target_is_refused_and_empty_target_is_filled);
    RUN_TEST(rejects_bad_images_with_distinct_errors);
    RUN_TEST(rejects_damaged_rvz);
    RUN_TEST(rejects_unsafe_fst_names);
    RUN_TEST(cancel_leaves_no_folder);
    RUN_TEST(full_disk_is_reported_and_cleaned);
    (void)remove("dxt_mini.iso");
    (void)remove("dxt_mini.rvz");
    (void)remove("dxt_bad.img");
    (void)remove("dxt_bad.rvz");
    (void)remove("dxt_bad.iso");
    return am_test_finish();
}
