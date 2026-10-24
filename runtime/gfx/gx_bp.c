/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gx_bp.h"

#include <string.h>

#include "am/log.h"
#include "am/mem.h"

#define BP_REG_COUNT 256U
#define BP_MASK_ALL  0xFFFFFFU

static struct {
    u32 regs[BP_REG_COUNT];
    u32 mask;
    s16 tev_color[4][4];
    s16 tev_konst[4][4];
    u8 tmem[GX_TMEM_SIZE];
    u64 tmem_writes; /* M8: conta as cargas na TMEM (o cache de texturas só refaz o hash da TLUT quando muda) */
} s_bp;

void gx_bp_reset(void) {
    memset(&s_bp, 0, sizeof(s_bp));
    s_bp.mask = BP_MASK_ALL;
}

u32 gx_bp_reg(u8 reg) {
    return s_bp.regs[reg];
}

u32 gx_bp_tex_reg(u32 kind, u32 texmap) {
    AM_ASSERT(texmap < GX_TEXMAP_MAX, "texmap %u", texmap);
    return s_bp.regs[kind + (texmap & 3U) + ((texmap & 4U) != 0U ? 0x20U : 0U)];
}

void gx_bp_tev_color(u32 n, s16 *rgba) {
    memcpy(rgba, s_bp.tev_color[n & 3U], sizeof(s_bp.tev_color[0]));
}

void gx_bp_tev_konst(u32 n, s16 *rgba) {
    memcpy(rgba, s_bp.tev_konst[n & 3U], sizeof(s_bp.tev_konst[0]));
}

const u8 *gx_bp_tmem(u32 offset, u32 size) {
    if (offset > GX_TMEM_SIZE || size > GX_TMEM_SIZE - offset) {
        return NULL;
    }
    return s_bp.tmem + offset;
}

static s16 sign11(u32 v) {
    return (s16)((s32)(v << 21) >> 21);
}

/* TevReg: RA (vermelho 0-10, alfa 12-22) ou BG (azul 0-10, verde 12-22); bit 23 = konst. */
static void tev_color_write(u8 reg, u32 v) {
    const u32 n = (reg >> 1) & 3U;
    s16 *dst = (v & 0x800000U) != 0U ? s_bp.tev_konst[n] : s_bp.tev_color[n];
    if ((reg & 1U) == 0U) {
        dst[0] = sign11(v);
        dst[3] = sign11(v >> 12);
    } else {
        dst[2] = sign11(v);
        dst[1] = sign11(v >> 12);
    }
}

/* O que passar do fim da TMEM é descartado. */
u64 gx_bp_tmem_writes(void) {
    return s_bp.tmem_writes;
}

static void copy_to_tmem(u32 tmem_offset, u32 src, u32 size) {
    s_bp.tmem_writes++;
    if (tmem_offset >= GX_TMEM_SIZE) {
        return;
    }
    const u32 n = size > GX_TMEM_SIZE - tmem_offset ? GX_TMEM_SIZE - tmem_offset : size;
    u8 *dst = s_bp.tmem + tmem_offset;
    for (u32 i = 0; i < n; i++) {
        /* offset < 1 MiB e n <= 1 MiB - offset; o analisador do clang 21 não acompanha (falso positivo). */
        // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
        dst[i] = mem_read8(NULL, MEM_CACHED_BASE | ((src + i) & 0x01FFFFFFU));
    }
}

/* BPMEM_LOADTLUT1: tlut_dest (endereço << 9, linhas de 32 bytes) a partir de LOADTLUT0 << 5. O GameCube ignora os
 * bits altos do endereço (BPStructs.cpp). */
static void load_tlut(u32 v) {
    copy_to_tmem((v & 0x3FFU) << 9, (s_bp.regs[GX_BP_LOADTLUT0] << 5) & 0x01FFFFFFU,
                 ((v >> 10) & 0x7FFU) * GX_TMEM_LINE);
}

/* BPMEM_PRELOAD_MODE != 0: GXPreLoadEntireTexture. Tipo 3 (RGBA8) intercala AR e GB em bancos separados. */
static void preload(void) {
    const u32 info = s_bp.regs[GX_BP_PRELOAD_MODE];
    const u32 count = info & 0x7FFFU;
    const u32 src = s_bp.regs[GX_BP_PRELOAD_ADDR] << 5;
    u32 even = s_bp.regs[GX_BP_PRELOAD_TMEMEVEN] * GX_TMEM_LINE;
    if (((info >> 15) & 3U) != 3U) {
        copy_to_tmem(even, src, count * GX_TMEM_LINE);
        return;
    }
    u32 odd = s_bp.regs[GX_BP_PRELOAD_TMEMODD] * GX_TMEM_LINE;
    for (u32 i = 0; i < count; i++) {
        copy_to_tmem(even, src + (i * 2U * GX_TMEM_LINE), GX_TMEM_LINE);
        copy_to_tmem(odd, src + (((i * 2U) + 1U) * GX_TMEM_LINE), GX_TMEM_LINE);
        even += GX_TMEM_LINE;
        odd += GX_TMEM_LINE;
    }
}

GxBpEvent gx_bp_write(u8 reg, u32 value) {
    const u32 newval = (s_bp.regs[reg] & ~s_bp.mask) | (value & s_bp.mask);
    if (reg != GX_BP_MASK) {
        s_bp.mask = BP_MASK_ALL;
    }
    s_bp.regs[reg] = newval;
    switch (reg) {
    case GX_BP_MASK:
        s_bp.mask = newval & BP_MASK_ALL;
        return GX_BP_EVENT_NONE;
    case GX_BP_SETDRAWDONE:
        return (newval & 0xFFU) == 0x02U ? GX_BP_EVENT_DRAW_DONE : GX_BP_EVENT_NONE;
    case GX_BP_PE_TOKEN:
        return GX_BP_EVENT_TOKEN;
    case GX_BP_PE_TOKEN_INT:
        return GX_BP_EVENT_TOKEN_INT;
    case GX_BP_COPY_EXECUTE:
        return GX_BP_EVENT_COPY;
    case GX_BP_LOADTLUT1:
        load_tlut(newval);
        return GX_BP_EVENT_NONE;
    case GX_BP_PRELOAD_MODE:
        if (newval != 0U) {
            preload();
        }
        return GX_BP_EVENT_NONE;
    default:
        if (reg >= GX_BP_TEV_COLOR_RA && reg < GX_BP_FOGRANGE) {
            tev_color_write(reg, newval);
        }
        return GX_BP_EVENT_NONE;
    }
}
