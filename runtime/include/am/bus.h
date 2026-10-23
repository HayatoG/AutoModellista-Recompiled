/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Barramento de hardware (MMIO). No M3 todo acesso é am_fatal; cada dispositivo entra no marco que o exige. */
#ifndef AM_BUS_H
#define AM_BUS_H

#include "am/compiler.h"
#include "am/types.h"

/* `size` em bytes: 1, 2, 4 ou 8. */
u64 bus_read(GuestAddr addr, u8 size);
void bus_write(GuestAddr addr, u64 value, u8 size);

/* Nome do bloco de hardware que ocupa `addr` (ex.: "VI"), ou "desconhecido". Estático; não liberar. */
const char *bus_region_name(GuestAddr addr);

#endif /* AM_BUS_H */
