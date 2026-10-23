/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Leitura das OSThread do SDK na memória do guest, para diagnóstico (nenhuma escrita). Offsets do OSThread.h do SDK
 * de 2002/2003 e globais do SO em memória baixa (YAGCD 4.2.1): __OSActiveThreadQueue em 0x800000DC (cabeça) e
 * __OSCurrentThread em 0x800000E4. */
#ifndef AM_OS_THREADS_H
#define AM_OS_THREADS_H

#include "am/types.h"

/* Escreve no log (nível INFO) cada thread ativa: estado, prioridade, pc de retomada, LR, pilha e a cadeia de
 * retornos salvos (back chain) até `depth` quadros. `cpu` é o estado atual (a thread corrente usa os registradores
 * vivos, não o contexto salvo). */
void os_threads_dump(const CPUState *cpu, u32 depth);

#endif /* AM_OS_THREADS_H */
