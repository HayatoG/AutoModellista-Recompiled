/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Imagem do memory card do slot A (M8 parte 2b): o conteúdo de um arquivo .raw do Dolphin 2609a (Core/HW/GCMemcard/
 * GCMemcardRaw.cpp: MemoryCard; GCMemcard.cpp: Format). Sem E/S de arquivo aqui: quem carrega e grava é o executável
 * (platform_load_file / platform_save_file); este módulo só guarda os bytes, formata cartão novo e marca "sujo". */
#ifndef AM_MEMCARD_H
#define AM_MEMCARD_H

#include "am/types.h"

#define MEMCARD_BLOCK_SIZE   0x2000U  /* GCMemcard.h: BLOCK_SIZE */
#define MEMCARD_MBIT_BYTES   0x20000U /* EXI_DeviceMemoryCard.cpp: SIZE_TO_Mb = 1024 * 8 * 16 */
#define MEMCARD_DEFAULT_MBIT 4U       /* cartão de 59 blocos (4 Mbit, 512 KiB) */
#define MEMCARD_FLASH_ID_LEN 12U
#define MEMCARD_HEADER_BYTES 20U /* o que CEXIMemoryCard lê para SetCardFlashID */

/* Tamanhos aceitos: potência de dois de 4 a 128 Mbit (os cartões de 59 a 2043 blocos). */
bool memcard_size_valid(size_t bytes);

/* Formata `data` (já com `mbit` * MEMCARD_MBIT_BYTES bytes) como GCMemcard::Format: cabeçalho, 2 diretórios e 2 BATs
 * nos 5 primeiros blocos, 0xFF no resto. Codificação Windows-1252, idioma inglês, rtc_bias 0, slot A. */
void memcard_format(u8 *data, u32 mbit, const u8 flash_id[MEMCARD_FLASH_ID_LEN], u64 format_time);

/* Cartão novo e formatado, com o flash ID padrão da SRAM do Dolphin ("DOLPHINSLOTA"); `format_time` em segundos desde
 * 2000-01-01 (o Dolphin usa o relógio do host; aqui vem do RTC emulado para a execução ser determinística). */
bool memcard_create(u32 mbit, u64 format_time);
/* Cartão já existente (conteúdo de um .raw). Copia os bytes. false se o tamanho não for de um cartão. */
bool memcard_load(const u8 *bytes, size_t size);
void memcard_eject(void);

bool memcard_present(void);
u32 memcard_mbit(void);
const u8 *memcard_image(size_t *size);
/* Dolphin: MemoryCard::MakeDirty/TestAndClear. true se houve escrita desde a última chamada. */
bool memcard_take_dirty(void);

/* Dolphin: Read/Write/ClearBlock/ClearAll sobre a imagem. Fora do limite é am_fatal (o Dolphin dá PanicAlert). */
void memcard_read(u32 address, u32 length, u8 *dest);
void memcard_write(u32 address, u32 length, const u8 *src);
void memcard_clear_block(u32 address);
void memcard_clear_all(void);

#endif /* AM_MEMCARD_H */
