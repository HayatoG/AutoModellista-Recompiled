/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/log.h"

#define DISC_PATH_MAX   1024U
#define DISC_DEPTH_MAX  32U
#define DISC_HANDLES    4U
#define DISC_BOOT_SIZE  0x440U
#define DISC_BI2_OFFSET 0x440U
#define DISC_BI2_SIZE   0x2000U
#define DISC_APPLOADER  0x2440U
#define DISC_FST_ENTRY  12U

typedef struct DiscRegion {
    u64 offset;
    u64 size;
    char *path; /* relativo à raiz */
} DiscRegion;

typedef struct DiscHandle {
    FILE *file;
    const DiscRegion *region;
    u64 last_use;
} DiscHandle;

static struct {
    char root[DISC_PATH_MAX];
    DiscRegion *regions;
    u32 count;
    u32 capacity;
    DiscHandle handles[DISC_HANDLES];
    u64 clock;
    bool open;
    DiscStats stats;
} s_disc;

const char *disc_result_name(DiscResult result) {
    switch (result) {
    case DISC_OK:
        return "ok";
    case DISC_ERR_SYS:
        return "sys/ incompleto (boot.bin, bi2.bin, apploader.img, main.dol, fst.bin)";
    case DISC_ERR_FST:
        return "FST malformado";
    case DISC_ERR_FILE:
        return "arquivo do FST ausente ou com tamanho diferente em files/";
    case DISC_ERR_OVERLAP:
        return "duas regiões no mesmo deslocamento do disco";
    case DISC_ERR_NOMEM:
        return "sem memória";
    }
    return "?";
}

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static bool join(char *out, size_t size, const char *a, const char *b) {
    const int n = snprintf(out, size, "%s/%s", a, b);
    return n > 0 && (size_t)n < size;
}

/* Tamanho do arquivo do host, ou -1. */
static s64 host_file_size(const char *rel) {
    char path[DISC_PATH_MAX];
    if (!join(path, sizeof(path), s_disc.root, rel)) {
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    s64 size = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        size = (s64)ftell(f);
    }
    (void)fclose(f);
    return size;
}

static DiscResult add_region(u64 offset, u64 size, const char *rel) {
    if (s_disc.count == s_disc.capacity) {
        const u32 cap = s_disc.capacity == 0U ? 64U : s_disc.capacity * 2U;
        DiscRegion *grown = realloc(s_disc.regions, cap * sizeof(*grown));
        if (grown == NULL) {
            return DISC_ERR_NOMEM;
        }
        s_disc.regions = grown;
        s_disc.capacity = cap;
    }
    const size_t len = strlen(rel) + 1U;
    char *copy = malloc(len);
    if (copy == NULL) {
        return DISC_ERR_NOMEM;
    }
    memcpy(copy, rel, len);
    s_disc.regions[s_disc.count++] = (DiscRegion){.offset = offset, .size = size, .path = copy};
    return DISC_OK;
}

/* Arquivo de sys/ inteiro em memória (boot.bin, fst.bin), para ler o cabeçalho e o FST. */
static u8 *load_sys(const char *name, size_t *size) {
    char rel[64];
    char path[DISC_PATH_MAX];
    (void)snprintf(rel, sizeof(rel), "sys/%s", name);
    if (!join(path, sizeof(path), s_disc.root, rel)) {
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    u8 *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc((size_t)n);
            if (data != NULL && fread(data, 1, (size_t)n, f) != (size_t)n) {
                free(data);
                data = NULL;
            }
            *size = (size_t)n;
        }
    }
    (void)fclose(f);
    return data;
}

static DiscResult add_sys(const char *name, u64 offset, u64 expected) {
    char rel[64];
    (void)snprintf(rel, sizeof(rel), "sys/%s", name);
    const s64 size = host_file_size(rel);
    if (size < 0 || (expected != 0U && (u64)size != expected)) {
        return DISC_ERR_SYS;
    }
    return add_region(offset, (u64)size, rel);
}

typedef struct FstDir {
    u32 end; /* primeiro índice depois do diretório */
    size_t path_len;
} FstDir;

/* Percorre o FST em ordem: diretórios empilham o prefixo do caminho até o índice "próximo". */
static DiscResult walk_fst(const u8 *fst, size_t size) {
    if (size < DISC_FST_ENTRY || (fst[0] & 1U) == 0U) {
        return DISC_ERR_FST;
    }
    const u32 count = be32(&fst[8]);
    if (count == 0U || (u64)count * DISC_FST_ENTRY > size) {
        return DISC_ERR_FST;
    }
    const char *names = (const char *)fst + ((size_t)count * DISC_FST_ENTRY);
    const size_t names_size = size - ((size_t)count * DISC_FST_ENTRY);
    char path[DISC_PATH_MAX] = "files";
    FstDir stack[DISC_DEPTH_MAX];
    u32 depth = 0;
    stack[depth++] = (FstDir){.end = count, .path_len = strlen(path)};
    for (u32 i = 1; i < count; i++) {
        while (depth > 1U && i >= stack[depth - 1U].end) {
            depth--;
        }
        path[stack[depth - 1U].path_len] = '\0';
        const u8 *e = &fst[(size_t)i * DISC_FST_ENTRY];
        const u32 name_off = be32(e) & 0x00FFFFFFU;
        if (name_off >= names_size || memchr(names + name_off, '\0', names_size - name_off) == NULL) {
            return DISC_ERR_FST;
        }
        const size_t len = strlen(path);
        const int n = snprintf(path + len, sizeof(path) - len, "/%s", names + name_off);
        if (n < 0 || (size_t)n >= sizeof(path) - len) {
            return DISC_ERR_FST;
        }
        if ((e[0] & 1U) != 0U) {
            const u32 next = be32(&e[8]);
            if (next <= i || next > count || depth == DISC_DEPTH_MAX) {
                return DISC_ERR_FST;
            }
            stack[depth++] = (FstDir){.end = next, .path_len = strlen(path)};
            continue;
        }
        const u64 offset = be32(&e[4]);
        const u64 fsize = be32(&e[8]);
        if (host_file_size(path) != (s64)fsize) {
            AM_LOG_ERROR("disco: %s deveria ter %llu bytes", path, (unsigned long long)fsize);
            return DISC_ERR_FILE;
        }
        const DiscResult r = fsize != 0U ? add_region(offset, fsize, path) : DISC_OK;
        if (r != DISC_OK) {
            return r;
        }
    }
    return DISC_OK;
}

static int by_offset(const void *a, const void *b) {
    const DiscRegion *x = a;
    const DiscRegion *y = b;
    if (x->offset != y->offset) {
        return x->offset < y->offset ? -1 : 1;
    }
    return 0;
}

static DiscResult build(void) {
    size_t boot_size = 0;
    size_t fst_size = 0;
    u8 *boot = load_sys("boot.bin", &boot_size);
    u8 *fst = load_sys("fst.bin", &fst_size);
    DiscResult r = DISC_ERR_SYS;
    if (boot != NULL && fst != NULL && boot_size >= DISC_BOOT_SIZE && be32(&boot[0x428]) == fst_size) {
        r = add_sys("boot.bin", 0, DISC_BOOT_SIZE);
        r = r == DISC_OK ? add_sys("bi2.bin", DISC_BI2_OFFSET, DISC_BI2_SIZE) : r;
        r = r == DISC_OK ? add_sys("apploader.img", DISC_APPLOADER, 0) : r;
        r = r == DISC_OK ? add_sys("main.dol", be32(&boot[0x420]), 0) : r;
        r = r == DISC_OK ? add_sys("fst.bin", be32(&boot[0x424]), fst_size) : r;
        r = r == DISC_OK ? walk_fst(fst, fst_size) : r;
    }
    free(boot);
    free(fst);
    if (r != DISC_OK) {
        return r;
    }
    qsort(s_disc.regions, s_disc.count, sizeof(DiscRegion), by_offset);
    for (u32 i = 1; i < s_disc.count; i++) {
        if (s_disc.regions[i - 1U].offset + s_disc.regions[i - 1U].size > s_disc.regions[i].offset) {
            AM_LOG_ERROR("disco: %s e %s se sobrepõem", s_disc.regions[i - 1U].path, s_disc.regions[i].path);
            return DISC_ERR_OVERLAP;
        }
    }
    return DISC_OK;
}

DiscResult disc_open(const char *root) {
    disc_close();
    const int n = snprintf(s_disc.root, sizeof(s_disc.root), "%s", root);
    if (n < 0 || (size_t)n >= sizeof(s_disc.root)) {
        return DISC_ERR_SYS;
    }
    const DiscResult r = build();
    if (r != DISC_OK) {
        disc_close();
        return r;
    }
    s_disc.open = true;
    s_disc.stats.regions = s_disc.count;
    return DISC_OK;
}

void disc_close(void) {
    for (u32 i = 0; i < DISC_HANDLES; i++) {
        if (s_disc.handles[i].file != NULL) {
            (void)fclose(s_disc.handles[i].file);
        }
    }
    for (u32 i = 0; i < s_disc.count; i++) {
        free(s_disc.regions[i].path);
    }
    free(s_disc.regions);
    memset(&s_disc, 0, sizeof(s_disc));
}

bool disc_is_open(void) {
    return s_disc.open;
}

const DiscStats *disc_stats(void) {
    return &s_disc.stats;
}

/* Última região com offset <= `offset`, ou NULL. */
static const DiscRegion *find(u64 offset) {
    u32 lo = 0;
    u32 hi = s_disc.count;
    while (lo < hi) {
        const u32 mid = lo + ((hi - lo) / 2U);
        if (s_disc.regions[mid].offset <= offset) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo == 0U ? NULL : &s_disc.regions[lo - 1U];
}

const char *disc_region_at(u64 offset, u64 *offset_in_file) {
    const DiscRegion *r = find(offset);
    if (r == NULL || offset >= r->offset + r->size) {
        return NULL;
    }
    if (offset_in_file != NULL) {
        *offset_in_file = offset - r->offset;
    }
    return r->path;
}

/* Poucos arquivos abertos ao mesmo tempo (os .afs são lidos em sequência); o mais antigo sai. */
static FILE *handle_for(const DiscRegion *r) {
    DiscHandle *victim = &s_disc.handles[0];
    s_disc.clock++;
    for (u32 i = 0; i < DISC_HANDLES; i++) {
        DiscHandle *h = &s_disc.handles[i];
        if (h->region == r && h->file != NULL) {
            h->last_use = s_disc.clock;
            return h->file;
        }
        if (h->last_use < victim->last_use) {
            victim = h;
        }
    }
    if (victim->file != NULL) {
        (void)fclose(victim->file);
    }
    char path[DISC_PATH_MAX];
    *victim = (DiscHandle){0};
    if (join(path, sizeof(path), s_disc.root, r->path)) {
        victim->file = fopen(path, "rb");
    }
    victim->region = r;
    victim->last_use = s_disc.clock;
    return victim->file;
}

bool disc_read(u64 offset, u8 *dst, u32 size) {
    AM_ASSERT(s_disc.open, "disc_read sem disc_open");
    s_disc.stats.reads++;
    s_disc.stats.bytes += size;
    while (size > 0U) {
        const DiscRegion *r = find(offset);
        if (r == NULL || offset >= r->offset + r->size) {
            /* Enchimento até a próxima região (ou até o fim do pedido). */
            const DiscRegion *next = r == NULL ? &s_disc.regions[0] : r + 1;
            u64 gap = size;
            if (next < s_disc.regions + s_disc.count && next->offset > offset && next->offset - offset < gap) {
                gap = next->offset - offset;
            }
            memset(dst, 0, (size_t)gap);
            s_disc.stats.padding_bytes += gap;
            dst += gap;
            offset += gap;
            size -= (u32)gap;
            continue;
        }
        const u64 in_file = offset - r->offset;
        const u64 avail = r->size - in_file;
        const u32 n = avail < size ? (u32)avail : size;
        FILE *f = handle_for(r);
        if (f == NULL || fseek(f, (long)in_file, SEEK_SET) != 0 || fread(dst, 1, n, f) != n) {
            AM_LOG_ERROR("disco: falha lendo %u bytes de %s em +0x%llX", n, r->path, (unsigned long long)in_file);
            return false;
        }
        dst += n;
        offset += n;
        size -= n;
    }
    return true;
}
