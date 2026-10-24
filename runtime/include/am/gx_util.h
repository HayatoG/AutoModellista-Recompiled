/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Utilidades numéricas comuns aos módulos do GX (só funções inline). */
#ifndef AM_GX_UTIL_H
#define AM_GX_UTIL_H

#include "am/types.h"

static inline s32 gx_clamp_s32(s32 v, s32 lo, s32 hi) {
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

static inline f32 gx_clamp_f32(f32 v, f32 lo, f32 hi) {
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

static inline s32 gx_min_s32(s32 a, s32 b) {
    return a < b ? a : b;
}

static inline s32 gx_max_s32(s32 a, s32 b) {
    return a > b ? a : b;
}

#endif /* AM_GX_UTIL_H */
