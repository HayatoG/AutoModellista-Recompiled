/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/mem.h"

#include <stdlib.h>

#include "am/bus.h"
#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/rel_map.h"

#define MEM_PAGE_SHIFT 12U
#define MEM_PAGES      (MEM_MEM1_SIZE >> MEM_PAGE_SHIFT)

static struct {
    u8 *mem1;
    u8 *aram;
    u32 page_gen[MEM_PAGES]; /* contador de escritas por página de 4 KiB da MEM1 (cache de texturas do GX) */
} s_mem;

void mem_mark_written(u32 phys, u32 size) {
    if (size == 0U || phys >= MEM_MEM1_SIZE) {
        return;
    }
    const u32 end = phys + size - 1U < MEM_MEM1_SIZE ? phys + size - 1U : MEM_MEM1_SIZE - 1U;
    for (u32 p = phys >> MEM_PAGE_SHIFT; p <= end >> MEM_PAGE_SHIFT; p++) {
        s_mem.page_gen[p]++;
    }
}

u64 mem_written_sum(u32 phys, u32 size) {
    u64 sum = 0;
    if (size == 0U || phys >= MEM_MEM1_SIZE) {
        return 0;
    }
    const u32 end = phys + size - 1U < MEM_MEM1_SIZE ? phys + size - 1U : MEM_MEM1_SIZE - 1U;
    for (u32 p = phys >> MEM_PAGE_SHIFT; p <= end >> MEM_PAGE_SHIFT; p++) {
        sum += s_mem.page_gen[p];
    }
    return sum;
}

/* Escritas que o cpu.c do DolRecomp faz direto na RAM (psq_st, stfd...) chegam por este diário. */
static void journal(u32 offset, u32 size, void *user) {
    (void)user;
    mem_mark_written(offset, size);
}

bool mem_init(void) {
    if (s_mem.mem1 != NULL) {
        return true;
    }
    s_mem.mem1 = calloc((size_t)MEM_MEM1_SIZE, 1);
    s_mem.aram = calloc((size_t)MEM_ARAM_SIZE, 1);
    if (s_mem.mem1 == NULL || s_mem.aram == NULL) {
        mem_shutdown();
        return false;
    }
    ppc_set_mem_write_journal(journal, NULL);
    return true;
}

void mem_shutdown(void) {
    free(s_mem.mem1);
    free(s_mem.aram);
    s_mem.mem1 = NULL;
    s_mem.aram = NULL;
}

bool mem_is_ready(void) {
    return s_mem.mem1 != NULL;
}

u8 *mem_mem1_data(void) {
    return s_mem.mem1;
}

/* Traduz endereço de CPU para MEM1. 0x80xxxxxx e 0xC0xxxxxx são o mesmo bloco físico (BAT padrão do SDK). Físico
 * entre 24 e 64 MiB é a janela virtual dos RELs (core/rel_map.c). O resto (MMIO em 0xCC..., cache bloqueado em
 * 0xE0...) devolve NULL e vai ao barramento. */
static u8 *mem1_resolve(GuestAddr addr, u32 size) {
    if ((addr >> 30) < 2U) {
        return NULL;
    }
    u32 phys = addr & 0x3FFFFFFFU;
    if (phys > MEM_MEM1_SIZE - size) {
        if (phys < REL_MAP_PHYS_START || phys >= REL_MAP_PHYS_END || !rel_map_resolve(phys, size, &phys)) {
            return NULL;
        }
    }
    return s_mem.mem1 + phys;
}

u8 *mem_host_ptr(GuestAddr addr, u32 size) {
    AM_ASSERT(size > 0U && size <= MEM_MEM1_SIZE, "mem_host_ptr: tamanho inválido %u", size);
    return mem1_resolve(addr, size);
}

/* Mesma regra de cpu.c (DolRecomp): uma escrita no bloco de 32 bytes reservado cancela o lwarx pendente. */
static void clear_reservation(CPUState *cpu, GuestAddr addr) {
    if (cpu != NULL && cpu->reserve_valid) {
        const u32 reserved = cpu->reserve_addr & ~0x40000000U;
        const u32 written = addr & ~0x40000000U;
        if (((reserved ^ written) & ~31U) == 0U) {
            cpu->reserve_valid = false;
        }
    }
}

static u64 load_be(const u8 *p, u32 size) {
    u64 value = 0;
    for (u32 i = 0; i < size; i++) {
        value = (value << 8) | p[i];
    }
    return value;
}

static void store_be(u8 *p, u32 size, u64 value) {
    for (u32 i = size; i > 0U; i--) {
        p[i - 1U] = (u8)(value & 0xFFU);
        value >>= 8;
    }
}

static u64 read_be(GuestAddr addr, u8 size) {
    const u8 *p = mem1_resolve(addr, size);
    if (p == NULL) {
        return bus_read(addr, size);
    }
    return load_be(p, size);
}

static void write_be(CPUState *cpu, GuestAddr addr, u64 value, u8 size) {
    u8 *p = mem1_resolve(addr, size);
    if (p == NULL) {
        bus_write(addr, value, size);
        return;
    }
    clear_reservation(cpu, addr);
    store_be(p, size, value);
    s_mem.page_gen[(u32)(p - s_mem.mem1) >> MEM_PAGE_SHIFT]++; /* size <= 8 e alinhado ao tipo: uma página só */
}

u8 mem_read8(CPUState *cpu, GuestAddr addr) {
    (void)cpu;
    return (u8)read_be(addr, 1U);
}

u16 mem_read16(CPUState *cpu, GuestAddr addr) {
    (void)cpu;
    return (u16)read_be(addr, 2U);
}

u32 mem_read32(CPUState *cpu, GuestAddr addr) {
    (void)cpu;
    return (u32)read_be(addr, 4U);
}

u64 mem_read64(CPUState *cpu, GuestAddr addr) {
    (void)cpu;
    return read_be(addr, 8U);
}

void mem_write8(CPUState *cpu, GuestAddr addr, u8 value) {
    write_be(cpu, addr, value, 1U);
}

void mem_write16(CPUState *cpu, GuestAddr addr, u16 value) {
    write_be(cpu, addr, value, 2U);
}

void mem_write32(CPUState *cpu, GuestAddr addr, u32 value) {
    write_be(cpu, addr, value, 4U);
}

void mem_write64(CPUState *cpu, GuestAddr addr, u64 value) {
    write_be(cpu, addr, value, 8U);
}

static u8 *aram_resolve(u32 offset, u32 size) {
    if (offset > MEM_ARAM_SIZE - size) {
        am_fatal("ARAM: acesso de %u byte(s) em 0x%08X fora dos %u MiB", size, offset, MEM_ARAM_SIZE >> 20);
    }
    return s_mem.aram + offset;
}

u8 mem_aram_read8(u32 offset) {
    return (u8)load_be(aram_resolve(offset, 1U), 1U);
}

u16 mem_aram_read16(u32 offset) {
    return (u16)load_be(aram_resolve(offset, 2U), 2U);
}

u32 mem_aram_read32(u32 offset) {
    return (u32)load_be(aram_resolve(offset, 4U), 4U);
}

void mem_aram_write8(u32 offset, u8 value) {
    store_be(aram_resolve(offset, 1U), 1U, value);
}

void mem_aram_write16(u32 offset, u16 value) {
    store_be(aram_resolve(offset, 2U), 2U, value);
}

void mem_aram_write32(u32 offset, u32 value) {
    store_be(aram_resolve(offset, 4U), 4U, value);
}
