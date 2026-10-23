/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Entrega de exceções ao SO do jogo (HLE do vetor).
 *
 * No hardware a exceção desvia para 0x00000500 (externa) etc., onde o OSExceptionInit do SDK copiou um trecho de ~38
 * instruções (__OSEVStart..__OSEVEnd) com o número da exceção remendado no "li r3,N". Esse trecho roda com tradução de
 * endereço desligada e não existe no C gerado (é cópia feita em tempo de execução). Aqui ele é executado em C,
 * instrução por instrução (ver exception.c), DEPOIS de conferir que a cópia na MEM1 é idêntica ao modelo do DOL a
 * menos do número: se o jogo pôs outra coisa no vetor, am_fatal. O tratador em C do SO (ex.: ExternalInterruptHandler)
 * é código do DOL e roda recompilado. */
#ifndef AM_EXCEPTION_H
#define AM_EXCEPTION_H

#include "am/types.h"

/* Números de exceção do SDK (OSException.h, __OSException) e vetores do 750CL (manual, tabela 4-2). */
#define EXCEPTION_NUM_EXTERNAL       4U
#define EXCEPTION_VEC_EXTERNAL       0x500U
#define EXCEPTION_NUM_FP_UNAVAILABLE 7U
#define EXCEPTION_VEC_FP_UNAVAILABLE 0x800U
#define EXCEPTION_NUM_DECREMENTER    8U
#define EXCEPTION_VEC_DECREMENTER    0x900U
#define EXCEPTION_VEC_SYSCALL        0xC00U

/* Onde está o modelo copiado pelo OSExceptionInit; endereços específicos do binário (am/game.h). */
typedef struct ExceptionLayout {
    GuestAddr ev_start;        /* __OSEVStart */
    GuestAddr ev_end;          /* __OSEVEnd (exclusivo) */
    GuestAddr ev_set_number;   /* instrução "li r3,0" que recebe o número */
    GuestAddr default_handler; /* OSDefaultExceptionHandler, destino quando MSR[RI]=0 */
    GuestAddr sc_start;        /* tratador de system call copiado para 0xC00 (__OSInitSystemCall) */
    GuestAddr sc_end;
} ExceptionLayout;

void exception_init(const ExceptionLayout *layout);

/* Interrompe a CPU no ponto atual (cpu->pc é a próxima instrução, vira SRR0) e leva até o tratador do SO. */
void exception_deliver(CPUState *cpu, u32 number, u32 vector);

/* Exceção síncrona que o C gerado já tomou (ppc_take_exception: SRR0/SRR1/MSR prontos, pc no vetor), por exemplo FP
 * indisponível (troca preguiçosa do contexto de FPU do SDK): executa o vetor e limpa `exc_bit` de cpu->exception. */
void exception_enter_taken(CPUState *cpu, u32 number, u32 vector, u32 exc_bit);

/* "sc" já tomado pelo C gerado (cpu->exception tem PPC_EXC_SYSTEM_CALL, pc = 0xC00): executa o tratador do SDK em
 * 0x80000C00 (conferido contra o modelo do DOL) e volta para SRR0. */
void exception_system_call(CPUState *cpu);

u64 exception_delivered_count(void);
/* Entregas por número de exceção do SDK (0..14). */
u64 exception_count_of(u32 number);
u64 exception_system_call_count(void);

#endif /* AM_EXCEPTION_H */
