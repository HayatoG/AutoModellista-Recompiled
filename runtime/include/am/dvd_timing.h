/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tempo de leitura do drive de DVD do GameCube, como o Dolphin 2609a o calcula (Core/HW/DVD/DVDInterface.cpp:
 * ScheduleReads; Core/HW/DVD/DVDMath.cpp): latência do comando, busca da cabeça, latência de rotação, velocidade CAV
 * conforme o raio e o buffer de 1 MiB que o drive continua enchendo depois de cada leitura. Só disco de GameCube,
 * sem partição Wii e sem "Speed Up Disc Transfer Rate" (o oráculo roda com o padrão, desligado).
 *
 * Por que imitar: o jogo tem threads que leem o disco ao mesmo tempo (música e cenário em fluxo); a ordem e o tamanho
 * das leituras dependem de quando cada uma termina. Com um modelo de tempo diferente do Dolphin a sequência de
 * eventos diverge (docs/M5_OS_DISCO.md §5). */
#ifndef AM_DVD_TIMING_H
#define AM_DVD_TIMING_H

#include "am/types.h"

/* SystemTimers.cpp: ticks por segundo no GameCube = clock da CPU. */
#define DVD_TIMING_TICKS_PER_SECOND 486000000ULL

void dvd_timing_reset(void);
/* Leitura de `length` bytes no deslocamento `offset` do disco pedida no ciclo `now`: devolve quantos ciclos até o
 * fim (a interrupção TCINT) e atualiza o estado do buffer do drive. */
u64 dvd_timing_read(u64 now, u64 offset, u32 length);

/* DVDMath, expostas para teste. */
double dvd_timing_seek_seconds(u64 from, u64 to);
double dvd_timing_rotational_latency(u64 offset, double time_seconds);
double dvd_timing_raw_read_seconds(u64 offset, u64 length);

#endif /* AM_DVD_TIMING_H */
