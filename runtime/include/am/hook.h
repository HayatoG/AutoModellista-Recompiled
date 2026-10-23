/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ganchos por endereço: funções do host que o chassi chama quando vai despachar um pc (HLE de OSLink, contagem de
 * quadros, parada). O gancho pode alterar o CPUState (ex.: trocar um argumento) e deixar a função do jogo rodar, ou
 * pedir que o chassi pare.
 *
 * Limite estrutural (docs/M4_BOOT.md §5.2): um bl para dentro do mesmo trecho do C gerado vira goto e não passa pelo
 * chassi, então um gancho só vê as chamadas que vêm de outro trecho. hook_check_reachable confere isso no código do
 * DOL antes de rodar. */
#ifndef AM_HOOK_H
#define AM_HOOK_H

#include "am/types.h"

typedef enum HookAction {
    HOOK_CONTINUE, /* segue para o despacho normal do pc (que o gancho pode ter mudado) */
    HOOK_STOP,     /* o chassi devolve CHASSIS_STOP_HOOK antes de despachar */
} HookAction;

typedef HookAction (*HookFn)(CPUState *cpu, void *user);

#define HOOK_MAX 32U

void hook_clear(void);
bool hook_register(GuestAddr addr, const char *name, HookFn fn, void *user);
/* Chamado pelo chassi antes de cada despacho; sem gancho no pc é só uma comparação. */
HookAction hook_run(CPUState *cpu);

/* Faixa de código com o conteúdo original (seção de texto do DOL), para a busca de desvios. */
typedef struct HookCodeRange {
    GuestAddr addr;
    u32 size;
    const u8 *data;
} HookCodeRange;

/* false (com log de cada caso) se algum b/bl direto para um endereço com gancho sai do mesmo trecho do gerado. */
bool hook_check_reachable(const HookCodeRange *ranges, u32 count);

u64 hook_hits(GuestAddr addr);

#endif /* AM_HOOK_H */
