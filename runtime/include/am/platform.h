/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Camada de plataforma (SDL3). Único lugar com código de SO; só depende de am/types.h. */
#ifndef AM_PLATFORM_H
#define AM_PLATFORM_H

#include "am/types.h"

/* No M3 só inicializa a SDL (sem subsistemas e sem janela). Em falha, platform_last_error() diz o motivo. */
bool platform_init(void);
void platform_shutdown(void);
const char *platform_last_error(void);
/* Lê o arquivo inteiro. Devolve NULL (e platform_last_error) em falha; liberar com platform_free_file. */
u8 *platform_load_file(const char *path, size_t *size);
void platform_free_file(u8 *data);

/* Cria o diretório (e os pais que faltarem); true se já existe. */
bool platform_make_dir(const char *path);

/* Milissegundos de relógio monotônico desde platform_init. */
u64 platform_ticks_ms(void);

/* Versão da SDL em uso, no formato major*1000000 + minor*1000 + patch. */
int platform_sdl_version(void);

#endif /* AM_PLATFORM_H */
