/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Janela virtual dos RELs (docs/M5_OS_DISCO.md §3).
 *
 * O C gerado de cada REL foi produzido com o módulo "carregado" numa base virtual fixa (scripts/regen_rels_m5.sh:
 * 0x81800000 em diante, logo depois dos 24 MiB da MEM1) e todas as relocações já aplicadas com essas bases: todo
 * lis/addi para dados do módulo é um imediato virtual. No jogo, o módulo está onde o carregador o pôs (endereço
 * real, dentro da MEM1). Esta tabela liga as duas coisas: um acesso à memória no endereço físico 0x018xxxxx (que não
 * existe no GameCube) vai para o endereço real correspondente. O OSLink do jogo é chamado com o endereço virtual
 * (os/os_link.c), então os ponteiros que ele grava nas relocações também são virtuais e tudo fica coerente.
 *
 * O dispatcher também traduz no sentido contrário (pc real -> virtual, PLANO_PORT_C.md §2.1) para o caso de alguém
 * saltar para o código pelo endereço real. */
#ifndef AM_REL_MAP_H
#define AM_REL_MAP_H

#include "am/types.h"

/* Endereços físicos da janela: de logo depois da MEM1 até o fim do espaço de 26 bits que as DMAs enxergam. */
#define REL_MAP_PHYS_START 0x01800000U
#define REL_MAP_PHYS_END   0x04000000U
#define REL_MAP_MAX        16U

void rel_map_reset(void);

/* Liga [virt, virt+size) a [real, real+size) para o módulo `id` (substitui o mapeamento anterior do mesmo id).
 * `virt` é endereço de CPU 0x818xxxxx; `real` é endereço de CPU na MEM1. false se não cabe na janela, sobrepõe outro
 * módulo ou a tabela está cheia. */
bool rel_map_add(u32 id, GuestAddr virt, u32 size, GuestAddr real);
/* Encolhe o mapeamento do módulo (depois do OSLinkFixed as tabelas de relocação viram heap). */
bool rel_map_shrink(u32 id, u32 size);

/* Físico da janela -> físico real, se [phys, phys+size) está inteiro dentro de um módulo. */
bool rel_map_resolve(u32 phys, u32 size, u32 *real_phys);
/* pc real (dentro de um módulo ligado) -> pc virtual. */
bool rel_map_real_to_virtual(GuestAddr real, GuestAddr *virt);

typedef struct RelMapping {
    u32 id;
    GuestAddr virt;
    GuestAddr real;
    u32 size;
} RelMapping;

u32 rel_map_count(void);
const RelMapping *rel_map_at(u32 index);

#endif /* AM_REL_MAP_H */
