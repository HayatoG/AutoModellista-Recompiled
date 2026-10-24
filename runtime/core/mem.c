/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/mem.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "am/bus.h"
#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/rel_map.h"

#define MEM_PAGE_SHIFT 12U
#define MEM_PAGE_SIZE  (1U << MEM_PAGE_SHIFT)
#define MEM_PAGES      (MEM_MEM1_SIZE >> MEM_PAGE_SHIFT)

/* M8 parte 2b: cópia na escrita (ver mem.h). Época = trecho do fluxo do GX entre dois eventos (DrawDone, token). */
typedef struct MemCow {
    bool on;
    u32 depth;                                     /* épocas que o GX pode ficar atrás da CPU (1..MAX) */
    u32 epoch;                                     /* só a CPU: época atual */
    u32 last[MEM_PAGES];                           /* só a CPU: época da última cópia (ou marca) de cada página */
    _Atomic u32 tag[MEM_COW_MAX_DEPTH][MEM_PAGES]; /* época em que a cópia da vaga foi tirada (0 = nenhuma) */
    u8 *snap[MEM_COW_MAX_DEPTH];                   /* cópias das páginas: conteúdo do fim da época anterior */
    _Atomic u32 gx_done;                           /* o GX terminou tudo até esta época */
    u32 gx_epoch;                                  /* só o GX: época do que está lendo */
    atomic_flag lock;                              /* cópia da CPU x escrita do GX na mesma página */
    _Atomic u64 copies;
} MemCow;

static struct {
    u8 *mem1;
    u8 *aram;
    u32 page_gen[MEM_PAGES];    /* contador de escritas por página de 4 KiB da MEM1 (cache de texturas do GX) */
    u32 page_gen_gx[MEM_PAGES]; /* o mesmo para as escritas do GX (cópias do EFB): só a thread do GX escreve */
    MemCow cow;
} s_mem;

static void cow_lock(void) {
    while (atomic_flag_test_and_set_explicit(&s_mem.cow.lock, memory_order_acquire)) {
    }
}

static void cow_unlock(void) {
    atomic_flag_clear_explicit(&s_mem.cow.lock, memory_order_release);
}

/* Primeira escrita da CPU numa página nesta época: guarda o conteúdo de antes para o GX, que ainda pode estar lendo
 * épocas anteriores, e só então deixa a escrita acontecer. */
static void cow_touch_slow(u32 page) {
    MemCow *c = &s_mem.cow;
    const u32 e = c->epoch;
    const u32 slot = e % c->depth;
    c->last[page] = e;
    cow_lock();
    if (atomic_load_explicit(&c->gx_done, memory_order_acquire) + 1U < e) { /* o GX ainda lê alguma época < e */
        memcpy(c->snap[slot] + ((size_t)page << MEM_PAGE_SHIFT), s_mem.mem1 + ((size_t)page << MEM_PAGE_SHIFT),
               MEM_PAGE_SIZE);
        atomic_fetch_add_explicit(&c->copies, 1U, memory_order_relaxed);
    }
    atomic_store_explicit(&c->tag[slot][page], e, memory_order_release);
    cow_unlock();
    atomic_thread_fence(memory_order_seq_cst); /* a marca fica visível antes da escrita que vem depois */
}

static inline void cow_touch(u32 page) {
    if (s_mem.cow.on && s_mem.cow.last[page] != s_mem.cow.epoch) {
        cow_touch_slow(page);
    }
}

void mem_mark_written(u32 phys, u32 size) {
    if (size == 0U || phys >= MEM_MEM1_SIZE) {
        return;
    }
    const u32 end = phys + size - 1U < MEM_MEM1_SIZE ? phys + size - 1U : MEM_MEM1_SIZE - 1U;
    for (u32 p = phys >> MEM_PAGE_SHIFT; p <= end >> MEM_PAGE_SHIFT; p++) {
        cow_touch(p);
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
        sum += (u64)s_mem.page_gen[p] + s_mem.page_gen_gx[p];
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
    mem_cow_stop();
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
    const u32 off = (u32)(p - s_mem.mem1);
    const u32 last = (off + size - 1U) >> MEM_PAGE_SHIFT; /* desalinhada, pode cair em duas páginas */
    cow_touch(off >> MEM_PAGE_SHIFT);
    if (last != off >> MEM_PAGE_SHIFT) {
        cow_touch(last);
        s_mem.page_gen[last]++;
    }
    store_be(p, size, value);
    s_mem.page_gen[off >> MEM_PAGE_SHIFT]++;
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

/* ---- M8 parte 2b: a memória vista pela thread do GX ---- */

bool mem_cow_start(u32 depth) {
    AM_ASSERT(depth >= 1U && depth <= MEM_COW_MAX_DEPTH, "mem_cow_start: profundidade %u", depth);
    MemCow *c = &s_mem.cow;
    mem_cow_stop();
    for (u32 i = 0; i < depth; i++) {
        c->snap[i] = malloc((size_t)MEM_MEM1_SIZE); /* páginas do SO só aparecem quando copiadas */
        if (c->snap[i] == NULL) {
            mem_cow_stop();
            return false;
        }
    }
    memset(c->last, 0, sizeof(c->last));
    for (u32 i = 0; i < MEM_COW_MAX_DEPTH; i++) {
        for (u32 p = 0; p < MEM_PAGES; p++) {
            atomic_init(&c->tag[i][p], 0U);
        }
    }
    atomic_init(&c->gx_done, 0U);
    atomic_init(&c->copies, 0U);
    atomic_flag_clear(&c->lock);
    c->depth = depth;
    c->epoch = 1U;
    c->gx_epoch = 1U;
    c->on = true;
    return true;
}

void mem_cow_stop(void) {
    MemCow *c = &s_mem.cow;
    c->on = false;
    for (u32 i = 0; i < MEM_COW_MAX_DEPTH; i++) {
        free(c->snap[i]);
        c->snap[i] = NULL;
    }
}

bool mem_cow_active(void) {
    return s_mem.cow.on;
}

u32 mem_cow_epoch(void) {
    return s_mem.cow.epoch;
}

void mem_cow_next_epoch(void) {
    s_mem.cow.epoch++;
}

void mem_cow_gx_done(u32 epoch) {
    atomic_store_explicit(&s_mem.cow.gx_done, epoch, memory_order_release);
}

void mem_cow_gx_epoch(u32 epoch) {
    s_mem.cow.gx_epoch = epoch;
}

u32 mem_cow_gx_current(void) {
    return s_mem.cow.on ? s_mem.cow.gx_epoch : 0U;
}

u64 mem_cow_copies(void) {
    return atomic_load_explicit(&s_mem.cow.copies, memory_order_relaxed);
}

/* Vaga com a cópia mais antiga tirada depois da época do GX (o conteúdo da página no fim dessa época), ou -1. */
static int cow_find(u32 page) {
    const MemCow *c = &s_mem.cow;
    int best = -1;
    u32 best_tag = 0;
    for (u32 i = 0; i < c->depth; i++) {
        const u32 t = atomic_load_explicit(&c->tag[i][page], memory_order_acquire);
        if (t > c->gx_epoch && (best < 0 || t < best_tag)) {
            best = (int)i;
            best_tag = t;
        }
    }
    return best;
}

/* Um pedaço dentro de uma página: da cópia, se a CPU já escreveu na página depois da época do GX; senão da memória,
 * conferindo depois da leitura que nenhuma escrita entrou no meio (a CPU marca antes de escrever). */
static void cow_read_chunk(u32 off, u8 *dst, u32 n) {
    const u32 page = off >> MEM_PAGE_SHIFT;
    int slot = cow_find(page);
    if (slot < 0) {
        memcpy(dst, s_mem.mem1 + off, n);
        atomic_thread_fence(memory_order_acquire);
        slot = cow_find(page);
    }
    if (slot >= 0) {
        memcpy(dst, s_mem.cow.snap[slot] + off, n);
    }
}

/* Cópia de [addr, addr+size) por páginas; `gx` = na visão da época do GX (com a cópia na escrita ligada). */
static void read_bytes(GuestAddr addr, u8 *dst, u32 size, bool gx) {
    while (size > 0U) {
        u32 n = MEM_PAGE_SIZE - (addr & (MEM_PAGE_SIZE - 1U));
        n = n < size ? n : size;
        const u8 *p = mem1_resolve(addr, n);
        if (p == NULL) {
            for (u32 i = 0; i < n; i++) {
                dst[i] = (u8)read_be(addr + i, 1U);
            }
        } else {
            const u32 off = (u32)(p - s_mem.mem1);
            const u32 room = MEM_PAGE_SIZE - (off & (MEM_PAGE_SIZE - 1U));
            n = n < room ? n : room;
            if (gx && s_mem.cow.on) {
                cow_read_chunk(off, dst, n);
            } else {
                memcpy(dst, p, n);
            }
        }
        addr += n;
        dst += n;
        size -= n;
    }
}

void mem_read_bytes(GuestAddr addr, u8 *dst, u32 size) {
    read_bytes(addr, dst, size, false);
}

void mem_gx_read(GuestAddr addr, u8 *dst, u32 size) {
    read_bytes(addr, dst, size, true);
}

u8 mem_gx_read8(GuestAddr addr) {
    u8 v = 0;
    mem_gx_read(addr, &v, 1U);
    return v;
}

bool mem_gx_unchanged(u32 phys, u32 size) {
    if (!s_mem.cow.on || size == 0U || phys >= MEM_MEM1_SIZE) {
        return true;
    }
    const u32 end = phys + size - 1U < MEM_MEM1_SIZE ? phys + size - 1U : MEM_MEM1_SIZE - 1U;
    atomic_thread_fence(memory_order_acquire); /* depois das leituras que o chamador quer conferir */
    for (u32 p = phys >> MEM_PAGE_SHIFT; p <= end >> MEM_PAGE_SHIFT; p++) {
        if (cow_find(p) >= 0) {
            return false;
        }
    }
    return true;
}

/* O GX escreveu [off, off+n) na memória (uma página): as cópias que épocas do GX ainda vão ler recebem o mesmo. */
static void cow_gx_mirror(u32 off, u32 n) {
    const MemCow *c = &s_mem.cow;
    const u32 page = off >> MEM_PAGE_SHIFT;
    for (u32 i = 0; i < c->depth; i++) {
        if (atomic_load_explicit(&c->tag[i][page], memory_order_acquire) > c->gx_epoch) {
            memcpy(c->snap[i] + off, s_mem.mem1 + off, n);
        }
    }
    s_mem.page_gen_gx[page]++;
}

void mem_gx_wrote(u32 phys, u32 size) {
    if (size == 0U || phys >= MEM_MEM1_SIZE) {
        return;
    }
    const u32 end = phys + size < MEM_MEM1_SIZE ? phys + size : MEM_MEM1_SIZE;
    if (s_mem.cow.on) {
        cow_lock();
    }
    for (u32 off = phys; off < end;) {
        const u32 room = MEM_PAGE_SIZE - (off & (MEM_PAGE_SIZE - 1U));
        const u32 n = end - off < room ? end - off : room;
        if (s_mem.cow.on) {
            cow_gx_mirror(off, n);
        } else {
            s_mem.page_gen_gx[off >> MEM_PAGE_SHIFT]++;
        }
        off += n;
    }
    if (s_mem.cow.on) {
        cow_unlock();
    }
}

static void gx_write(GuestAddr addr, u64 value, u8 size) {
    u8 *p = mem1_resolve(addr, size);
    if (p == NULL) {
        bus_write(addr, value, size);
        return;
    }
    store_be(p, size, value);
    mem_gx_wrote((u32)(p - s_mem.mem1), size); /* uma cópia da CPU tirada no meio da escrita é refeita no espelho */
}

void mem_gx_write8(GuestAddr addr, u8 value) {
    gx_write(addr, value, 1U);
}

void mem_gx_write16(GuestAddr addr, u16 value) {
    gx_write(addr, value, 2U);
}

void mem_gx_write32(GuestAddr addr, u32 value) {
    gx_write(addr, value, 4U);
}
