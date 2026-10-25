/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pasta do jogo escolhida pelo jogador (M9 parte 5): a árvore extraída do disco (dtk disc extract ou "Extract Entire
 * Disc" do Dolphin), com sys/main.dol conferido pelo SHA-256 contra o GAUE08 rev 0 de que o C foi gerado. */
#ifndef AM_GAMEDIR_H
#define AM_GAMEDIR_H

#include "am/sha256.h"
#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GameDirResult {
    GAMEDIR_OK,
    GAMEDIR_NO_DOL,   /* nem <pasta>/sys/main.dol nem <pasta>/main.dol */
    GAMEDIR_NO_FILES, /* falta a files/ ao lado da sys/ */
    GAMEDIR_BAD_HASH, /* main.dol de outra versão (ou de outro jogo) */
} GameDirResult;

/* Aceita a raiz (com sys/ e files/) ou a própria sys/. Em root_out, a raiz; em hash_out, o SHA-256 do main.dol
 * (vazio se não leu). */
GameDirResult gamedir_check(const char *dir, char *root_out, size_t root_size, char hash_out[SHA256_HEX_SIZE]);
const char *gamedir_result_text(GameDirResult r);

#ifdef __cplusplus
}
#endif

#endif /* AM_GAMEDIR_H */
