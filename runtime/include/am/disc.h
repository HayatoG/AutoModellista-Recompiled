/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Imagem de disco montada a partir da árvore extraída pelo `dtk disc extract` (PLANO_PORT_C.md §2.1: o port não lê
 * RVZ/ISO). O deslocamento de cada arquivo no disco vem do FST; a área de sistema vem de sys/:
 *
 *   0x00000000  boot.bin (0x440)        cabeçalho; 0x420 = DOL, 0x424/0x428 = FST e tamanho
 *   0x00000440  bi2.bin (0x2000)
 *   0x00002440  apploader.img
 *   boot[0x420] main.dol
 *   boot[0x424] fst.bin
 *
 * FST (YAGCD 13.4; Dolphin DiscIO/FileSystemGCWii.cpp): entradas de 12 bytes (flag de diretório + deslocamento do
 * nome em 24 bits, deslocamento no disco ou pai, tamanho ou próximo índice), a raiz diz quantas entradas há, e a
 * tabela de nomes vem logo depois. Bytes do disco fora de qualquer arquivo (enchimento entre arquivos) são lidos como
 * zero e contados: o disco original tem lixo pseudoaleatório ali, que nenhum jogo deveria ler. */
#ifndef AM_DISC_H
#define AM_DISC_H

#include "am/types.h"

typedef enum DiscResult {
    DISC_OK,
    DISC_ERR_SYS,     /* falta arquivo de sys/ ou boot.bin curto */
    DISC_ERR_FST,     /* FST malformado */
    DISC_ERR_FILE,    /* arquivo do FST ausente ou de tamanho diferente em files/ */
    DISC_ERR_OVERLAP, /* duas regiões ocupam o mesmo deslocamento */
    DISC_ERR_NOMEM,
} DiscResult;

const char *disc_result_name(DiscResult result);

/* `root` contém sys/ e files/. Fecha a imagem anterior. */
DiscResult disc_open(const char *root);
void disc_close(void);
bool disc_is_open(void);

/* Lê [offset, offset+size) do disco. false só em erro de E/S do host (arquivo sumiu). */
bool disc_read(u64 offset, u8 *dst, u32 size);

/* Região (arquivo) que contém `offset`: caminho relativo a `root` ("files/module/game.rel") e deslocamento dentro
 * dele. NULL se cair em enchimento. Para log. */
const char *disc_region_at(u64 offset, u64 *offset_in_file);

typedef struct DiscStats {
    u32 regions;
    u64 reads;
    u64 bytes;
    u64 padding_bytes; /* lidos fora de qualquer arquivo */
} DiscStats;

const DiscStats *disc_stats(void);

#endif /* AM_DISC_H */
