/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Arquivos e pastas com caminhos em UTF-8 e deslocamentos de 64 bits, para a extração da imagem do jogo
 * (core/disc_extract.c). Só libc e a API do SO: não usa a SDL, para compilar sozinho (tools/disc_extract). Fica à
 * parte de platform.h, que cuida da janela e do usuário. Seguro entre threads (sem estado). */
#ifndef AM_PLATFORM_FS_H
#define AM_PLATFORM_FS_H

#include <stdio.h>

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* fopen com caminho UTF-8 (no Windows, _wfopen). */
FILE *platform_fs_fopen(const char *path, const char *mode);
/* fseek/ftell de 64 bits desde o início do arquivo. */
bool platform_fs_seek(FILE *file, u64 offset);
bool platform_fs_size(FILE *file, u64 *size);

/* O arquivo ou diretório existe. */
bool platform_fs_exists(const char *path);
/* Cria UM diretório (o pai tem de existir). 1 = criou, 0 = já existia como diretório, -1 = falhou. */
int platform_fs_mkdir(const char *path);
/* Apaga um arquivo ou um diretório vazio. */
bool platform_fs_remove(const char *path);
/* Renomeia arquivo ou diretório; falha se `to` já existe (no Windows, um diretório vazio também conta). */
bool platform_fs_rename(const char *from, const char *to);
/* Espaço livre no volume que contém `path` (que deve existir). */
bool platform_fs_free_bytes(const char *path, u64 *bytes);

#ifdef __cplusplus
}
#endif

#endif /* AM_PLATFORM_FS_H */
