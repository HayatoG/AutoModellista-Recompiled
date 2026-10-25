/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SHA-256 (FIPS 180-4), para conferir o main.dol escolhido na tela inicial (M9 parte 5). */
#ifndef AM_SHA256_H
#define AM_SHA256_H

#include <stddef.h>

#include "am/types.h"

#define SHA256_DIGEST_SIZE 32U
#define SHA256_HEX_SIZE    65U /* 64 dígitos e o '\0' */

typedef struct Sha256 {
    u32 state[8];
    u8 block[64];
    u64 length; /* bytes já recebidos */
    u32 used;   /* bytes em `block` */
} Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const u8 *data, size_t size);
void sha256_final(Sha256 *s, u8 digest[SHA256_DIGEST_SIZE]);
/* Atalho: hash de um bloco inteiro, em hexadecimal minúsculo. */
void sha256_hex(const u8 *data, size_t size, char out[SHA256_HEX_SIZE]);

#endif /* AM_SHA256_H */
