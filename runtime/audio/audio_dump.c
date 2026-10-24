/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/audio_dump.h"

#include <stdio.h>
#include <string.h>

#include "am/log.h"

static struct {
    FILE *file;
    char path[512];
    u64 max_bytes;
    u64 bytes;
    u64 frames;
} s_dump;

static void put32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

void audio_dump_header(u8 out[44], u32 rate_hz, u32 data_bytes) {
    memcpy(out, "RIFF", 4);
    put32(out + 4, data_bytes + 36U);
    memcpy(out + 8, "WAVEfmt ", 8);
    put32(out + 16, 16U);
    put32(out + 20, 0x00020001U); /* PCM, 2 canais */
    put32(out + 24, rate_hz);
    put32(out + 28, rate_hz * 4U);
    put32(out + 32, 0x00100004U); /* 4 bytes por quadro, 16 bits */
    memcpy(out + 36, "data", 4);
    put32(out + 40, data_bytes);
}

bool audio_dump_open(const char *path, u32 rate_hz, u64 max_bytes) {
    audio_dump_close();
    memset(&s_dump, 0, sizeof(s_dump));
    s_dump.file = fopen(path, "wb");
    if (s_dump.file == NULL) {
        return false;
    }
    (void)snprintf(s_dump.path, sizeof(s_dump.path), "%s", path);
    s_dump.max_bytes = max_bytes;
    u8 header[44];
    audio_dump_header(header, rate_hz, 0);
    return fwrite(header, 1, sizeof(header), s_dump.file) == sizeof(header);
}

void audio_dump_write(const s16 *lr, u32 frames) {
    if (s_dump.file == NULL) {
        return;
    }
    const u64 bytes = (u64)frames * 4U;
    if (s_dump.bytes + bytes > s_dump.max_bytes) {
        am_fatal("--audio-dump: limite de %llu bytes em %s (aumente --audio-dump-max-mb)",
                 (unsigned long long)s_dump.max_bytes, s_dump.path);
    }
    u8 buf[256 * 4];
    for (u32 done = 0; done < frames;) {
        const u32 n = frames - done < 256U ? frames - done : 256U;
        for (u32 i = 0; i < n * 2U; i++) {
            const u16 v = (u16)lr[(done * 2U) + i]; /* little-endian, como o WAV pede */
            buf[i * 2U] = (u8)v;
            buf[(i * 2U) + 1U] = (u8)(v >> 8);
        }
        if (fwrite(buf, 4, n, s_dump.file) != n) {
            am_fatal("--audio-dump: erro de escrita em %s", s_dump.path);
        }
        done += n;
    }
    s_dump.bytes += bytes;
    s_dump.frames += frames;
}

void audio_dump_close(void) {
    if (s_dump.file == NULL) {
        return;
    }
    u8 header[44];
    audio_dump_header(header, 0, (u32)s_dump.bytes);
    /* Só os dois tamanhos mudam: o resto do cabeçalho já está no arquivo. */
    (void)fseek(s_dump.file, 4, SEEK_SET);
    (void)fwrite(header + 4, 1, 4, s_dump.file);
    (void)fseek(s_dump.file, 40, SEEK_SET);
    (void)fwrite(header + 40, 1, 4, s_dump.file);
    (void)fclose(s_dump.file);
    s_dump.file = NULL;
}

u64 audio_dump_frames(void) {
    return s_dump.frames;
}
