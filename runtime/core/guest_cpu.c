/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/guest_cpu.h"

#include <string.h>

#include "am/bus.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/ppc_sys.h"

/* Os callbacks abaixo são o contrato que o C gerado e o cpu.c do DolRecomp esperam da runtime. Instruções de sistema
 * (SPR, cache) ficam em ppc_sys.c; o que ainda não existe é am_fatal. */

static u64 cb_external_read(CPUState *cpu, u32 ea, u8 size) {
    (void)cpu;
    return bus_read(ea, size);
}

static void cb_external_write(CPUState *cpu, u32 ea, u64 value, u8 size) {
    (void)cpu;
    bus_write(ea, value, size);
}

static u32 cb_external_read32(CPUState *cpu, u32 ea, u8 rid) {
    (void)cpu;
    am_fatal("external_read32 sem implementação: ea=0x%08X rid=%u", ea, rid);
}

static void cb_external_write32(CPUState *cpu, u32 ea, u32 value, u8 rid) {
    (void)cpu;
    am_fatal("external_write32 sem implementação: ea=0x%08X valor=0x%08X rid=%u", ea, value, rid);
}

/* false = "não interceptei este endereço": o gerado segue para a função recompilada. É a resposta correta enquanto
 * não há HLE (M5: OSLink, DVD, troca de contexto). O pseudo-endereço de consulta de região nativa não é usado por
 * este port; se aparecer, é bug. */
static bool cb_host_call(CPUState *cpu, u32 address) {
    (void)cpu;
    if (address == PPC_HOST_CALL_NATIVE_REGION_QUERY) {
        am_fatal("host_call: consulta de região nativa (0xFFFFFFFC) inesperada");
    }
    return false;
}

/* mfspr/mtspr pelo caminho do cpu.c (ppc_mfspr/ppc_mtspr). O C gerado deste jogo não usa esse caminho (entrega tudo
 * ao instruction_fallback), mas os dois vão ao mesmo módulo para não haver duas semânticas. */
static u32 cb_spr_read(CPUState *cpu, u16 spr, u32 cia) {
    return ppc_sys_mfspr(cpu, spr, cia);
}

static void cb_spr_write(CPUState *cpu, u16 spr, u32 value, u32 cia) {
    ppc_sys_mtspr(cpu, spr, value, cia);
}

/* PPC_CACHE_DCBST.. (cpu.h) têm a mesma numeração de PpcCacheOp. HIPÓTESE do M3 sobre os icbi: ver ppc_sys.c. */
static void cb_cache_control(CPUState *cpu, u8 operation, u32 ea, u32 cia) {
    ppc_sys_cache_op(cpu, (PpcCacheOp)operation, ea, cia);
}

bool guest_cpu_create(CPUState *cpu) {
    AM_ASSERT(mem_is_ready(), "guest_cpu_create antes de mem_init");
    memset(cpu, 0, sizeof(*cpu));
    cpu->ram = mem_mem1_data();
    cpu->ram_size = MEM_MEM1_SIZE;
    cpu->external_read = cb_external_read;
    cpu->external_write = cb_external_write;
    cpu->external_read32 = cb_external_read32;
    cpu->external_write32 = cb_external_write32;
    cpu->instruction_fallback = ppc_sys_fallback;
    cpu->host_call = cb_host_call;
    cpu->spr_read = cb_spr_read;
    cpu->spr_write = cb_spr_write;
    cpu->cache_control = cb_cache_control;
    return true;
}

void guest_cpu_destroy(CPUState *cpu) {
    memset(cpu, 0, sizeof(*cpu));
}
