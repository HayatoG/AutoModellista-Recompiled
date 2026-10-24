/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Memória do guest: MEM1 (24 MiB) e ARAM (16 MiB), sempre em big-endian.
 *
 * As funções mem_read_N e mem_write_N têm exatamente a assinatura que o C gerado chama (cpu/cpu.h do DolRecomp), por
 * isso recebem `CPUState *`. A memória é única (módulo), então código do host pode passar NULL; quando há CPU, a
 * escrita invalida a reserva de lwarx/stwcx. */
#ifndef AM_MEM_H
#define AM_MEM_H

#include "am/guest_cpu.h"
#include "am/types.h"

#define MEM_MEM1_SIZE (24U * 1024U * 1024U)
#define MEM_ARAM_SIZE (16U * 1024U * 1024U)
/* YAGCD 4.1: MEM1 em 0x80000000 (cacheado) e espelho em 0xC0000000 (não cacheado). */
#define MEM_CACHED_BASE   0x80000000U
#define MEM_UNCACHED_BASE 0xC0000000U

bool mem_init(void);
void mem_shutdown(void);
bool mem_is_ready(void);

/* Acesso à memória do guest, declarado em cpu/cpu.h (DolRecomp) porque é a assinatura que o C gerado chama:
 *   u8/u16/u32/u64 mem_read8/16/32/64(CPUState *cpu, GuestAddr addr);
 *   void mem_write8/16/32/64(CPUState *cpu, GuestAddr addr, uN value);
 * Endereços de MEM1 (0x80/0xC0) vão direto à RAM; o resto vai ao barramento (bus_read/bus_write). */

/* Início do bloco de MEM1 no host (para CPUState.ram). Dados em big-endian. */
u8 *mem_mem1_data(void);

/* Ponteiro de host para [addr, addr+size) dentro de MEM1, ou NULL. Só para cópia em bloco; nunca guardar. */
u8 *mem_host_ptr(GuestAddr addr, u32 size);

/* ARAM não é endereçável pela CPU: só por DMA/DSP. `offset` é relativo ao início da ARAM; fora do limite é fatal. */
/* Rastreio de escrita na MEM1 (M7, cache de texturas decodificadas do GX): cada página de 4 KiB tem um contador que
 * toda escrita incrementa (mem_write*, o diário do cpu.c, DMA do DVD). A soma dos contadores de uma faixa só cresce:
 * igual = nada escrito ali desde a última vez. `phys` é físico. */
void mem_mark_written(u32 phys, u32 size);
u64 mem_written_sum(u32 phys, u32 size);

u8 mem_aram_read8(u32 offset);
u16 mem_aram_read16(u32 offset);
u32 mem_aram_read32(u32 offset);
void mem_aram_write8(u32 offset, u8 value);
void mem_aram_write16(u32 offset, u16 value);
void mem_aram_write32(u32 offset, u32 value);

#endif /* AM_MEM_H */
