/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: perfil de ciclos emulados por trecho do C gerado e por thread do SO do jogo (diagnóstico do tempo emulado).
 * O chassi informa cada fatia (pc de entrada, ciclos descontados); entre profile_begin e profile_end os ciclos são
 * somados e profile_end grava as tabelas ordenadas em texto. Determinístico: só ciclos, nada do relógio do host. */
#ifndef AM_PROFILE_H
#define AM_PROFILE_H

#include <stdio.h>

#include "am/types.h"

/* Começa a somar (zera as tabelas). */
void profile_begin(void);
/* Soma uma fatia: pc de entrada do trecho, thread corrente do SO (OSGetCurrentThread) e ciclos. */
void profile_add(GuestAddr pc, GuestAddr thread, u64 cycles);
/* Para de somar e grava as `top` maiores entradas de cada tabela em `out`. */
void profile_end(FILE *out, u32 top);
bool profile_active(void);

#endif /* AM_PROFILE_H */
