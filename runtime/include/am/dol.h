/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Executável DOL do GameCube: leitura do cabeçalho e carga na MEM1.
 *
 * Formato (YAGCD 14.2 "DOL format"; Dolphin Core/Boot/DolReader.cpp): cabeçalho de 0x100 bytes big-endian com
 * 7 seções de texto + 11 de dados, cada uma com deslocamento no arquivo (0x00), endereço de carga (0x48) e tamanho
 * (0x90); depois endereço e tamanho do .bss (0xD8, 0xDC) e entry point (0xE0). Seção de tamanho 0 = ausente. */
#ifndef AM_DOL_H
#define AM_DOL_H

#include "am/types.h"

#define DOL_TEXT_COUNT    7U
#define DOL_DATA_COUNT    11U
#define DOL_SECTION_COUNT (DOL_TEXT_COUNT + DOL_DATA_COUNT)
#define DOL_HEADER_SIZE   0x100U

typedef struct DolSection {
    u32 offset; /* no arquivo */
    GuestAddr addr;
    u32 size;
} DolSection;

/* sections[0..6] = texto, sections[7..17] = dados, na ordem do cabeçalho. */
typedef struct DolImage {
    DolSection sections[DOL_SECTION_COUNT];
    GuestAddr bss_addr;
    u32 bss_size;
    GuestAddr entry;
} DolImage;

typedef enum DolResult {
    DOL_OK,
    DOL_ERR_TRUNCATED,       /* arquivo menor que o cabeçalho */
    DOL_ERR_SECTION_FILE,    /* seção aponta para fora do arquivo */
    DOL_ERR_SECTION_MEM1,    /* seção ou .bss fora da MEM1 */
    DOL_ERR_SECTION_ALIGN,   /* endereço ou tamanho de seção não alinhado a 4 */
    DOL_ERR_SECTION_OVERLAP, /* duas seções carregadas no mesmo endereço */
    DOL_ERR_ENTRY,           /* entry point fora de toda seção de texto */
} DolResult;

const char *dol_result_name(DolResult result);

/* Valida o cabeçalho de `data` e preenche `out`. Não toca na memória do guest. */
DolResult dol_parse(const u8 *data, size_t size, DolImage *out);

/* Zera o intervalo do .bss e copia as seções para a MEM1 (exige mem_init). As seções são copiadas DEPOIS do zero:
 * no layout do CodeWarrior o .bss do cabeçalho cobre também .sdata/.sdata2 (seções de dados 4 e 5 deste jogo), e o
 * apploader nunca zera nada (quem zera o .bss é __init_data em __start). Zerar só os buracos dá o mesmo resultado
 * que a memória limpa do console. */
DolResult dol_load(const DolImage *image, const u8 *data, size_t size);

/* Índice da seção de texto que contém `addr`, ou -1. */
int dol_text_section_of(const DolImage *image, GuestAddr addr);

#endif /* AM_DOL_H */
