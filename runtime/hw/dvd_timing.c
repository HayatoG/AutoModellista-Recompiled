/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Modelo de tempo do drive (am/dvd_timing.h). As constantes e a ordem das contas, inclusive as conversões entre
 * inteiro e double, seguem DVDInterface.cpp (ScheduleReads) e DVDMath.cpp do Dolphin 2609a, consultados em
 * /Volumes/AMRecomp/ref/dolphin; nada copiado. Arredondar diferente muda o ciclo da interrupção. */
#include "am/dvd_timing.h"

#include <math.h>

/* DVDInterface.cpp. */
#define READ_COMMAND_LATENCY_US    600ULL
#define STREAMING_BUFFER_SIZE      (1024ULL * 1024ULL)
#define DVD_SECTOR_SIZE            0x800ULL
#define DVD_ECC_BLOCK_SIZE         (16ULL * DVD_SECTOR_SIZE)
#define BUFFER_TRANSFER_RATE       (32ULL * 1024ULL * 1024ULL)
#define BUFFER_BACKWARD_SEEK_LIMIT (DVD_ECC_BLOCK_SIZE * 2ULL)

/* DVDMath.cpp (raios em metros, velocidades em bytes/s). GC e Wii têm a mesma densidade: o Dolphin usa a geometria
 * do Wii para a posição e a faixa de velocidade do GC para a leitura. */
#define WII_DISC_LAYER_SIZE         0x118240000ULL
#define DVD_INNER_RADIUS            0.024
#define WII_DVD_OUTER_RADIUS        0.058
#define GC_DVD_OUTER_RADIUS         0.038
#define DVD_TRACK_PITCH             0.00000074
#define GC_DISC_INNER_READ_SPEED    (1024.0 * 1024.0 * 2.1)
#define GC_DISC_OUTER_READ_SPEED    (1024.0 * 1024.0 * 3.325)
#define GC_ROTATIONS_PER_SECOND     28.5
#define SHORT_SEEK_MAX_DISTANCE     0.001
#define SHORT_SEEK_CONSTANT         0.035
#define SHORT_SEEK_VELOCITY_INVERSE 50.0
#define LONG_SEEK_CONSTANT          0.075
#define LONG_SEEK_VELOCITY_INVERSE  4.5

static struct {
    u64 buffer_start_offset;
    u64 buffer_end_offset;
    u64 buffer_start_time;
    u64 buffer_end_time;
} s_dvd;

void dvd_timing_reset(void) {
    s_dvd.buffer_start_offset = 0;
    s_dvd.buffer_end_offset = 0;
    s_dvd.buffer_start_time = 0;
    s_dvd.buffer_end_time = 0;
}

static u64 align_down(u64 value, u64 align) {
    return value - (value % align);
}

static u64 align_up(u64 value, u64 align) {
    return align_down(value + align - 1U, align);
}

/* CalculatePhysicalDiscPosition: raio da trilha (espiral) no deslocamento. */
static double physical_position(u64 offset) {
    offset %= WII_DISC_LAYER_SIZE * 2U;
    if (offset > WII_DISC_LAYER_SIZE) {
        offset = (WII_DISC_LAYER_SIZE * 2U) - offset;
    }
    const double inner2 = DVD_INNER_RADIUS * DVD_INNER_RADIUS;
    const double outer2 = WII_DVD_OUTER_RADIUS * WII_DVD_OUTER_RADIUS;
    return sqrt(((double)offset / (double)WII_DISC_LAYER_SIZE * (outer2 - inner2)) + inner2);
}

double dvd_timing_seek_seconds(u64 from, u64 to) {
    const double distance = fabs(physical_position(from) - physical_position(to));
    if (distance < SHORT_SEEK_MAX_DISTANCE) {
        return (distance * SHORT_SEEK_VELOCITY_INVERSE) + SHORT_SEEK_CONSTANT;
    }
    return (distance * LONG_SEEK_VELOCITY_INVERSE) + LONG_SEEK_CONSTANT;
}

double dvd_timing_rotational_latency(u64 offset, double time_seconds) {
    const double target_angle = fmod(physical_position(offset) / DVD_TRACK_PITCH, 1.0);
    const double start_angle = fmod(time_seconds * GC_ROTATIONS_PER_SECOND, 1.0);
    return fmod(target_angle + 1.0 - start_angle, 1.0) / GC_ROTATIONS_PER_SECOND;
}

double dvd_timing_raw_read_seconds(u64 offset, u64 length) {
    const double position = physical_position(offset + (length / 2U));
    const double fraction = (position - DVD_INNER_RADIUS) / (GC_DVD_OUTER_RADIUS - DVD_INNER_RADIUS);
    const double speed = (fraction * (GC_DISC_OUTER_READ_SPEED - GC_DISC_INNER_READ_SPEED)) + GC_DISC_INNER_READ_SPEED;
    return (double)length / speed;
}

typedef struct ReadWindow {
    u64 buffer_start;
    u64 buffer_end;
    u64 head;
} ReadWindow;

/* Começo de ScheduleReads: o que o buffer do drive contém agora e onde está a cabeça. */
static ReadWindow current_window(u64 now, u64 first_block) {
    ReadWindow w = {0, 0, 0};
    if (s_dvd.buffer_start_time == s_dvd.buffer_end_time) {
        return w; /* sem buffer */
    }
    w.buffer_start =
        s_dvd.buffer_end_offset > STREAMING_BUFFER_SIZE ? s_dvd.buffer_end_offset - STREAMING_BUFFER_SIZE : 0U;
    if (now >= s_dvd.buffer_end_time) {
        w.buffer_end = s_dvd.buffer_end_offset;
    } else {
        w.buffer_end =
            s_dvd.buffer_start_offset +
            align_down((now - s_dvd.buffer_start_time) * (s_dvd.buffer_end_offset - s_dvd.buffer_start_offset) /
                           (s_dvd.buffer_end_time - s_dvd.buffer_start_time),
                       DVD_ECC_BLOCK_SIZE);
    }
    w.head = w.buffer_end;
    if (first_block < w.buffer_start) {
        w.buffer_start = 0; /* ler antes do buffer o destrói; a cabeça fica onde estava */
        w.buffer_end = 0;
    }
    return w;
}

/* Fim de ScheduleReads: descarta o que não deve ser reusado e agenda o enchimento do buffer. */
static void update_buffer(u64 now, s64 ticks, const ReadWindow *w, u64 first_block, u64 last_block, bool seek) {
    if (last_block - w->buffer_start <= BUFFER_BACKWARD_SEEK_LIMIT && w->buffer_start != w->buffer_end) {
        return; /* ler os dois primeiros blocos do buffer não muda o estado */
    }
    s_dvd.buffer_start_offset = last_block >= w->buffer_end ? last_block : w->buffer_end;
    s_dvd.buffer_end_offset = last_block + STREAMING_BUFFER_SIZE - BUFFER_BACKWARD_SEEK_LIMIT;
    if (seek && s_dvd.buffer_end_offset < first_block + STREAMING_BUFFER_SIZE) {
        s_dvd.buffer_end_offset = first_block + STREAMING_BUFFER_SIZE;
    }
    s_dvd.buffer_start_time = now + (u64)ticks;
    s_dvd.buffer_end_time = s_dvd.buffer_start_time +
                            (u64)((double)DVD_TIMING_TICKS_PER_SECOND *
                                  dvd_timing_raw_read_seconds(s_dvd.buffer_start_offset,
                                                              s_dvd.buffer_end_offset - s_dvd.buffer_start_offset));
}

u64 dvd_timing_read(u64 now, u64 offset, u32 length) {
    const u64 tps = DVD_TIMING_TICKS_PER_SECOND;
    u64 dvd_offset = align_down(offset, DVD_ECC_BLOCK_SIZE);
    const u64 first_block = dvd_offset;
    ReadWindow w = current_window(now, first_block);
    u64 head = w.head;
    bool seek = false;
    s64 ticks = (s64)(READ_COMMAND_LATENCY_US * (tps / 1000000U));
    do {
        /* "+1": um pedido alinhado lê o bloco inteiro; o último pedaço pode ser curto. */
        u64 chunk = align_up(offset + 1U, DVD_ECC_BLOCK_SIZE) - offset;
        if (chunk > length) {
            chunk = length;
        }
        if (dvd_offset >= w.buffer_start && dvd_offset < w.buffer_end) {
            ticks += (s64)(chunk * tps / BUFFER_TRANSFER_RATE);
        } else {
            if (dvd_offset != head) {
                seek = true;
                ticks += (s64)(u64)((double)tps * dvd_timing_seek_seconds(head, dvd_offset));
                /* O Dolphin divide inteiros aqui: o tempo depois da busca vai em segundos inteiros. */
                const u64 whole_seconds = (now + (u64)ticks) / tps;
                const double rotation = (double)tps * dvd_timing_rotational_latency(dvd_offset, (double)whole_seconds);
                ticks = (s64)((double)ticks + rotation);
            } else {
                ticks += (s64)(u64)((double)tps * dvd_timing_raw_read_seconds(dvd_offset, DVD_ECC_BLOCK_SIZE));
            }
            head = dvd_offset + DVD_ECC_BLOCK_SIZE;
        }
        offset += chunk;
        length -= (u32)chunk;
        dvd_offset += DVD_ECC_BLOCK_SIZE;
    } while (length > 0U);
    update_buffer(now, ticks, &w, first_block, dvd_offset, seek);
    return (u64)ticks;
}
