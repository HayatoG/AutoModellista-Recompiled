/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Barramento de hardware (MMIO em 0xCC000000). Cada bloco que já tem dispositivo é despachado para ele; bloco ou
 * registrador sem implementação é am_fatal (PADROES_DE_CODIGO.md §5). */
#ifndef AM_BUS_H
#define AM_BUS_H

#include "am/compiler.h"
#include "am/types.h"

/* Endereço visto pela CPU de um registrador físico 0x0Cxxxxxx (para mensagens). */
#define BUS_CPU_ADDR(phys) ((phys) | 0xC0000000U)

/* `size` em bytes: 1, 2, 4 ou 8. */
u64 bus_read(GuestAddr addr, u8 size);
void bus_write(GuestAddr addr, u64 value, u8 size);

/* Nome do bloco de hardware que ocupa `addr` (ex.: "VI"), ou "desconhecido". Estático; não liberar. */
const char *bus_region_name(GuestAddr addr);

/* Usadas pelos dispositivos para registrador que ainda não existe: mesma mensagem fatal do barramento. */
AM_NORETURN void bus_unhandled_read(GuestAddr addr, u8 size);
AM_NORETURN void bus_unhandled_write(GuestAddr addr, u64 value, u8 size);

/* Reinicia todos os dispositivos (estado de power-on). */
void bus_reset(void);

/* Conclui eventos de dispositivo com prazo vencido (DMA, comandos). O chassi chama a cada volta. */
void bus_update(void);

#endif /* AM_BUS_H */
