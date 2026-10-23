/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Dispatcher: tabela ordenada endereço -> função recompilada, busca binária (PLANO_PORT_C.md §2.1).
 *
 * Cada entrada cobre um intervalo [start, end) do guest. A função é chamada com cpu->pc == endereço de destino
 * e devolve quando precisa que o dispatcher decida o próximo pc (convenção do C gerado). */
#ifndef AM_DISPATCH_H
#define AM_DISPATCH_H

#include "am/types.h"

typedef void (*DispatchFn)(CPUState *cpu);

typedef struct DispatchEntry {
    GuestAddr start; /* inclusivo */
    GuestAddr end;   /* exclusivo */
    DispatchFn fn;
} DispatchEntry;

typedef enum DispatchResult {
    DISPATCH_OK,
    DISPATCH_ERR_INVALID, /* fn nulo, intervalo vazio ou endereço desalinhado */
    DISPATCH_ERR_OVERLAP, /* cruza uma entrada já registrada */
    DISPATCH_ERR_NOMEM,
} DispatchResult;

DispatchResult dispatch_register(GuestAddr start, GuestAddr end, DispatchFn fn);
void dispatch_clear(void);
size_t dispatch_count(void);

/* Entradas em ordem crescente de `start`; o ponteiro vale até a próxima alteração da tabela. */
const DispatchEntry *dispatch_entries(void);

/* NULL se nenhuma entrada cobre `pc`. */
DispatchFn dispatch_lookup(GuestAddr pc);

/* Executa a função que cobre cpu->pc uma vez. Sem função: am_fatal (não existe fallback; PLANO §2, princípio 4). */
void dispatch_step(CPUState *cpu);

#endif /* AM_DISPATCH_H */
