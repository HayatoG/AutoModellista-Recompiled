/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Time base (TBL/TBU) e decrementer (DEC) do Gekko, derivados dos ciclos estimados que o C gerado desconta de
 * CPUState.downcount. Determinístico: o mesmo caminho de código dá sempre os mesmos valores.
 *
 * Fonte: o TB avança a 1/4 do clock de barramento (162 MHz / 4 = 40,5 MHz) e a CPU roda a 486 MHz, logo 12 ciclos de
 * CPU por tique (Dolphin HW/SystemTimers.h: TIMER_RATIO = 12; YAGCD 4.2.1: clocks em 0x800000F8/0x800000FC). */
#ifndef AM_TIMEBASE_H
#define AM_TIMEBASE_H

#include "am/types.h"

#define TIMEBASE_CYCLES_PER_TICK 12U
/* Tiques por segundo do TB (Dolphin Boot_BS2Emu.cpp: PresetTimeBaseTicks usa 40500000). */
#define TIMEBASE_TICKS_PER_SECOND 40500000ULL

/* Zera os contadores; o TB passa a valer `tb` e o DEC 0xFFFFFFFF (Dolphin SystemTimers.cpp: SetFakeDecStartValue). */
void timebase_reset(u64 tb);

/* Fatia de execução: o chassi zera cpu->downcount antes de chamar o gerado e consolida o que foi descontado depois. */
void timebase_begin_slice(CPUState *cpu);
void timebase_end_slice(CPUState *cpu);

/* Ciclos de CPU desde o reset, incluindo a fatia em andamento. Com cpu == NULL usa a CPU da fatia atual: é assim que
 * os dispositivos (hw/) medem o tempo sem receber o CPUState. */
u64 timebase_cycles(const CPUState *cpu);

u64 timebase_read(const CPUState *cpu);
/* mtspr 284/285 (TBL/TBU de escrita). */
void timebase_write_lower(const CPUState *cpu, u32 value);
void timebase_write_upper(const CPUState *cpu, u32 value);

/* DEC conta para baixo a cada tique do TB. */
u32 timebase_dec_read(const CPUState *cpu);
void timebase_dec_write(const CPUState *cpu, u32 value);
/* true se o DEC passou de 0 para negativo (bit 31) desde a última escrita e a exceção ainda não foi entregue: o Gekko
 * gera exceção de decrementer (fica pendente até MSR[EE]). */
bool timebase_dec_expired(const CPUState *cpu);
/* A exceção foi entregue: só volta a valer depois de outra escrita no DEC. */
void timebase_dec_take(void);
/* Ciclo em que o DEC passa de 0 (UINT64_MAX se não vai gerar exceção). Para o avanço de tempo ocioso do chassi. */
u64 timebase_dec_expiry_cycle(void);

/* M8: o TB que o jogo leu por último (mftb). É o que o stub GDB do Dolphin mostra num breakpoint (GDBStub.cpp lê o
 * SPR TL, que só o mftb atualiza), então é o que o --timebase-out grava para comparar com o oráculo. */
u64 timebase_guest_last(void);

/* M8: ciclos que passam sem o C gerado rodar (o BS2 e o apploader antes do entry point, am/boot.h). Fora de fatia. */
void timebase_advance(u64 cycles);

#endif /* AM_TIMEBASE_H */
