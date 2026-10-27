/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/launcher_gfx.h"

#include <math.h>
#include <stdlib.h>

float launcher_scale(float width, float height) {
    const float sx = width / LAUNCHER_DESIGN_W;
    const float sy = height / LAUNCHER_DESIGN_H;
    const float s = sx < sy ? sx : sy;
    return s < 0.4F ? 0.4F : (s > 8.0F ? 8.0F : s);
}

LauncherRect launcher_cover(float view_w, float view_h, u32 img_w, u32 img_h) {
    LauncherRect r = {0.0F, 0.0F, view_w, view_h};
    if (img_w == 0U || img_h == 0U || view_w <= 0.0F || view_h <= 0.0F) {
        return r;
    }
    const float sx = view_w / (float)img_w;
    const float sy = view_h / (float)img_h;
    const float s = sx > sy ? sx : sy;
    r.w = (float)img_w * s;
    r.h = (float)img_h * s;
    r.x = (view_w - r.w) * 0.5F;
    r.y = (view_h - r.h) * 0.5F;
    return r;
}

u32 launcher_glass_width(u32 blur) {
    const u32 b = blur < 4U ? 4U : (blur > 48U ? 48U : blur);
    return 1920U / (b * 2U / 3U + 4U) + 24U; /* 4 px: 344; 24 px: 120; 48 px: 77 */
}

/* Cobertura de [a, b) sobre o pixel de origem i, em fração do tamanho do pixel de destino. */
static float coverage(float a, float b, u32 i) {
    const float lo = a > (float)i ? a : (float)i;
    const float hi = b < (float)(i + 1U) ? b : (float)(i + 1U);
    return hi > lo ? hi - lo : 0.0F;
}

void launcher_box_reduce(const u8 *src, u32 sw, u32 sh, u8 *dst, u32 dw, u32 dh) {
    const float fx = (float)sw / (float)dw;
    const float fy = (float)sh / (float)dh;
    for (u32 y = 0; y < dh; y++) {
        const float y0 = (float)y * fy;
        const float y1 = y0 + fy;
        const u32 sy0 = (u32)y0;
        u32 sy1 = (u32)ceilf(y1);
        sy1 = sy1 > sh ? sh : sy1;
        for (u32 x = 0; x < dw; x++) {
            const float x0 = (float)x * fx;
            const float x1 = x0 + fx;
            const u32 sx0 = (u32)x0;
            u32 sx1 = (u32)ceilf(x1);
            sx1 = sx1 > sw ? sw : sx1;
            float acc[4] = {0.0F, 0.0F, 0.0F, 0.0F};
            float total = 0.0F;
            for (u32 sy = sy0; sy < sy1; sy++) {
                const float wy = coverage(y0, y1, sy);
                for (u32 sx = sx0; sx < sx1; sx++) {
                    const float w = wy * coverage(x0, x1, sx);
                    const u8 *p = src + ((size_t)sy * sw + sx) * 4U;
                    acc[0] += w * (float)p[0];
                    acc[1] += w * (float)p[1];
                    acc[2] += w * (float)p[2];
                    acc[3] += w * (float)p[3];
                    total += w;
                }
            }
            u8 *o = dst + ((size_t)y * dw + x) * 4U;
            for (u32 c = 0; c < 4U; c++) {
                o[c] = (u8)(total > 0.0F ? acc[c] / total + 0.5F : 0.0F);
            }
        }
    }
}

static u8 clamp8(float v) {
    return (u8)(v < 0.0F ? 0.0F : (v > 255.0F ? 255.0F : v + 0.5F));
}

/* Um passe 1-2-1 nos dois eixos, de src para dst (bordas repetidas). */
static void blur_pass(const u8 *src, u8 *dst, u32 w, u32 h) {
    static const float k[3] = {0.25F, 0.5F, 0.25F};
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            float acc[3] = {0.0F, 0.0F, 0.0F};
            for (u32 j = 0; j < 3U; j++) {
                const u32 yy = y + j == 0U ? 0U : (y + j - 1U >= h ? h - 1U : y + j - 1U);
                for (u32 i = 0; i < 3U; i++) {
                    const u32 xx = x + i == 0U ? 0U : (x + i - 1U >= w ? w - 1U : x + i - 1U);
                    const u8 *p = src + ((size_t)yy * w + xx) * 4U;
                    for (u32 c = 0; c < 3U; c++) {
                        acc[c] += k[i] * k[j] * (float)p[c];
                    }
                }
            }
            u8 *o = dst + ((size_t)y * w + x) * 4U;
            o[0] = clamp8(acc[0]);
            o[1] = clamp8(acc[1]);
            o[2] = clamp8(acc[2]);
            o[3] = 255U;
        }
    }
}

u32 launcher_glass_passes(u32 blur, u32 width) {
    const float b = (float)(blur < 4U ? 4U : (blur > 48U ? 48U : blur));
    const float sigma_texels = b * 0.6F * (float)width / LAUNCHER_DESIGN_W;
    const u32 n = (u32)(2.0F * sigma_texels * sigma_texels + 0.5F);
    return n < 1U ? 1U : (n > 40U ? 40U : n);
}

void launcher_glass_finish(u8 *rgba, u32 w, u32 h, u32 passes, float saturation) {
    u8 *tmp = (u8 *)malloc((size_t)w * h * 4U);
    if (tmp == NULL) {
        return;
    }
    for (u32 n = 0; n < passes; n++) {
        blur_pass(rgba, tmp, w, h);
        for (size_t i = 0; i < (size_t)w * h * 4U; i++) {
            rgba[i] = tmp[i];
        }
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        u8 *p = rgba + i * 4U;
        const float luma = 0.2126F * (float)p[0] + 0.7152F * (float)p[1] + 0.0722F * (float)p[2];
        for (u32 c = 0; c < 3U; c++) {
            p[c] = clamp8(luma + ((float)p[c] - luma) * saturation);
        }
        p[3] = 255U;
    }
    free(tmp);
}
