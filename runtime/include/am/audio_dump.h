/* SPDX-License-Identifier: GPL-3.0-or-later */
/* --audio-dump (M7): grava a saída da DMA de áudio num WAV PCM 16 bits estéreo, no mesmo formato do dspdump.wav do
 * Dolphin (AudioCommon/WaveFile.cpp: cabeçalho de 44 bytes, taxa inteira 108 MHz / 3372 = 32028 Hz, L antes de R,
 * sem pular silêncio). As amostras vêm no ritmo do tempo emulado, então o arquivo é o mesmo em qualquer velocidade
 * de execução e em qualquer arquitetura. */
#ifndef AM_AUDIO_DUMP_H
#define AM_AUDIO_DUMP_H

#include "am/types.h"

/* Abre `path` e grava o cabeçalho. `max_bytes` é o teto dos dados: passar disso é am_fatal (como o --trace). */
bool audio_dump_open(const char *path, u32 rate_hz, u64 max_bytes);
/* `frames` quadros L, R intercalados. */
void audio_dump_write(const s16 *lr, u32 frames);
/* Corrige os tamanhos do cabeçalho e fecha. Sem arquivo aberto, não faz nada. */
void audio_dump_close(void);
u64 audio_dump_frames(void);

/* Cabeçalho WAV de 44 bytes para `data_bytes` de PCM 16 bits estéreo (exposto para teste). */
void audio_dump_header(u8 out[44], u32 rate_hz, u32 data_bytes);

#endif /* AM_AUDIO_DUMP_H */
