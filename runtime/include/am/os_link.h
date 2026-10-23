/* SPDX-License-Identifier: GPL-3.0-or-later */
/* HLE de OSLink/OSLinkFixed/OSUnlink (PLANO_PORT_C.md §2.1; docs/M5_OS_DISCO.md §3).
 *
 * Na entrada de OSLinkFixed(módulo, bss), com o módulo no endereço real R que o carregador do jogo escolheu:
 *   1. identifica o módulo pelo id do cabeçalho e confere que os bytes em R são o .rel do disco;
 *   2. calcula a base virtual V (a do C gerado: primeiro trecho do módulo menos o deslocamento da primeira seção
 *      executável) e o .bss virtual (V + fixSize alinhado, como o DolRecomp o pôs);
 *   3. liga a janela [V, ...) -> [R, ...) (core/rel_map.c) e troca r3 = V e r4 = .bss virtual;
 *   4. deixa o Link do SDK (recompilado) rodar: ele relocaliza tudo com endereços virtuais, coerentes com os imediatos
 *      do C gerado, e grava pelo alias nos bytes reais;
 *   5. no retorno confere r3 = TRUE e encolhe a janela para código + dados + .bss (as relocações viram heap).
 * OSUnlink(R) recebe r3 = V pelo mesmo motivo. Cada ligação é registrada (log e os_link_events). */
#ifndef AM_OS_LINK_H
#define AM_OS_LINK_H

#include "am/types.h"

/* Bytes do .rel original (files/module/<nome>.rel), para conferir o que o jogo carregou. `data` vive até o fim. */
typedef bool (*OsLinkLoadRel)(const char *name, const u8 **data, size_t *size, void *user);

typedef struct OsLinkEvent {
    char name[16];
    u32 id;
    bool unlink;
    GuestAddr real;
    GuestAddr virt;
    GuestAddr bss_real; /* o que o jogo passou em r4 */
    GuestAddr bss_virt;
    u32 size; /* tamanho da janela depois do link */
    u64 cycle;
} OsLinkEvent;

/* Um REL recompilado: nome ("game"), id do cabeçalho, início do primeiro trecho e ponto de entrada (prolog) do C
 * gerado, ambos virtuais. main.c monta a lista a partir de am/recomp.h ("game_1" -> "game", 1). */
typedef struct OsLinkModule {
    char name[16];
    u32 id;
    GuestAddr code_start;
    GuestAddr prolog;
} OsLinkModule;

#define OS_LINK_MAX_MODULES 16U

typedef struct OsLinkConfig {
    GuestAddr os_link;
    GuestAddr os_link_fixed;
    GuestAddr os_unlink;
    const OsLinkModule *modules;
    u32 module_count;
    OsLinkLoadRel load_rel;
    void *user;
    /* Opcional: cada ligação/desligação, logo que acontece (log de eventos). */
    void (*on_event)(const OsLinkEvent *event, void *user);
} OsLinkConfig;

#define OS_LINK_MAX_EVENTS 64U

/* Registra os ganchos (am/hook.h). */
bool os_link_init(const OsLinkConfig *config);
u32 os_link_event_count(void);
const OsLinkEvent *os_link_event(u32 index);

#endif /* AM_OS_LINK_H */
