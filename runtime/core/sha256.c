/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FIPS 180-4, §5.3.3 (valores iniciais), §4.2.2 (constantes) e §6.2.2 (compressão). */
#include "am/sha256.h"

#include <stdio.h>
#include <string.h>

static const u32 s_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static u32 rotr(u32 x, u32 n) {
    return (x >> n) | (x << (32U - n));
}

static void compress(u32 state[8], const u8 block[64]) {
    u32 w[64];
    for (u32 i = 0; i < 16U; i++) {
        w[i] = ((u32)block[i * 4U] << 24) | ((u32)block[(i * 4U) + 1U] << 16) | ((u32)block[(i * 4U) + 2U] << 8) |
               (u32)block[(i * 4U) + 3U];
    }
    for (u32 i = 16; i < 64U; i++) {
        const u32 s0 = rotr(w[i - 15U], 7) ^ rotr(w[i - 15U], 18) ^ (w[i - 15U] >> 3);
        const u32 s1 = rotr(w[i - 2U], 17) ^ rotr(w[i - 2U], 19) ^ (w[i - 2U] >> 10);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }
    u32 v[8];
    memcpy(v, state, sizeof(v));
    for (u32 i = 0; i < 64U; i++) {
        const u32 s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
        const u32 ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const u32 t1 = v[7] + s1 + ch + s_k[i] + w[i];
        const u32 s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
        const u32 maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        memmove(&v[1], &v[0], 7U * sizeof(u32));
        v[4] += t1;
        v[0] = t1 + s0 + maj;
    }
    for (u32 i = 0; i < 8U; i++) {
        state[i] += v[i];
    }
}

void sha256_init(Sha256 *s) {
    static const u32 h0[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                              0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    memset(s, 0, sizeof(*s));
    memcpy(s->state, h0, sizeof(h0));
}

void sha256_update(Sha256 *s, const u8 *data, size_t size) {
    s->length += size;
    while (size != 0U) {
        const size_t take = size < 64U - s->used ? size : 64U - s->used;
        memcpy(s->block + s->used, data, take);
        s->used += (u32)take;
        data += take;
        size -= take;
        if (s->used == 64U) {
            compress(s->state, s->block);
            s->used = 0;
        }
    }
}

void sha256_final(Sha256 *s, u8 digest[SHA256_DIGEST_SIZE]) {
    const u64 bits = s->length * 8U;
    const u8 one = 0x80U;
    const u8 zero = 0;
    sha256_update(s, &one, 1);
    while (s->used != 56U) {
        sha256_update(s, &zero, 1);
    }
    u8 len[8];
    for (u32 i = 0; i < 8U; i++) {
        len[i] = (u8)(bits >> (56U - (i * 8U)));
    }
    sha256_update(s, len, sizeof(len));
    for (u32 i = 0; i < 8U; i++) {
        digest[i * 4U] = (u8)(s->state[i] >> 24);
        digest[(i * 4U) + 1U] = (u8)(s->state[i] >> 16);
        digest[(i * 4U) + 2U] = (u8)(s->state[i] >> 8);
        digest[(i * 4U) + 3U] = (u8)s->state[i];
    }
}

void sha256_hex(const u8 *data, size_t size, char out[SHA256_HEX_SIZE]) {
    Sha256 s;
    u8 digest[SHA256_DIGEST_SIZE];
    sha256_init(&s);
    sha256_update(&s, data, size);
    sha256_final(&s, digest);
    for (u32 i = 0; i < SHA256_DIGEST_SIZE; i++) {
        (void)snprintf(out + (i * 2U), 3, "%02x", digest[i]);
    }
}
