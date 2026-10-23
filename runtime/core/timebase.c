/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/timebase.h"

#include <stdint.h>

#include "am/guest_cpu.h"

/* Estado: ciclos já consolidados + o valor do TB e do DEC no ciclo da última escrita (como o "FakeTBStartTicks" do
 * Dolphin, HW/SystemTimers.cpp: GetFakeTimeBase/GetFakeDecrementer). */
static struct {
    u64 cycles;
    u64 tb_value;
    u64 tb_cycle;
    u32 dec_value;
    u64 dec_cycle;
    bool dec_taken;  /* a exceção desta contagem já foi entregue */
    bool dec_forced; /* escrita que levou o DEC de positivo a negativo */
    bool in_slice;
    const CPUState *slice_cpu; /* para timebase_cycles(NULL) enxergar a fatia em andamento (dispositivos) */
} s_tb;

void timebase_reset(u64 tb) {
    s_tb.cycles = 0;
    s_tb.tb_value = tb;
    s_tb.tb_cycle = 0;
    s_tb.dec_value = 0xFFFFFFFFU;
    s_tb.dec_cycle = 0;
    s_tb.dec_taken = false;
    s_tb.dec_forced = false;
    s_tb.in_slice = false;
    s_tb.slice_cpu = NULL;
}

void timebase_begin_slice(CPUState *cpu) {
    cpu->downcount = 0;
    s_tb.in_slice = true;
    s_tb.slice_cpu = cpu;
}

/* O gerado só subtrai de downcount (ctx->downcount -= N); valor positivo seria escrita de outro lugar. */
static u64 slice_cycles(const CPUState *cpu) {
    if (cpu == NULL) {
        cpu = s_tb.slice_cpu;
    }
    if (cpu == NULL || !s_tb.in_slice || cpu->downcount >= 0) {
        return 0;
    }
    return (u64)(-cpu->downcount);
}

void timebase_end_slice(CPUState *cpu) {
    s_tb.cycles += slice_cycles(cpu);
    cpu->downcount = 0;
    s_tb.in_slice = false;
    s_tb.slice_cpu = NULL;
}

u64 timebase_cycles(const CPUState *cpu) {
    return s_tb.cycles + slice_cycles(cpu);
}

u64 timebase_read(const CPUState *cpu) {
    return s_tb.tb_value + ((timebase_cycles(cpu) - s_tb.tb_cycle) / TIMEBASE_CYCLES_PER_TICK);
}

static void tb_set(const CPUState *cpu, u64 value) {
    s_tb.tb_value = value;
    s_tb.tb_cycle = timebase_cycles(cpu);
}

void timebase_write_lower(const CPUState *cpu, u32 value) {
    tb_set(cpu, (timebase_read(cpu) & 0xFFFFFFFF00000000ULL) | value);
}

void timebase_write_upper(const CPUState *cpu, u32 value) {
    tb_set(cpu, ((u64)value << 32) | (timebase_read(cpu) & 0xFFFFFFFFULL));
}

static u64 dec_elapsed_ticks(const CPUState *cpu) {
    return (timebase_cycles(cpu) - s_tb.dec_cycle) / TIMEBASE_CYCLES_PER_TICK;
}

u32 timebase_dec_read(const CPUState *cpu) {
    return s_tb.dec_value - (u32)dec_elapsed_ticks(cpu);
}

/* Dolphin mtspr SPR_DEC: escrever um valor negativo sobre um positivo gera a exceção na hora. */
void timebase_dec_write(const CPUState *cpu, u32 value) {
    const bool was_positive = (timebase_dec_read(cpu) & 0x80000000U) == 0U;
    s_tb.dec_value = value;
    s_tb.dec_cycle = timebase_cycles(cpu);
    s_tb.dec_taken = false;
    s_tb.dec_forced = was_positive && (value & 0x80000000U) != 0U;
}

bool timebase_dec_expired(const CPUState *cpu) {
    if (s_tb.dec_taken) {
        return false;
    }
    if (s_tb.dec_forced) {
        return true;
    }
    if ((s_tb.dec_value & 0x80000000U) != 0U) {
        return false; /* já estava negativo: o Gekko só sinaliza na transição */
    }
    return dec_elapsed_ticks(cpu) > (u64)s_tb.dec_value;
}

void timebase_dec_take(void) {
    s_tb.dec_taken = true;
    s_tb.dec_forced = false;
}

u64 timebase_dec_expiry_cycle(void) {
    if (s_tb.dec_taken || s_tb.dec_forced || (s_tb.dec_value & 0x80000000U) != 0U) {
        return s_tb.dec_forced ? s_tb.cycles : UINT64_MAX;
    }
    return s_tb.dec_cycle + (((u64)s_tb.dec_value + 1U) * TIMEBASE_CYCLES_PER_TICK);
}

/* mftb do C gerado (cpu/cpu.h). O cpu.c do DolRecomp tem a própria versão, que lê cpu->timebase (nunca avança);
 * no alvo am_ppc ela é renomeada (runtime/CMakeLists.txt) e esta toma o lugar. TBR 268 = TBL, 269 = TBU. */
u32 ppc_mftb(CPUState *cpu, u16 tbr, u32 cia) {
    const u64 tb = timebase_read(cpu);
    if (tbr == 268U) {
        return (u32)tb;
    }
    if (tbr == 269U) {
        return (u32)(tb >> 32);
    }
    ppc_program_exception(cpu, PPC_PROGRAM_ILLEGAL, cia);
    return 0;
}
