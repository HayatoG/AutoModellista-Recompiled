/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitor de RVZ (GameCube) por deslocamento do disco. Formato e semântica copiados do Dolphin
 * (Source/Core/DiscIO/WIABlob.cpp, WIACompression.cpp e LaggedFibonacciGenerator.cpp; docs/WiaAndRvz.md):
 *
 *   0x00  cabeçalho 1 (0x48): magia "RVZ\x01", versões, tamanho do cabeçalho 2, SHA-1, tamanho do ISO (0x24) e do
 *         arquivo (0x2C)
 *   0x48  cabeçalho 2 (0xDC): tipo de disco, compressão, tamanho do bloco, os 0x80 primeiros bytes do disco, e a
 *         posição das tabelas de "raw data" (faixas do disco) e de grupos
 *   tabelas: comprimidas com o mesmo método dos grupos; cada raw data aponta para uma faixa de grupos de `chunk`
 *            bytes; cada grupo do RVZ tem 12 bytes (deslocamento >> 2, tamanho com o bit 31 = comprimido, e o
 *            rvz_packed_size).
 *   grupo "empacotado" (rvz_packed_size != 0): depois de descomprimido é uma sequência de segmentos
 *            [u32 tamanho | bit 31 = lixo][68 bytes de semente, só no lixo | tamanho bytes de dados].
 *
 * Tudo em big-endian. Aqui só GameCube: sem partições, listas de exceção de hash nem criptografia. */
#include <stdlib.h>
#include <string.h>

#include "am/disc_image.h"
#include "am/platform_fs.h"

#include "zstd.h"

#define RVZ_HEADER1_SIZE 0x48U
#define RVZ_HEADER2_MIN  0xD5U /* até compressor_data_size (o Dolphin aceita mais, com dados do compressor) */
#define RVZ_DISC_HEADER  0x80U
#define RVZ_SECTOR       0x8000U /* o lixo é regenerado a cada setor de 32 KiB do disco */
#define RVZ_GROUP_ENTRY  12U
#define RVZ_RAW_ENTRY    0x18U
#define RVZ_SEED_BYTES   68U
#define RVZ_MAX_TABLE    (64U << 20)
#define RVZ_MAX_CHUNK    (64U << 20)
#define RVZ_NO_GROUP     (~(u64)0)
/* RVZ_VERSION_READ_COMPATIBLE: versão mínima que lemos; RVZ_VERSION: a máxima que o campo "compatível" pode exigir. */
#define RVZ_VERSION_READ_COMPATIBLE 0x00030000U
#define RVZ_VERSION_CURRENT         0x01000000U

enum {
    RVZ_COMP_NONE = 0,
    RVZ_COMP_PURGE = 1,
    RVZ_COMP_BZIP2 = 2,
    RVZ_COMP_LZMA = 3,
    RVZ_COMP_LZMA2 = 4,
    RVZ_COMP_ZSTD = 5,
};

/* --- Gerador de lixo: Lagged Fibonacci K=521, J=32 (LaggedFibonacciGenerator.cpp) ------------------------------- */

#define LFG_K     521U
#define LFG_J     32U
#define LFG_SEED  17U
#define LFG_BYTES (LFG_K * 4U)

typedef struct Lfg {
    u32 buf[LFG_K];
    u32 pos; /* em bytes dentro de buf */
} Lfg;

static void lfg_step(Lfg *l) {
    for (u32 i = 0; i < LFG_J; i++) {
        l->buf[i] ^= l->buf[i + LFG_K - LFG_J];
    }
    for (u32 i = LFG_J; i < LFG_K; i++) {
        l->buf[i] ^= l->buf[i - LFG_J];
    }
}

static u32 swap32(u32 x) {
    return (x >> 24) | ((x >> 8) & 0xFF00U) | ((x << 8) & 0xFF0000U) | (x << 24);
}

/* SetSeed + Initialize(false): expande a semente e já aplica o "shift por 18 em vez de 16" e o byte-swap, de modo que
 * os bytes de saída são os de buf em little-endian (o que o Dolphin lê num host little-endian). */
static void lfg_set_seed(Lfg *l, const u8 *seed) {
    for (u32 i = 0; i < LFG_SEED; i++) {
        l->buf[i] = disc_be32(seed + (size_t)i * 4U);
    }
    for (u32 i = LFG_SEED; i < LFG_K; i++) {
        l->buf[i] = (l->buf[i - 17U] << 23) ^ (l->buf[i - 16U] >> 9) ^ l->buf[i - 1U];
    }
    for (u32 i = 0; i < LFG_K; i++) {
        const u32 x = l->buf[i];
        l->buf[i] = swap32((x & 0xFF00FFFFU) | ((x >> 2) & 0x00FF0000U));
    }
    for (u32 i = 0; i < 4; i++) {
        lfg_step(l);
    }
    l->pos = 0;
}

static void lfg_forward(Lfg *l, u32 count) {
    l->pos += count;
    while (l->pos >= LFG_BYTES) {
        lfg_step(l);
        l->pos -= LFG_BYTES;
    }
}

static void lfg_bytes(Lfg *l, u8 *dst, u32 count) {
    while (count > 0) {
        const u32 word = l->buf[l->pos / 4U];
        dst[0] = (u8)(word >> (8U * (l->pos & 3U)));
        dst++;
        count--;
        l->pos++;
        if (l->pos == LFG_BYTES) {
            lfg_step(l);
            l->pos = 0;
        }
    }
}

void disc_rvz_junk(const u8 seed[68], u32 offset_in_sector, u8 *dst, u32 size) {
    Lfg *l = (Lfg *)malloc(sizeof(*l)); /* 2 KiB: fora da pilha das threads pequenas */
    if (l == NULL) {
        memset(dst, 0, size);
        return;
    }
    lfg_set_seed(l, seed);
    lfg_forward(l, offset_in_sector % RVZ_SECTOR);
    lfg_bytes(l, dst, size);
    free(l);
}

/* --- Leitor ---------------------------------------------------------------------------------------------------- */

typedef struct RvzRaw {
    u64 offset;
    u64 size;
    u32 group_index;
    u32 groups;
} RvzRaw;

typedef struct RvzGroup {
    u64 file_offset;
    u32 data_size; /* sem o bit de "comprimido" */
    u32 packed;    /* rvz_packed_size */
    bool compressed;
} RvzGroup;

typedef struct DiscRvz {
    DiscSource base;
    FILE *file;
    u64 file_size;
    u32 compression;
    u32 chunk;
    u8 disc_header[RVZ_DISC_HEADER];
    RvzRaw *raws;
    u32 raw_count;
    RvzGroup *groups;
    u32 group_count;
    u8 *cache; /* o último grupo descomprimido (chunk bytes) */
    u32 cache_len;
    u64 cache_key; /* (entrada << 32) | grupo na entrada */
    u8 *in;        /* dados comprimidos do grupo */
    u8 *packed;    /* grupo descomprimido ainda empacotado */
    u32 buf_cap;
} DiscRvz;

static DiscImageResult read_file(DiscRvz *r, u64 offset, u8 *dst, size_t size) {
    if (offset > r->file_size || size > r->file_size - offset) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    if (size == 0) {
        return DISC_IMAGE_OK;
    }
    if (!platform_fs_seek(r->file, offset) || fread(dst, 1, size, r->file) != size) {
        return DISC_IMAGE_ERR_IO;
    }
    return DISC_IMAGE_OK;
}

/* Descomprime `in` em `out`, que deve resultar em exatamente `out_size` bytes. */
static DiscImageResult inflate_exact(const DiscRvz *r, bool compressed, const u8 *in, size_t in_size, u8 *out,
                                     size_t out_size) {
    if (!compressed || r->compression == RVZ_COMP_NONE) {
        if (in_size != out_size) {
            return DISC_IMAGE_ERR_CORRUPT;
        }
        memcpy(out, in, out_size);
        return DISC_IMAGE_OK;
    }
    const size_t got = ZSTD_decompress(out, out_size, in, in_size);
    if (ZSTD_isError(got) || got != out_size) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    return DISC_IMAGE_OK;
}

static DiscImageResult load_table(DiscRvz *r, u64 offset, u32 stored, size_t raw_size, u8 **out) {
    if (raw_size > RVZ_MAX_TABLE || stored > RVZ_MAX_TABLE) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    u8 *in = (u8 *)malloc(stored != 0 ? stored : 1U);
    u8 *table = (u8 *)malloc(raw_size != 0 ? raw_size : 1U);
    if (in == NULL || table == NULL) {
        free(in);
        free(table);
        return DISC_IMAGE_ERR_NOMEM;
    }
    DiscImageResult res = read_file(r, offset, in, stored);
    if (res == DISC_IMAGE_OK) {
        res = inflate_exact(r, true, in, stored, table, raw_size);
    }
    free(in);
    if (res != DISC_IMAGE_OK) {
        free(table);
        return res;
    }
    *out = table;
    return DISC_IMAGE_OK;
}

/* Expande os segmentos de dados e de lixo de um grupo empacotado em `out` (clen bytes). `pos` é o deslocamento do
 * grupo em relação ao início alinhado da faixa: o Dolphin usa pos % 0x8000 para posicionar o gerador de lixo. */
static DiscImageResult unpack_group(const u8 *packed, size_t plen, u8 *out, u32 clen, u64 pos) {
    size_t ip = 0;
    u32 op = 0;
    while (op < clen) {
        if (plen - ip < 4U) {
            return DISC_IMAGE_ERR_CORRUPT;
        }
        const u32 head = disc_be32(packed + ip);
        ip += 4U;
        const u32 n = head & 0x7FFFFFFFU;
        if (n > clen - op) {
            return DISC_IMAGE_ERR_CORRUPT;
        }
        if ((head & 0x80000000U) != 0) {
            if (plen - ip < RVZ_SEED_BYTES) {
                return DISC_IMAGE_ERR_CORRUPT;
            }
            disc_rvz_junk(packed + ip, (u32)(pos % RVZ_SECTOR), out + op, n);
            ip += RVZ_SEED_BYTES;
        } else {
            if (plen - ip < n) {
                return DISC_IMAGE_ERR_CORRUPT;
            }
            memcpy(out + op, packed + ip, n);
            ip += n;
        }
        op += n;
        pos += n;
    }
    return ip == plen ? DISC_IMAGE_OK : DISC_IMAGE_ERR_CORRUPT;
}

static DiscImageResult load_group(DiscRvz *r, u32 entry, u32 index) {
    const u64 key = ((u64)entry << 32) | index;
    if (r->cache_key == key) {
        return DISC_IMAGE_OK;
    }
    r->cache_key = RVZ_NO_GROUP;
    const RvzRaw *raw = &r->raws[entry];
    const u64 aligned_size = raw->size + raw->offset % RVZ_SECTOR;
    const u64 group_pos = (u64)index * r->chunk;
    const u64 left = aligned_size - group_pos;
    const u32 clen = left < r->chunk ? (u32)left : r->chunk;
    const RvzGroup *g = &r->groups[raw->group_index + index];

    if (g->data_size == 0) { /* grupo todo de zeros */
        memset(r->cache, 0, clen);
    } else {
        const u32 expect = g->packed != 0 ? g->packed : clen;
        if (g->data_size > r->buf_cap || expect > r->buf_cap) {
            return DISC_IMAGE_ERR_CORRUPT;
        }
        DiscImageResult res = read_file(r, g->file_offset, r->in, g->data_size);
        if (res != DISC_IMAGE_OK) {
            return res;
        }
        if (g->packed == 0) {
            res = inflate_exact(r, g->compressed, r->in, g->data_size, r->cache, clen);
        } else {
            res = inflate_exact(r, g->compressed, r->in, g->data_size, r->packed, g->packed);
            if (res == DISC_IMAGE_OK) {
                res = unpack_group(r->packed, g->packed, r->cache, clen, group_pos);
            }
        }
        if (res != DISC_IMAGE_OK) {
            return res;
        }
    }
    r->cache_len = clen;
    r->cache_key = key;
    return DISC_IMAGE_OK;
}

static const RvzRaw *find_raw(const DiscRvz *r, u64 pos, u32 *entry) {
    for (u32 i = 0; i < r->raw_count; i++) {
        const RvzRaw *e = &r->raws[i];
        if (e->size != 0 && pos >= e->offset && pos - e->offset < e->size) {
            *entry = i;
            return e;
        }
    }
    return NULL;
}

static DiscImageResult rvz_read(DiscSource *src, u64 offset, u8 *dst, u32 size) {
    DiscRvz *r = (DiscRvz *)src;
    if (offset > src->size || size > src->size - offset) {
        return DISC_IMAGE_ERR_RANGE;
    }
    while (size > 0) {
        u32 n = size;
        if (offset < RVZ_DISC_HEADER) { /* o cabeçalho do disco mora no cabeçalho 2 */
            if (n > RVZ_DISC_HEADER - offset) {
                n = (u32)(RVZ_DISC_HEADER - offset);
            }
            memcpy(dst, &r->disc_header[offset], n);
        } else {
            u32 entry = 0;
            const RvzRaw *raw = find_raw(r, offset, &entry);
            if (raw == NULL) {
                return DISC_IMAGE_ERR_CORRUPT; /* buraco entre as faixas */
            }
            const u64 rel = offset - (raw->offset - raw->offset % RVZ_SECTOR);
            const u32 index = (u32)(rel / r->chunk);
            const DiscImageResult res = load_group(r, entry, index);
            if (res != DISC_IMAGE_OK) {
                return res;
            }
            const u32 in_group = (u32)(rel % r->chunk);
            if (in_group >= r->cache_len) {
                return DISC_IMAGE_ERR_CORRUPT;
            }
            if (n > r->cache_len - in_group) {
                n = r->cache_len - in_group;
            }
            memcpy(dst, r->cache + in_group, n);
        }
        dst += n;
        offset += n;
        size -= n;
    }
    return DISC_IMAGE_OK;
}

static void rvz_destroy(DiscSource *src) {
    DiscRvz *r = (DiscRvz *)src;
    (void)fclose(r->file);
    free(r->raws);
    free(r->groups);
    free(r->cache);
    free(r->in);
    free(r->packed);
    free(r);
}

static DiscImageResult parse_raws(DiscRvz *r, const u8 *table, u32 count) {
    r->raws = (RvzRaw *)calloc(count != 0 ? count : 1U, sizeof(RvzRaw));
    if (r->raws == NULL) {
        return DISC_IMAGE_ERR_NOMEM;
    }
    r->raw_count = count;
    for (u32 i = 0; i < count; i++) {
        const u8 *p = table + (size_t)i * RVZ_RAW_ENTRY;
        RvzRaw *e = &r->raws[i];
        e->offset = disc_be64(p);
        e->size = disc_be64(p + 8);
        e->group_index = disc_be32(p + 16);
        e->groups = disc_be32(p + 20);
        if (e->size == 0) {
            continue;
        }
        const u64 aligned = e->size + e->offset % RVZ_SECTOR;
        if (e->offset > r->base.size || e->size > r->base.size - e->offset ||
            (u64)e->group_index + e->groups > r->group_count || (u64)e->groups * r->chunk < aligned) {
            return DISC_IMAGE_ERR_CORRUPT;
        }
    }
    return DISC_IMAGE_OK;
}

static DiscImageResult parse_groups(DiscRvz *r, const u8 *table, u32 count) {
    r->groups = (RvzGroup *)calloc(count != 0 ? count : 1U, sizeof(RvzGroup));
    if (r->groups == NULL) {
        return DISC_IMAGE_ERR_NOMEM;
    }
    r->group_count = count;
    for (u32 i = 0; i < count; i++) {
        const u8 *p = table + (size_t)i * RVZ_GROUP_ENTRY;
        RvzGroup *g = &r->groups[i];
        g->file_offset = (u64)disc_be32(p) << 2;
        const u32 size = disc_be32(p + 4);
        g->compressed = (size & 0x80000000U) != 0;
        g->data_size = size & 0x7FFFFFFFU;
        g->packed = disc_be32(p + 8);
    }
    return DISC_IMAGE_OK;
}

/* Onde estão as tabelas (cabeçalho 2). */
typedef struct RvzLayout {
    u64 raw_offset;
    u32 raw_stored;
    u32 raw_count;
    u64 group_offset;
    u32 group_stored;
    u32 group_count;
} RvzLayout;

/* Cabeçalhos 1 e 2 e verificações de formato. */
static DiscImageResult parse_headers(DiscRvz *r, RvzLayout *lay) {
    u8 h1[RVZ_HEADER1_SIZE];
    if (read_file(r, 0, h1, sizeof(h1)) != DISC_IMAGE_OK) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    const u32 version = disc_be32(h1 + 4);
    const u32 compatible = disc_be32(h1 + 8);
    const u32 h2_size = disc_be32(h1 + 12);
    /* O Dolphin recusa arquivo cujo tamanho difere do gravado: é assim que um RVZ truncado aparece. */
    if (version < RVZ_VERSION_READ_COMPATIBLE || compatible > RVZ_VERSION_CURRENT ||
        disc_be64(h1 + 0x2C) != r->file_size) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    r->base.size = disc_be64(h1 + 0x24);
    if (h2_size < RVZ_HEADER2_MIN || h2_size > 0x1000U) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    u8 h2[0x1000];
    if (read_file(r, RVZ_HEADER1_SIZE, h2, h2_size) != DISC_IMAGE_OK) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    if (disc_be32(h2) != 1U || disc_be32(h2 + 0x90) != 0) { /* tipo 1 = GameCube; sem partições */
        return DISC_IMAGE_ERR_NOT_GAMECUBE;
    }
    r->compression = disc_be32(h2 + 4);
    r->chunk = disc_be32(h2 + 12);
    if (r->compression > RVZ_COMP_ZSTD || r->compression == RVZ_COMP_PURGE) {
        return DISC_IMAGE_ERR_FORMAT; /* o RVZ não tem "purge" como compressão (só o WIA tem) */
    }
    if (r->compression == RVZ_COMP_BZIP2 || r->compression == RVZ_COMP_LZMA || r->compression == RVZ_COMP_LZMA2) {
        return DISC_IMAGE_ERR_COMPRESSION;
    }
    if (r->chunk < RVZ_SECTOR || r->chunk > RVZ_MAX_CHUNK || r->chunk % RVZ_SECTOR != 0 || r->base.size == 0) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    memcpy(r->disc_header, h2 + 0x10, RVZ_DISC_HEADER);
    static const u8 gc_magic[4] = {0xC2, 0x33, 0x9F, 0x3D};
    if (memcmp(&r->disc_header[0x1C], gc_magic, 4) != 0) {
        return DISC_IMAGE_ERR_NOT_GAMECUBE;
    }
    lay->raw_count = disc_be32(h2 + 0xB4);
    lay->raw_offset = disc_be64(h2 + 0xB8);
    lay->raw_stored = disc_be32(h2 + 0xC0);
    lay->group_count = disc_be32(h2 + 0xC4);
    lay->group_offset = disc_be64(h2 + 0xC8);
    lay->group_stored = disc_be32(h2 + 0xD0);
    if (lay->raw_count > RVZ_MAX_TABLE / RVZ_RAW_ENTRY || lay->group_count > RVZ_MAX_TABLE / RVZ_GROUP_ENTRY) {
        return DISC_IMAGE_ERR_CORRUPT;
    }
    return DISC_IMAGE_OK;
}

static DiscImageResult open_tables(DiscRvz *r, const RvzLayout *lay) {
    u8 *table = NULL;
    DiscImageResult res =
        load_table(r, lay->group_offset, lay->group_stored, (size_t)lay->group_count * RVZ_GROUP_ENTRY, &table);
    if (res != DISC_IMAGE_OK) {
        return res;
    }
    res = parse_groups(r, table, lay->group_count);
    free(table);
    if (res != DISC_IMAGE_OK) {
        return res;
    }
    res = load_table(r, lay->raw_offset, lay->raw_stored, (size_t)lay->raw_count * RVZ_RAW_ENTRY, &table);
    if (res != DISC_IMAGE_OK) {
        return res;
    }
    res = parse_raws(r, table, lay->raw_count);
    free(table);
    if (res != DISC_IMAGE_OK) {
        return res;
    }
    r->buf_cap = 2U * r->chunk + 0x1000U; /* o zstd pode passar um pouco do tamanho do bloco */
    r->cache = (u8 *)malloc(r->chunk);
    r->in = (u8 *)malloc(r->buf_cap);
    r->packed = (u8 *)malloc(r->buf_cap);
    return (r->cache != NULL && r->in != NULL && r->packed != NULL) ? DISC_IMAGE_OK : DISC_IMAGE_ERR_NOMEM;
}

DiscImageResult disc_rvz_open(FILE *file, u64 file_size, DiscSource **out) {
    *out = NULL;
    DiscRvz *r = (DiscRvz *)calloc(1, sizeof(*r));
    if (r == NULL) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_NOMEM;
    }
    r->file = file;
    r->file_size = file_size;
    r->cache_key = RVZ_NO_GROUP;
    r->base.read = rvz_read;
    r->base.destroy = rvz_destroy;
    RvzLayout lay;
    DiscImageResult res = parse_headers(r, &lay);
    if (res == DISC_IMAGE_OK) {
        res = open_tables(r, &lay);
    }
    if (res != DISC_IMAGE_OK) {
        rvz_destroy(&r->base);
        return res;
    }
    *out = &r->base;
    return DISC_IMAGE_OK;
}
