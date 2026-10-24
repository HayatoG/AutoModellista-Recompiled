/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M8: diagnóstico do tempo emulado contra o Dolphin.
 *   --profile <q0>:<q1>:<arq>   perfil de ciclos (am/profile.h) do quadro q0 ao q1
 *   --probes <q0>:<a>,<b>,...   a partir do quadro q0, "probe <pc> <lr> <timebase>" no --timebase-out a cada entrada
 *                               nesses endereços (o mesmo que dolphin_trace.py --probes) */
#ifndef AM_GAME_DIAG_H
#define AM_GAME_DIAG_H

#include <stdio.h>

#include "am/types.h"

/* `timebase` é o arquivo do --timebase-out (obrigatório com --probes). false = especificação inválida. */
bool game_diag_init(const char *profile, const char *probes, FILE *timebase);
/* Entrada do quadro `frame` (1, 2...). */
void game_diag_frame(u64 frame);
void game_diag_shutdown(void);

#endif /* AM_GAME_DIAG_H */
