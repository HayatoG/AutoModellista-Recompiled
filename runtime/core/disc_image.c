/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/disc_image.h"

#include <stdlib.h>
#include <string.h>

#include "am/platform_fs.h"

/* Magia no deslocamento 0x1C do cabeçalho do disco do GameCube (YAGCD 13.1) e no 0x18 do Wii. */
static const u8 s_gc_magic[4] = {0xC2, 0x33, 0x9F, 0x3D};
static const u8 s_wii_magic[4] = {0x5D, 0x1C, 0x9E, 0xA3};
static const u8 s_rvz_magic[4] = {'R', 'V', 'Z', 0x01};
static const u8 s_wia_magic[4] = {'W', 'I', 'A', 0x01};

#define DISC_HEADER_MIN 0x440U

const char *disc_image_result_name(DiscImageResult result) {
    switch (result) {
    case DISC_IMAGE_OK:
        return "ok";
    case DISC_IMAGE_ERR_OPEN:
        return "open";
    case DISC_IMAGE_ERR_FORMAT:
        return "format";
    case DISC_IMAGE_ERR_WIA:
        return "wia";
    case DISC_IMAGE_ERR_COMPRESSION:
        return "compression";
    case DISC_IMAGE_ERR_NOT_GAMECUBE:
        return "not-gamecube";
    case DISC_IMAGE_ERR_CORRUPT:
        return "corrupt";
    case DISC_IMAGE_ERR_RANGE:
        return "range";
    case DISC_IMAGE_ERR_IO:
        return "io";
    case DISC_IMAGE_ERR_NOMEM:
        return "nomem";
    }
    return "?";
}

typedef struct RawSource {
    DiscSource base;
    FILE *file;
} RawSource;

static DiscImageResult raw_read(DiscSource *src, u64 offset, u8 *dst, u32 size) {
    RawSource *raw = (RawSource *)src;
    if (offset > src->size || size > src->size - offset) {
        return DISC_IMAGE_ERR_RANGE;
    }
    if (size == 0) {
        return DISC_IMAGE_OK;
    }
    if (!platform_fs_seek(raw->file, offset) || fread(dst, 1, size, raw->file) != size) {
        return DISC_IMAGE_ERR_IO;
    }
    return DISC_IMAGE_OK;
}

static void raw_destroy(DiscSource *src) {
    RawSource *raw = (RawSource *)src;
    (void)fclose(raw->file);
    free(raw);
}

static DiscImageResult raw_open(FILE *file, u64 size, DiscSource **out) {
    RawSource *raw = (RawSource *)calloc(1, sizeof(*raw));
    if (raw == NULL) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_NOMEM;
    }
    raw->file = file;
    raw->base.size = size;
    raw->base.read = raw_read;
    raw->base.destroy = raw_destroy;
    *out = &raw->base;
    return DISC_IMAGE_OK;
}

DiscImageResult disc_image_open(const char *path, DiscSource **out) {
    *out = NULL;
    FILE *file = platform_fs_fopen(path, "rb");
    if (file == NULL) {
        return DISC_IMAGE_ERR_OPEN;
    }
    u64 size = 0;
    u8 head[0x20];
    if (!platform_fs_size(file, &size) || !platform_fs_seek(file, 0)) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_IO;
    }
    if (size < sizeof(head) || fread(head, 1, sizeof(head), file) != sizeof(head)) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_FORMAT;
    }
    if (memcmp(head, s_rvz_magic, 4) == 0) {
        return disc_rvz_open(file, size, out);
    }
    if (memcmp(head, s_wia_magic, 4) == 0) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_WIA;
    }
    if (memcmp(&head[0x18], s_wii_magic, 4) == 0) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_NOT_GAMECUBE;
    }
    if (memcmp(&head[0x1C], s_gc_magic, 4) != 0 || size < DISC_HEADER_MIN) {
        (void)fclose(file);
        return DISC_IMAGE_ERR_FORMAT;
    }
    return raw_open(file, size, out);
}

DiscImageResult disc_image_read(DiscSource *src, u64 offset, u8 *dst, u32 size) {
    return src->read(src, offset, dst, size);
}

void disc_image_close(DiscSource *src) {
    if (src != NULL) {
        src->destroy(src);
    }
}
