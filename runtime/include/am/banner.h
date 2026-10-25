/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Arte da tela inicial (M9 parte 5): o banner do disco (files/opening.bnr, o mesmo que o menu do GameCube mostra),
 * lido da cópia do jogador em tempo de execução e decodificado pelo decodificador de texturas do GX. Nada do jogo é
 * embutido no executável. Formato: YAGCD 14 ("BNR1"/"BNR2"): 96x32 texels RGB5A3 em blocos de 4x4 a partir de 0x20,
 * seguidos dos textos (nome do jogo, fabricante, descrição). */
#ifndef AM_BANNER_H
#define AM_BANNER_H

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BANNER_WIDTH       96U
#define BANNER_HEIGHT      32U
#define BANNER_IMAGE_SIZE  (BANNER_WIDTH * BANNER_HEIGHT * 2U)
#define BANNER_IMAGE_START 0x20U
#define BANNER_TEXT_START  (BANNER_IMAGE_START + BANNER_IMAGE_SIZE) /* 0x1820: nome curto (32 bytes) */

typedef struct Banner {
    u8 rgba[BANNER_WIDTH * BANNER_HEIGHT * 4U];
    char name[65]; /* nome longo do jogo (64 bytes no banner), sem os espaços das pontas */
} Banner;

/* Decodifica um banner já na memória. false = não é um banner. */
bool banner_decode(const u8 *data, size_t size, Banner *out);
/* <raiz>/files/opening.bnr. */
bool banner_load(const char *disc_root, Banner *out);

#ifdef __cplusplus
}
#endif

#endif /* AM_BANNER_H */
