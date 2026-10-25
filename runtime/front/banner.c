/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/banner.h"

#include <stdio.h>
#include <string.h>

#include "am/gx_tex.h"
#include "am/platform.h"

/* YAGCD 14: nome curto (0x1820, 32 bytes), fabricante curto (0x1840, 32), nome longo (0x1860, 64)... */
#define BANNER_LONG_NAME     0x1860U
#define BANNER_LONG_NAME_LEN 64U

bool banner_decode(const u8 *data, size_t size, Banner *out) {
    if (size < BANNER_LONG_NAME + BANNER_LONG_NAME_LEN ||
        (memcmp(data, "BNR1", 4) != 0 && memcmp(data, "BNR2", 4) != 0)) {
        return false;
    }
    const GxTexSource src = {.data = data + BANNER_IMAGE_START, .size = BANNER_IMAGE_SIZE};
    for (u32 t = 0; t < BANNER_HEIGHT; t++) {
        for (u32 s = 0; s < BANNER_WIDTH; s++) {
            gx_tex_decode_texel(&out->rgba[((t * BANNER_WIDTH) + s) * 4U], &src, s, t, BANNER_WIDTH - 1U, GX_TF_RGB5A3);
        }
    }
    char name[BANNER_LONG_NAME_LEN + 1U];
    memcpy(name, data + BANNER_LONG_NAME, BANNER_LONG_NAME_LEN);
    name[BANNER_LONG_NAME_LEN] = '\0';
    const char *p = name;
    while (*p == ' ') {
        p++;
    }
    (void)snprintf(out->name, sizeof(out->name), "%s", p);
    size_t n = strlen(out->name);
    while (n != 0U && out->name[n - 1U] == ' ') {
        out->name[--n] = '\0';
    }
    return true;
}

bool banner_load(const char *disc_root, Banner *out) {
    char path[1200];
    const int n = snprintf(path, sizeof(path), "%s/files/opening.bnr", disc_root);
    size_t size = 0;
    u8 *data = n > 0 && (size_t)n < sizeof(path) ? platform_load_file(path, &size) : NULL;
    if (data == NULL) {
        return false;
    }
    const bool ok = banner_decode(data, size, out);
    platform_free_file(data);
    return ok;
}
