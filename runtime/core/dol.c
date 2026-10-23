/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/dol.h"

#include <string.h>

#include "am/mem.h"

/* Deslocamentos do cabeçalho (YAGCD 14.2). */
#define DOL_OFF_OFFSETS  0x00U
#define DOL_OFF_ADDRS    0x48U
#define DOL_OFF_SIZES    0x90U
#define DOL_OFF_BSS_ADDR 0xD8U
#define DOL_OFF_BSS_SIZE 0xDCU
#define DOL_OFF_ENTRY    0xE0U

/* Leitura de um buffer do host (o arquivo), não da memória do guest. */
static u32 be32_at(const u8 *p, u32 off) {
    return ((u32)p[off] << 24) | ((u32)p[off + 1U] << 16) | ((u32)p[off + 2U] << 8) | (u32)p[off + 3U];
}

const char *dol_result_name(DolResult result) {
    switch (result) {
    case DOL_OK:
        return "ok";
    case DOL_ERR_TRUNCATED:
        return "arquivo menor que o cabeçalho de 0x100 bytes";
    case DOL_ERR_SECTION_FILE:
        return "seção fora do arquivo";
    case DOL_ERR_SECTION_MEM1:
        return "seção ou .bss fora da MEM1";
    case DOL_ERR_SECTION_ALIGN:
        return "seção desalinhada";
    case DOL_ERR_SECTION_OVERLAP:
        return "seções sobrepostas";
    case DOL_ERR_ENTRY:
        return "entry point fora das seções de texto";
    }
    return "?";
}

/* [addr, addr+size) cabe na MEM1 vista em 0x80000000. */
static bool fits_mem1(GuestAddr addr, u32 size) {
    if (addr < MEM_CACHED_BASE) {
        return false;
    }
    const u32 off = addr - MEM_CACHED_BASE;
    return off <= MEM_MEM1_SIZE && size <= MEM_MEM1_SIZE - off;
}

static bool overlaps(const DolSection *a, const DolSection *b) {
    return a->size != 0U && b->size != 0U && a->addr < b->addr + b->size && b->addr < a->addr + a->size;
}

static DolResult check_sections(const DolImage *img, size_t size) {
    for (u32 i = 0; i < DOL_SECTION_COUNT; i++) {
        const DolSection *s = &img->sections[i];
        if (s->size == 0U) {
            continue;
        }
        if ((s->addr & 3U) != 0U || (s->size & 3U) != 0U) {
            return DOL_ERR_SECTION_ALIGN;
        }
        if (s->offset > size || s->size > size - s->offset) {
            return DOL_ERR_SECTION_FILE;
        }
        if (!fits_mem1(s->addr, s->size)) {
            return DOL_ERR_SECTION_MEM1;
        }
        for (u32 j = 0; j < i; j++) {
            if (overlaps(s, &img->sections[j])) {
                return DOL_ERR_SECTION_OVERLAP;
            }
        }
    }
    return DOL_OK;
}

DolResult dol_parse(const u8 *data, size_t size, DolImage *out) {
    if (size < DOL_HEADER_SIZE) {
        return DOL_ERR_TRUNCATED;
    }
    memset(out, 0, sizeof(*out));
    for (u32 i = 0; i < DOL_SECTION_COUNT; i++) {
        out->sections[i].offset = be32_at(data, DOL_OFF_OFFSETS + (i * 4U));
        out->sections[i].addr = be32_at(data, DOL_OFF_ADDRS + (i * 4U));
        out->sections[i].size = be32_at(data, DOL_OFF_SIZES + (i * 4U));
    }
    out->bss_addr = be32_at(data, DOL_OFF_BSS_ADDR);
    out->bss_size = be32_at(data, DOL_OFF_BSS_SIZE);
    out->entry = be32_at(data, DOL_OFF_ENTRY);

    const DolResult sections = check_sections(out, size);
    if (sections != DOL_OK) {
        return sections;
    }
    if (out->bss_size != 0U && !fits_mem1(out->bss_addr, out->bss_size)) {
        return DOL_ERR_SECTION_MEM1;
    }
    if (dol_text_section_of(out, out->entry) < 0) {
        return DOL_ERR_ENTRY;
    }
    return DOL_OK;
}

int dol_text_section_of(const DolImage *image, GuestAddr addr) {
    for (u32 i = 0; i < DOL_TEXT_COUNT; i++) {
        const DolSection *s = &image->sections[i];
        if (s->size != 0U && addr >= s->addr && addr - s->addr < s->size) {
            return (int)i;
        }
    }
    return -1;
}

DolResult dol_load(const DolImage *image, const u8 *data, size_t size) {
    const DolResult check = check_sections(image, size);
    if (check != DOL_OK) {
        return check;
    }
    if (image->bss_size != 0U) {
        if (!fits_mem1(image->bss_addr, image->bss_size)) {
            return DOL_ERR_SECTION_MEM1;
        }
        /* Cópia em bloco dentro da MEM1: mem_host_ptr só vive nesta função (PADROES_DE_CODIGO.md §4). */
        memset(mem_host_ptr(image->bss_addr, image->bss_size), 0, image->bss_size);
    }
    for (u32 i = 0; i < DOL_SECTION_COUNT; i++) {
        const DolSection *s = &image->sections[i];
        if (s->size != 0U) {
            memcpy(mem_host_ptr(s->addr, s->size), data + s->offset, s->size);
        }
    }
    return DOL_OK;
}
