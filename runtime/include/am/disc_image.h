/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fonte de bytes de uma imagem de disco GameCube (M9 parte 5, extração): a mesma interface para ISO/GCM crus e para
 * RVZ (formato do Dolphin). Quem lê (core/disc_extract.c) só vê "leia n bytes no deslocamento do disco".
 *
 * RVZ: docs do Dolphin (docs/WiaAndRvz.md; Source/Core/DiscIO/WIABlob.cpp, WIACompression.cpp,
 * LaggedFibonacciGenerator.cpp). Suportados: compressão Zstandard e sem compressão, blocos de qualquer tamanho
 * válido, grupos "empacotados" (segmentos de dados e de lixo). Recusados com erro próprio: LZMA, LZMA2, bzip2
 * (ERR_COMPRESSION) e WIA (ERR_WIA). Sem partições Wii, criptografia ou hashes de grupo. Os SHA-1 do cabeçalho não
 * são conferidos; o tamanho do arquivo é (um RVZ truncado dá ERR_CORRUPT). */
#ifndef AM_DISC_IMAGE_H
#define AM_DISC_IMAGE_H

#include <stdio.h>

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DiscImageResult {
    DISC_IMAGE_OK,
    DISC_IMAGE_ERR_OPEN,         /* não abriu o arquivo */
    DISC_IMAGE_ERR_FORMAT,       /* nem ISO/GCM, nem RVZ (ou arquivo grande demais) */
    DISC_IMAGE_ERR_WIA,          /* WIA (irmão antigo do RVZ): não suportado */
    DISC_IMAGE_ERR_COMPRESSION,  /* RVZ com LZMA, LZMA2 ou bzip2 */
    DISC_IMAGE_ERR_NOT_GAMECUBE, /* disco do Wii (ou RVZ de Wii) */
    DISC_IMAGE_ERR_CORRUPT,      /* RVZ truncado, tabela ou grupo inconsistente */
    DISC_IMAGE_ERR_RANGE,        /* leitura além do fim do disco */
    DISC_IMAGE_ERR_IO,           /* erro de leitura do arquivo */
    DISC_IMAGE_ERR_NOMEM,
} DiscImageResult;

const char *disc_image_result_name(DiscImageResult result);

typedef struct DiscSource DiscSource;
struct DiscSource {
    u64 size; /* tamanho do disco (do ISO equivalente) */
    DiscImageResult (*read)(DiscSource *src, u64 offset, u8 *dst, u32 size);
    void (*destroy)(DiscSource *src);
};

/* Abre ISO/GCM ou RVZ, reconhecidos pelo conteúdo (não pela extensão). Não usa estado global: pode ser chamada de
 * qualquer thread, uma fonte por thread. */
DiscImageResult disc_image_open(const char *path, DiscSource **out);
void disc_image_close(DiscSource *src);
/* Lê [offset, offset+size) do disco. */
DiscImageResult disc_image_read(DiscSource *src, u64 offset, u8 *dst, u32 size);

/* --- Internos (disc_rvz.c e testes) --- */

/* Assume o FILE (fecha no destroy, e também se falhar). `file_size` é o tamanho do arquivo RVZ. */
DiscImageResult disc_rvz_open(FILE *file, u64 file_size, DiscSource **out);
/* Gerador de lixo do disco (Lagged Fibonacci do Dolphin): `seed` são os 68 bytes gravados no RVZ; gera `size` bytes
 * a partir de `offset_in_sector` (deslocamento dentro do setor de 0x8000 bytes). */
void disc_rvz_junk(const u8 seed[68], u32 offset_in_sector, u8 *dst, u32 size);

static inline u32 disc_be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static inline u64 disc_be64(const u8 *p) {
    return ((u64)disc_be32(p) << 32) | (u64)disc_be32(p + 4);
}

#ifdef __cplusplus
}
#endif

#endif /* AM_DISC_IMAGE_H */
