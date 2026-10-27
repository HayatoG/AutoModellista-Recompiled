/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitor da árvore do disco (cabeçalho, bi2, apploader, FST, DOL) e extração para sys/ + files/.
 * Layout do disco do GameCube: YAGCD 13; FST como em core/disc.c (entradas de 12 bytes: flag de diretório e nome em
 * 24 bits, deslocamento ou pai, tamanho ou próximo índice). A saída copia a do `dtk disc extract`. */
#include "am/disc_extract.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "am/disc_image.h"
#include "am/game.h"
#include "am/platform_fs.h"
#include "am/sha256.h"

#define EX_BOOT_SIZE      0x440U
#define EX_BI2_OFFSET     0x440U
#define EX_BI2_SIZE       0x2000U
#define EX_APPLOADER_OFF  0x2440U
#define EX_APPLOADER_HEAD 0x20U
#define EX_APPLOADER_MAX  (8U << 20)
#define EX_FST_MAX        (16U << 20)
#define EX_FST_DEPTH      64U
#define EX_DOL_HEADER     0x100U
#define EX_DOL_SECTIONS   18U
#define EX_DOL_MAX        (32U << 20)
#define EX_PATH           1024U
#define EX_COPY_CHUNK     (1U << 20)
#define EX_FREE_MARGIN    (64ULL << 20)

typedef struct PathList {
    char **items;
    size_t count;
    size_t cap;
} PathList;

typedef struct Job Job;
typedef DiscExtractResult (*FstVisit)(Job *job, const char *rel, bool is_dir, u64 offset, u32 size);

struct Job {
    DiscExtractOptions opt;
    DiscSource *src;
    char out[EX_PATH];
    char tmp[EX_PATH];
    u8 boot[EX_BOOT_SIZE];
    u8 *fst;
    u32 fst_size;
    u64 fst_offset;
    u64 dol_offset;
    u32 dol_size;
    u8 *dol;
    u64 apploader_size;
    bool other_game;
    bool committed;
    bool tmp_created;
    u32 files_total;
    u32 files_done;
    u64 bytes_total;
    u64 bytes_done;
    u64 written;
    PathList files;
    PathList dirs;
    u8 *buf;
};

const char *disc_extract_result_text(DiscExtractResult result) {
    switch (result) {
    case DISC_EXTRACT_OK:
        return "Imagem extraída.";
    case DISC_EXTRACT_ERR_OPEN:
        return "Não foi possível abrir a imagem do jogo.";
    case DISC_EXTRACT_ERR_FORMAT:
        return "Formato desconhecido: use uma imagem ISO, GCM ou RVZ do GameCube.";
    case DISC_EXTRACT_ERR_WIA:
        return "Imagem WIA não é suportada: reconverta no Dolphin para RVZ (zstd) ou ISO.";
    case DISC_EXTRACT_ERR_COMPRESSION:
        return "RVZ com compressão não suportada (LZMA ou bzip2): reconverta no Dolphin com zstd ou use ISO.";
    case DISC_EXTRACT_ERR_NOT_GAMECUBE:
        return "Esta imagem não é de um disco de GameCube (disco do Wii não é suportado).";
    case DISC_EXTRACT_ERR_CORRUPT:
        return "A imagem está truncada ou corrompida: baixe ou converta de novo.";
    case DISC_EXTRACT_ERR_TREE:
        return "Os dados de sistema do disco (cabeçalho, FST ou DOL) são inválidos.";
    case DISC_EXTRACT_ERR_EXISTS:
        return "A pasta de destino já contém um jogo extraído.";
    case DISC_EXTRACT_ERR_DISK_FULL:
        return "Sem espaço no disco para extrair o jogo (cerca de 1 GB).";
    case DISC_EXTRACT_ERR_WRITE:
        return "Erro ao gravar os arquivos extraídos.";
    case DISC_EXTRACT_ERR_CANCELLED:
        return "Extração cancelada.";
    case DISC_EXTRACT_ERR_OTHER_GAME:
        return "Este disco não é o Auto Modellista (USA) revisão 0 de que o port foi gerado.";
    case DISC_EXTRACT_ERR_NOMEM:
        return "Memória insuficiente para extrair.";
    }
    return "Erro desconhecido.";
}

static void set_detail(const Job *job, const char *fmt, ...) {
    if (job->opt.detail == NULL || job->opt.detail_size == 0) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(job->opt.detail, job->opt.detail_size, fmt, args);
    va_end(args);
}

static DiscExtractResult map_image_error(DiscImageResult res) {
    switch (res) {
    case DISC_IMAGE_OK:
        return DISC_EXTRACT_OK;
    case DISC_IMAGE_ERR_OPEN:
        return DISC_EXTRACT_ERR_OPEN;
    case DISC_IMAGE_ERR_FORMAT:
        return DISC_EXTRACT_ERR_FORMAT;
    case DISC_IMAGE_ERR_WIA:
        return DISC_EXTRACT_ERR_WIA;
    case DISC_IMAGE_ERR_COMPRESSION:
        return DISC_EXTRACT_ERR_COMPRESSION;
    case DISC_IMAGE_ERR_NOT_GAMECUBE:
        return DISC_EXTRACT_ERR_NOT_GAMECUBE;
    case DISC_IMAGE_ERR_NOMEM:
        return DISC_EXTRACT_ERR_NOMEM;
    case DISC_IMAGE_ERR_CORRUPT:
    case DISC_IMAGE_ERR_RANGE:
    case DISC_IMAGE_ERR_IO:
        return DISC_EXTRACT_ERR_CORRUPT;
    }
    return DISC_EXTRACT_ERR_CORRUPT;
}

static DiscExtractResult read_src(Job *job, u64 offset, u8 *dst, u32 size) {
    const DiscImageResult res = disc_image_read(job->src, offset, dst, size);
    if (res != DISC_IMAGE_OK) {
        set_detail(job, "leitura da imagem em 0x%llX (%u bytes): %s", (unsigned long long)offset, size,
                   disc_image_result_name(res));
    }
    return map_image_error(res);
}

/* --- Lista de caminhos criados, para desfazer ---------------------------------------------------------------- */

static bool plist_add(PathList *list, const char *path) {
    if (list->count == list->cap) {
        const size_t cap = list->cap != 0 ? list->cap * 2U : 64U;
        char **items = (char **)realloc(list->items, cap * sizeof(char *));
        if (items == NULL) {
            return false;
        }
        list->items = items;
        list->cap = cap;
    }
    const size_t len = strlen(path) + 1U;
    char *copy = (char *)malloc(len);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, path, len);
    list->items[list->count++] = copy;
    return true;
}

static void plist_free(PathList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

/* Apaga o que esta extração criou: arquivos e depois diretórios, do último para o primeiro (só vazios saem). */
static void job_cleanup(Job *job) {
    for (size_t i = job->files.count; i > 0; i--) {
        (void)platform_fs_remove(job->files.items[i - 1U]);
    }
    for (size_t i = job->dirs.count; i > 0; i--) {
        (void)platform_fs_remove(job->dirs.items[i - 1U]);
    }
}

/* --- Árvore do disco --------------------------------------------------------------------------------------- */

static DiscExtractResult load_apploader_size(Job *job) {
    u8 head[EX_APPLOADER_HEAD];
    DiscExtractResult res = read_src(job, EX_APPLOADER_OFF, head, sizeof(head));
    if (res != DISC_EXTRACT_OK) {
        return res;
    }
    /* Cabeçalho do apploader: +0x14 tamanho do código, +0x18 tamanho do trailer (YAGCD 13.3). */
    const u64 size = (u64)EX_APPLOADER_HEAD + disc_be32(&head[0x14]) + disc_be32(&head[0x18]);
    if (size > EX_APPLOADER_MAX || EX_APPLOADER_OFF + size > job->src->size) {
        set_detail(job, "apploader com tamanho %llu", (unsigned long long)size);
        return DISC_EXTRACT_ERR_TREE;
    }
    job->apploader_size = size;
    return DISC_EXTRACT_OK;
}

static DiscExtractResult load_fst(Job *job) {
    job->fst_offset = disc_be32(&job->boot[0x424]);
    job->fst_size = disc_be32(&job->boot[0x428]);
    if (job->fst_size < 12U || job->fst_size > EX_FST_MAX || job->fst_offset + job->fst_size > job->src->size) {
        set_detail(job, "FST em 0x%llX com %u bytes", (unsigned long long)job->fst_offset, job->fst_size);
        return DISC_EXTRACT_ERR_TREE;
    }
    job->fst = (u8 *)malloc(job->fst_size);
    if (job->fst == NULL) {
        return DISC_EXTRACT_ERR_NOMEM;
    }
    return read_src(job, job->fst_offset, job->fst, job->fst_size);
}

/* O DOL não tem tamanho no disco: é o maior (deslocamento + tamanho) das 7 seções de texto e 11 de dados do
 * cabeçalho (deslocamentos em +0x00, tamanhos em +0x90). */
static DiscExtractResult load_dol(Job *job) {
    job->dol_offset = disc_be32(&job->boot[0x420]);
    u8 head[EX_DOL_HEADER];
    if (job->dol_offset + sizeof(head) > job->src->size) {
        return DISC_EXTRACT_ERR_TREE;
    }
    DiscExtractResult res = read_src(job, job->dol_offset, head, sizeof(head));
    if (res != DISC_EXTRACT_OK) {
        return res;
    }
    u64 end = EX_DOL_HEADER;
    for (u32 i = 0; i < EX_DOL_SECTIONS; i++) {
        const u64 section_end = (u64)disc_be32(&head[i * 4U]) + disc_be32(&head[0x90U + i * 4U]);
        if (disc_be32(&head[0x90U + i * 4U]) != 0 && section_end > end) {
            end = section_end;
        }
    }
    if (end > EX_DOL_MAX || job->dol_offset + end > job->src->size) {
        set_detail(job, "main.dol com tamanho %llu", (unsigned long long)end);
        return DISC_EXTRACT_ERR_TREE;
    }
    job->dol_size = (u32)end;
    job->dol = (u8 *)malloc(job->dol_size);
    if (job->dol == NULL) {
        return DISC_EXTRACT_ERR_NOMEM;
    }
    return read_src(job, job->dol_offset, job->dol, job->dol_size);
}

static DiscExtractResult load_tree(Job *job) {
    DiscExtractResult res = read_src(job, 0, job->boot, EX_BOOT_SIZE);
    if (res != DISC_EXTRACT_OK) {
        return res;
    }
    if (disc_be32(&job->boot[0x1C]) != 0xC2339F3DU) { /* magia do GameCube (YAGCD 13.1) */
        return DISC_EXTRACT_ERR_NOT_GAMECUBE;
    }
    if (EX_BI2_OFFSET + EX_BI2_SIZE > job->src->size) {
        return DISC_EXTRACT_ERR_CORRUPT;
    }
    res = load_apploader_size(job);
    if (res == DISC_EXTRACT_OK) {
        res = load_fst(job);
    }
    if (res == DISC_EXTRACT_OK) {
        res = load_dol(job);
    }
    return res;
}

/* Nome da FST: sem separadores, ".." nem caracteres que o Windows recusa (nada escapa da pasta de destino). */
static bool valid_name(const char *name, size_t len) {
    if (len == 0 || (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || strchr("/\\:*?\"<>|", (int)c) != NULL) {
            return false;
        }
    }
    return true;
}

typedef struct FstWalk {
    char path[EX_PATH];
    size_t prefix[EX_FST_DEPTH];
    u32 end[EX_FST_DEPTH];
    u32 depth;
} FstWalk;

static DiscExtractResult walk_entry(Job *job, FstWalk *w, FstVisit visit, u32 index, u32 count) {
    const u8 *entry = job->fst + (size_t)index * 12U;
    const u32 names_off = count * 12U;
    const u32 name_off = disc_be32(entry) & 0x00FFFFFFU;
    const bool is_dir = entry[0] != 0;
    const u32 a = disc_be32(entry + 4);
    const u32 b = disc_be32(entry + 8);
    if (names_off + name_off >= job->fst_size) {
        return DISC_EXTRACT_ERR_TREE;
    }
    const char *name = (const char *)job->fst + names_off + name_off;
    size_t len = 0;
    while (names_off + name_off + len < job->fst_size && name[len] != 0) {
        len++;
    }
    const size_t base = strlen(w->path);
    if (names_off + name_off + len >= job->fst_size || !valid_name(name, len) || base + 1U + len >= sizeof(w->path)) {
        set_detail(job, "nome inválido na FST (entrada %u)", index);
        return DISC_EXTRACT_ERR_TREE;
    }
    (void)snprintf(w->path + base, sizeof(w->path) - base, "/%.*s", (int)len, name);
    if (!is_dir) {
        const DiscExtractResult res = visit(job, w->path, false, a, b);
        w->path[base] = '\0';
        return res;
    }
    if (b <= index || b > count || w->depth >= EX_FST_DEPTH) {
        set_detail(job, "diretório inválido na FST (entrada %u)", index);
        return DISC_EXTRACT_ERR_TREE;
    }
    w->prefix[w->depth] = base;
    w->end[w->depth++] = b;
    return visit(job, w->path, true, 0, 0);
}

/* Percorre a FST (a raiz diz quantas entradas há) chamando `visit` com "files/caminho" para cada diretório e arquivo.
 */
static DiscExtractResult fst_walk(Job *job, FstVisit visit) {
    const u32 count = disc_be32(job->fst + 8);
    if (count < 1U || (u64)count * 12U > job->fst_size) {
        set_detail(job, "FST com %u entradas", count);
        return DISC_EXTRACT_ERR_TREE;
    }
    FstWalk *w = (FstWalk *)calloc(1, sizeof(*w));
    if (w == NULL) {
        return DISC_EXTRACT_ERR_NOMEM;
    }
    (void)snprintf(w->path, sizeof(w->path), "files");
    DiscExtractResult res = DISC_EXTRACT_OK;
    for (u32 i = 1; i < count && res == DISC_EXTRACT_OK; i++) {
        while (w->depth > 0 && i >= w->end[w->depth - 1U]) {
            w->path[w->prefix[--w->depth]] = '\0';
        }
        res = walk_entry(job, w, visit, i, count);
    }
    free(w);
    return res;
}

static DiscExtractResult visit_count(Job *job, const char *rel, bool is_dir, u64 offset, u32 size) {
    if (is_dir) {
        return DISC_EXTRACT_OK;
    }
    if (offset + size > job->src->size) {
        set_detail(job, "%s fora do disco", rel);
        return DISC_EXTRACT_ERR_CORRUPT;
    }
    job->files_total++;
    job->bytes_total += size;
    return DISC_EXTRACT_OK;
}

/* --- Gravação ---------------------------------------------------------------------------------------------- */

static bool full_path(const Job *job, const char *rel, char *out, size_t out_size) {
    const int n = snprintf(out, out_size, "%s/%s", job->tmp, rel);
    return n > 0 && (size_t)n < out_size;
}

/* Cria um diretório (com os pais que faltarem), anotando os que esta extração criou. */
static DiscExtractResult make_dirs(Job *job, const char *path) {
    char buf[EX_PATH];
    const size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        return DISC_EXTRACT_ERR_WRITE;
    }
    memcpy(buf, path, len + 1U);
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] != '/' && buf[i] != '\\' && buf[i] != '\0') {
            continue;
        }
        const char saved = buf[i];
        buf[i] = '\0';
        if (i > 1U && !platform_fs_exists(buf)) {
            if (platform_fs_mkdir(buf) < 0) {
                set_detail(job, "não criou a pasta %s", buf);
                return DISC_EXTRACT_ERR_WRITE;
            }
            if (!plist_add(&job->dirs, buf)) {
                return DISC_EXTRACT_ERR_NOMEM;
            }
        }
        buf[i] = saved;
    }
    return DISC_EXTRACT_OK;
}

static DiscExtractResult tick(Job *job, const char *current, u64 advance) {
    job->bytes_done += advance;
    if (job->opt.cancel != NULL && *job->opt.cancel != 0) {
        return DISC_EXTRACT_ERR_CANCELLED;
    }
    if (job->opt.progress != NULL) {
        const DiscExtractProgress p = {job->bytes_done, job->bytes_total, job->files_done, job->files_total, current};
        if (!job->opt.progress(&p, job->opt.user)) {
            return DISC_EXTRACT_ERR_CANCELLED;
        }
    }
    return DISC_EXTRACT_OK;
}

static DiscExtractResult write_error(Job *job, const char *rel) {
    const int err = errno;
    set_detail(job, "gravando %s: %s", rel, strerror(err));
    return err == ENOSPC ? DISC_EXTRACT_ERR_DISK_FULL : DISC_EXTRACT_ERR_WRITE;
}

static DiscExtractResult copy_chunks(Job *job, FILE *f, const char *rel, u64 offset, u64 size) {
    u64 done = 0;
    while (done < size) {
        const u32 n = (size - done) < EX_COPY_CHUNK ? (u32)(size - done) : EX_COPY_CHUNK;
        DiscExtractResult res = read_src(job, offset + done, job->buf, n);
        if (res != DISC_EXTRACT_OK) {
            return res;
        }
        if (job->opt.test_write_limit != 0 && job->written + n > job->opt.test_write_limit) {
            errno = ENOSPC;
            return write_error(job, rel);
        }
        if (fwrite(job->buf, 1, n, f) != n) {
            return write_error(job, rel);
        }
        job->written += n;
        done += n;
        res = tick(job, rel, n);
        if (res != DISC_EXTRACT_OK) {
            return res;
        }
    }
    return DISC_EXTRACT_OK;
}

/* Copia [offset, offset+size) do disco para tmp/rel. */
static DiscExtractResult copy_range(Job *job, const char *rel, u64 offset, u64 size) {
    char path[EX_PATH];
    if (!full_path(job, rel, path, sizeof(path))) {
        return DISC_EXTRACT_ERR_WRITE;
    }
    FILE *f = platform_fs_fopen(path, "wb");
    if (f == NULL) {
        return write_error(job, rel);
    }
    if (!plist_add(&job->files, path)) {
        (void)fclose(f);
        return DISC_EXTRACT_ERR_NOMEM;
    }
    DiscExtractResult res = copy_chunks(job, f, rel, offset, size);
    if (fclose(f) != 0 && res == DISC_EXTRACT_OK) {
        res = write_error(job, rel);
    }
    if (res == DISC_EXTRACT_OK) {
        job->files_done++;
        res = tick(job, rel, 0);
    }
    return res;
}

static DiscExtractResult visit_extract(Job *job, const char *rel, bool is_dir, u64 offset, u32 size) {
    char path[EX_PATH];
    if (is_dir) {
        return full_path(job, rel, path, sizeof(path)) ? make_dirs(job, path) : DISC_EXTRACT_ERR_WRITE;
    }
    return copy_range(job, rel, offset, size);
}

static DiscExtractResult extract_sys(Job *job) {
    static const char *const names[] = {"sys/boot.bin", "sys/bi2.bin", "sys/apploader.img", "sys/fst.bin",
                                        "sys/main.dol"};
    const u64 offsets[] = {0, EX_BI2_OFFSET, EX_APPLOADER_OFF, job->fst_offset, job->dol_offset};
    const u64 sizes[] = {EX_BOOT_SIZE, EX_BI2_SIZE, job->apploader_size, job->fst_size, job->dol_size};
    for (size_t i = 0; i < 5; i++) {
        const DiscExtractResult res = copy_range(job, names[i], offsets[i], sizes[i]);
        if (res != DISC_EXTRACT_OK) {
            return res;
        }
    }
    return DISC_EXTRACT_OK;
}

/* --- Pré-condições e commit -------------------------------------------------------------------------------- */

static DiscExtractResult check_dol(Job *job) {
    char hex[SHA256_HEX_SIZE];
    sha256_hex(job->dol, job->dol_size, hex);
    if (strcmp(hex, GAME_MAIN_DOL_SHA256) == 0) {
        return DISC_EXTRACT_OK;
    }
    set_detail(job, "SHA-256 do main.dol: %s", hex);
    job->other_game = true;
    return job->opt.keep_other_game ? DISC_EXTRACT_OK : DISC_EXTRACT_ERR_OTHER_GAME;
}

static DiscExtractResult prepare_dirs(Job *job) {
    char path[EX_PATH];
    DiscExtractResult res = make_dirs(job, job->tmp);
    if (res == DISC_EXTRACT_OK) {
        job->tmp_created = true;
        u64 free_bytes = 0;
        if (platform_fs_free_bytes(job->tmp, &free_bytes) && free_bytes < job->bytes_total + EX_FREE_MARGIN) {
            set_detail(job, "livres %llu, necessários %llu", (unsigned long long)free_bytes,
                       (unsigned long long)job->bytes_total);
            return DISC_EXTRACT_ERR_DISK_FULL;
        }
    }
    static const char *const subdirs[] = {"sys", "files"};
    for (size_t i = 0; i < 2 && res == DISC_EXTRACT_OK; i++) {
        res = full_path(job, subdirs[i], path, sizeof(path)) ? make_dirs(job, path) : DISC_EXTRACT_ERR_WRITE;
    }
    return res;
}

/* Tenta mover a pasta pronta para o destino. Se o destino já existe (pasta vazia), move sys/ e files/ um a um. */
static DiscExtractResult commit(Job *job) {
    if (platform_fs_rename(job->tmp, job->out)) {
        job->committed = true;
        return DISC_EXTRACT_OK;
    }
    if (make_dirs(job, job->out) != DISC_EXTRACT_OK) {
        return DISC_EXTRACT_ERR_WRITE;
    }
    static const char *const subdirs[] = {"sys", "files"};
    char from[EX_PATH];
    char to[EX_PATH];
    for (size_t i = 0; i < 2; i++) {
        (void)snprintf(from, sizeof(from), "%s/%s", job->tmp, subdirs[i]);
        (void)snprintf(to, sizeof(to), "%s/%s", job->out, subdirs[i]);
        if (!platform_fs_rename(from, to)) {
            if (i == 1) { /* desfaz sys/ para o cleanup apagar tudo no mesmo lugar */
                (void)snprintf(from, sizeof(from), "%s/sys", job->tmp);
                (void)snprintf(to, sizeof(to), "%s/sys", job->out);
                (void)platform_fs_rename(to, from);
            }
            set_detail(job, "não moveu %s para %s", job->tmp, job->out);
            return DISC_EXTRACT_ERR_WRITE;
        }
    }
    job->committed = true;
    (void)platform_fs_remove(job->tmp);
    return DISC_EXTRACT_OK;
}

static DiscExtractResult prepare_job(Job *job, const char *image_path, const char *out_dir) {
    size_t len = strlen(out_dir);
    while (len > 1U && (out_dir[len - 1U] == '/' || out_dir[len - 1U] == '\\')) {
        len--;
    }
    if (len == 0 || len + 8U >= sizeof(job->out)) {
        return DISC_EXTRACT_ERR_WRITE;
    }
    memcpy(job->out, out_dir, len);
    (void)snprintf(job->tmp, sizeof(job->tmp), "%s.parte", job->out);
    char probe[EX_PATH];
    (void)snprintf(probe, sizeof(probe), "%s/sys", job->out);
    if (platform_fs_exists(probe)) {
        return DISC_EXTRACT_ERR_EXISTS;
    }
    (void)snprintf(probe, sizeof(probe), "%s/files", job->out);
    if (platform_fs_exists(probe)) {
        return DISC_EXTRACT_ERR_EXISTS;
    }
    const DiscImageResult img = disc_image_open(image_path, &job->src);
    if (img != DISC_IMAGE_OK) {
        return map_image_error(img);
    }
    job->buf = (u8 *)malloc(EX_COPY_CHUNK);
    return job->buf != NULL ? DISC_EXTRACT_OK : DISC_EXTRACT_ERR_NOMEM;
}

static DiscExtractResult run(Job *job, const char *image_path, const char *out_dir) {
    DiscExtractResult res = prepare_job(job, image_path, out_dir);
    if (res == DISC_EXTRACT_OK) {
        res = load_tree(job);
    }
    if (res == DISC_EXTRACT_OK) {
        res = check_dol(job);
    }
    if (res == DISC_EXTRACT_OK) {
        res = fst_walk(job, visit_count);
    }
    if (res != DISC_EXTRACT_OK) {
        return res;
    }
    job->files_total += 5U; /* sys/ */
    job->bytes_total += EX_BOOT_SIZE + EX_BI2_SIZE + job->apploader_size + job->fst_size + job->dol_size;
    res = prepare_dirs(job);
    if (res == DISC_EXTRACT_OK) {
        res = extract_sys(job);
    }
    if (res == DISC_EXTRACT_OK) {
        res = fst_walk(job, visit_extract);
    }
    if (res == DISC_EXTRACT_OK) {
        res = commit(job);
    }
    return res;
}

DiscExtractResult disc_extract_ex(const char *image_path, const char *out_dir, const DiscExtractOptions *options) {
    Job *job = (Job *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return DISC_EXTRACT_ERR_NOMEM;
    }
    if (options != NULL) {
        job->opt = *options;
    }
    if (job->opt.detail != NULL && job->opt.detail_size > 0) {
        job->opt.detail[0] = '\0';
    }
    DiscExtractResult res = run(job, image_path, out_dir);
    if (!job->committed) {
        job_cleanup(job);
    } else if (job->other_game) {
        res = DISC_EXTRACT_ERR_OTHER_GAME; /* keep_other_game: a árvore ficou em out_dir */
    }
    disc_image_close(job->src);
    plist_free(&job->files);
    plist_free(&job->dirs);
    free(job->fst);
    free(job->dol);
    free(job->buf);
    free(job);
    return res;
}

DiscExtractResult disc_extract(const char *image_path, const char *out_dir, DiscExtractProgressFn progress,
                               void *user) {
    DiscExtractOptions options;
    memset(&options, 0, sizeof(options));
    options.progress = progress;
    options.user = user;
    return disc_extract_ex(image_path, out_dir, &options);
}
