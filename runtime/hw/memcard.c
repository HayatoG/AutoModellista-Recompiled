/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/memcard.h"

#include <stdlib.h>
#include <string.h>

#include "am/log.h"

#define MC_FST_BLOCKS      5U /* GCMemcard.h: MC_FST_BLOCKS */
#define MC_BLOCKS_PER_MBIT 16U
#define MC_LCG_MUL         0x41C64E6DULL
#define MC_LCG_ADD         0x3039ULL

static struct {
    u8 *data;
    size_t size;
    bool dirty;
} s_card;

static void put16(u8 *p, u32 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

static void put32(u8 *p, u32 v) {
    put16(p, v >> 16);
    put16(p + 2, v & 0xFFFFU);
}

/* GCMemcard.cpp: CalculateMemcardChecksums (soma de palavras big-endian e soma das invertidas; 0xFFFF vira 0). */
static void checksums(const u8 *data, size_t size, u8 *out_sum, u8 *out_inv) {
    u16 sum = 0;
    u16 inv = 0;
    for (size_t i = 0; i < size; i += 2U) {
        const u16 d = (u16)((data[i] << 8) | data[i + 1U]);
        sum = (u16)(sum + d);
        inv = (u16)(inv + (u16)(d ^ 0xFFFFU));
    }
    put16(out_sum, sum == 0xFFFFU ? 0U : sum);
    put16(out_inv, inv == 0xFFFFU ? 0U : inv);
}

bool memcard_size_valid(size_t bytes) {
    for (u32 mbit = 4U; mbit <= 128U; mbit *= 2U) {
        if (bytes == (size_t)mbit * MEMCARD_MBIT_BYTES) {
            return true;
        }
    }
    return false;
}

/* InitializeHeaderData: serial[i] = flash_id[i] + (u8)rand, com o LCG do SDK (constantes fixas do GC SDK). */
static void format_header(u8 *blk, u32 mbit, const u8 *flash_id, u64 format_time) {
    memset(blk, 0xFF, MEMCARD_BLOCK_SIZE);
    u64 rand = format_time;
    for (u32 i = 0; i < MEMCARD_FLASH_ID_LEN; i++) {
        rand = ((rand * MC_LCG_MUL) + MC_LCG_ADD) >> 16;
        blk[i] = (u8)(flash_id[i] + (u8)rand);
        rand = ((rand * MC_LCG_MUL) + MC_LCG_ADD) >> 16;
        rand &= 0x7FFFULL;
    }
    put32(blk + 0x0C, (u32)(format_time >> 32));
    put32(blk + 0x10, (u32)format_time);
    put32(blk + 0x14, 0); /* SRAM bias */
    put32(blk + 0x18, 0); /* idioma da SRAM: inglês */
    put32(blk + 0x1C, 0); /* DTV status */
    put16(blk + 0x20, 0); /* dispositivo: slot A */
    put16(blk + 0x22, mbit);
    put16(blk + 0x24, 0); /* Windows-1252 */
    checksums(blk, 0x1FCU, blk + 0x1FC, blk + 0x1FE);
}

static void format_directory(u8 *blk) {
    memset(blk, 0xFF, MEMCARD_BLOCK_SIZE);
    put16(blk + 0x1FFA, 0); /* update counter */
    checksums(blk, 0x1FFCU, blk + 0x1FFC, blk + 0x1FFE);
}

/* BlockAlloc::BlockAlloc: mapa zerado, blocos livres = mbit * 16 - 5, último alocado = 4. */
static void format_bat(u8 *blk, u32 mbit) {
    memset(blk, 0, MEMCARD_BLOCK_SIZE);
    put16(blk + 6, mbit * MC_BLOCKS_PER_MBIT - MC_FST_BLOCKS);
    put16(blk + 8, 4U);
    checksums(blk + 4, MEMCARD_BLOCK_SIZE - 4U, blk, blk + 2);
}

void memcard_format(u8 *data, u32 mbit, const u8 flash_id[MEMCARD_FLASH_ID_LEN], u64 format_time) {
    const size_t size = (size_t)mbit * MEMCARD_MBIT_BYTES;
    memset(data, 0xFF, size);
    format_header(data, mbit, flash_id, format_time);
    format_directory(data + (size_t)MEMCARD_BLOCK_SIZE * 1U);
    memcpy(data + (size_t)MEMCARD_BLOCK_SIZE * 2U, data + (size_t)MEMCARD_BLOCK_SIZE * 1U, MEMCARD_BLOCK_SIZE);
    format_bat(data + (size_t)MEMCARD_BLOCK_SIZE * 3U, mbit);
    memcpy(data + (size_t)MEMCARD_BLOCK_SIZE * 4U, data + (size_t)MEMCARD_BLOCK_SIZE * 3U, MEMCARD_BLOCK_SIZE);
}

void memcard_eject(void) {
    free(s_card.data);
    s_card.data = NULL;
    s_card.size = 0;
    s_card.dirty = false;
}

bool memcard_create(u32 mbit, u64 format_time) {
    memcard_eject();
    const size_t size = (size_t)mbit * MEMCARD_MBIT_BYTES;
    if (!memcard_size_valid(size)) {
        return false;
    }
    s_card.data = malloc(size);
    if (s_card.data == NULL) {
        return false;
    }
    s_card.size = size;
    memcard_format(s_card.data, mbit, (const u8 *)"DOLPHINSLOTA", format_time);
    s_card.dirty = true; /* cartão novo: o arquivo ainda não existe */
    return true;
}

bool memcard_load(const u8 *bytes, size_t size) {
    memcard_eject();
    if (!memcard_size_valid(size)) {
        return false;
    }
    s_card.data = malloc(size);
    if (s_card.data == NULL) {
        return false;
    }
    memcpy(s_card.data, bytes, size);
    s_card.size = size;
    return true;
}

bool memcard_present(void) {
    return s_card.data != NULL;
}

u32 memcard_mbit(void) {
    return (u32)(s_card.size / MEMCARD_MBIT_BYTES);
}

const u8 *memcard_image(size_t *size) {
    *size = s_card.size;
    return s_card.data;
}

bool memcard_take_dirty(void) {
    const bool d = s_card.dirty;
    s_card.dirty = false;
    return d;
}

static void check_range(const char *what, u32 address, u32 length) {
    if ((u64)address + length > s_card.size) {
        am_fatal("memory card: %s fora do cartão (endereço 0x%X, %u bytes, cartão de %zu bytes)", what, address, length,
                 s_card.size);
    }
}

void memcard_read(u32 address, u32 length, u8 *dest) {
    check_range("leitura", address, length);
    memcpy(dest, s_card.data + address, length);
}

void memcard_write(u32 address, u32 length, const u8 *src) {
    check_range("escrita", address, length);
    memcpy(s_card.data + address, src, length);
    s_card.dirty = true;
}

void memcard_clear_block(u32 address) {
    if ((address & (MEMCARD_BLOCK_SIZE - 1U)) != 0U) {
        am_fatal("memory card: apagar bloco em endereço 0x%X não alinhado", address);
    }
    check_range("apagar bloco", address, MEMCARD_BLOCK_SIZE);
    memset(s_card.data + address, 0xFF, MEMCARD_BLOCK_SIZE);
    s_card.dirty = true;
}

void memcard_clear_all(void) {
    memset(s_card.data, 0xFF, s_card.size);
    s_card.dirty = true;
}
