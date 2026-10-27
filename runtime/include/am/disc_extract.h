/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Extração da imagem do jogo (ISO, GCM ou RVZ) para a árvore que o runtime lê (M9 parte 5, docs/M9_PARTE5_LAUNCHER.md
 * §6): o mesmo formato do `dtk disc extract`.
 *
 *   <out_dir>/sys/boot.bin bi2.bin apploader.img fst.bin main.dol
 *   <out_dir>/files/<caminho da FST>
 *
 * Os 0x440 primeiros bytes do disco são boot.bin; bi2.bin fica em 0x440 (0x2000 bytes); o apploader em 0x2440 com
 * 0x20 + tamanho + trailer (cabeçalho do apploader: +0x14 e +0x18); fst.bin e main.dol pelos deslocamentos de
 * boot.bin (0x424/0x428 e 0x420); o tamanho do DOL é o maior (offset + tamanho) das seções do cabeçalho dele. O
 * espaço fora dos arquivos (lixo) não é gravado: o runtime lê esse espaço como zero (core/disc.c).
 *
 * Segurança: grava em "<out_dir>.parte" e só no fim renomeia para out_dir, de modo que nunca sobra uma árvore pela
 * metade (em qualquer erro, ou ao cancelar, o que foi criado é apagado). Não usa estado global: pode rodar em outra
 * thread (uma extração por vez por out_dir). */
#ifndef AM_DISC_EXTRACT_H
#define AM_DISC_EXTRACT_H

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DiscExtractResult {
    DISC_EXTRACT_OK,
    DISC_EXTRACT_ERR_OPEN,         /* não abriu a imagem */
    DISC_EXTRACT_ERR_FORMAT,       /* nem ISO/GCM nem RVZ */
    DISC_EXTRACT_ERR_WIA,          /* WIA: reconverter para RVZ ou ISO */
    DISC_EXTRACT_ERR_COMPRESSION,  /* RVZ com LZMA/LZMA2/bzip2: reconverter com zstd ou usar ISO */
    DISC_EXTRACT_ERR_NOT_GAMECUBE, /* disco do Wii */
    DISC_EXTRACT_ERR_CORRUPT,      /* imagem truncada ou corrompida */
    DISC_EXTRACT_ERR_TREE,         /* cabeçalho, FST ou DOL do disco inválidos */
    DISC_EXTRACT_ERR_EXISTS,       /* out_dir/sys já existe: não sobrescreve */
    DISC_EXTRACT_ERR_DISK_FULL,
    DISC_EXTRACT_ERR_WRITE, /* outro erro ao gravar */
    DISC_EXTRACT_ERR_CANCELLED,
    DISC_EXTRACT_ERR_OTHER_GAME, /* main.dol com SHA-256 diferente de GAME_MAIN_DOL_SHA256 */
    DISC_EXTRACT_ERR_NOMEM,
} DiscExtractResult;

/* Mensagem em português para mostrar ao jogador (sem `\n`). */
const char *disc_extract_result_text(DiscExtractResult result);

typedef struct DiscExtractProgress {
    u64 bytes_done;
    u64 bytes_total;
    u32 files_done;
    u32 files_total;
    const char *current; /* "sys/main.dol", "files/afs00.afs"... válido só durante a chamada */
} DiscExtractProgress;

/* Chamada a cada ~1 MiB e a cada arquivo, na thread que extrai. Devolver false cancela. */
typedef bool (*DiscExtractProgressFn)(const DiscExtractProgress *progress, void *user);

typedef struct DiscExtractOptions {
    DiscExtractProgressFn progress; /* pode ser NULL */
    void *user;
    /* Cancelamento por outra thread: ao ficar diferente de zero a extração para na próxima 1 MiB. Escreva com um
     * armazenamento atômico (C11 atomic_store / std::atomic<int>) de um objeto int; pode ser NULL. */
    const volatile int *cancel;
    /* main.dol de outro jogo/versão: false = recusa antes de gravar qualquer coisa (ERR_OTHER_GAME); true = extrai
     * assim mesmo e devolve ERR_OTHER_GAME com a árvore completa já em out_dir. */
    bool keep_other_game;
    /* Detalhe do erro (nome do arquivo, causa) para log; pode ser NULL. */
    char *detail;
    size_t detail_size;
    /* Só para teste: simula disco cheio depois deste número de bytes gravados (0 = sem limite). */
    u64 test_write_limit;
} DiscExtractOptions;

DiscExtractResult disc_extract_ex(const char *image_path, const char *out_dir, const DiscExtractOptions *options);

/* Atalho sem as opções extras. */
DiscExtractResult disc_extract(const char *image_path, const char *out_dir, DiscExtractProgressFn progress, void *user);

#ifdef __cplusplus
}
#endif

#endif /* AM_DISC_EXTRACT_H */
