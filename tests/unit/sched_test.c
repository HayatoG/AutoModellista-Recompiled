/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Escalonador de threads do guest, isolado do jogo (docs/M5_OS_DISCO.md §4).
 *
 * No port não há thread de host por OSThread: o C gerado devolve ao chassi em todo desvio entre trechos e nunca
 * guarda estado na pilha do host, então a troca de contexto é a do próprio SDK (OSSaveContext/OSLoadContext + rfi),
 * recompilada. O que a runtime faz é entregar as exceções (externa, decrementer) nos pontos seguros, executar o vetor
 * copiado (os/exception.c) e o rfi. Este teste monta um "SO" sintético com o mesmo padrão do SDK:
 *
 *   - contexto atual em 0x800000C0 (físico) / 0x800000D4 (virtual), layout de OSContext;
 *   - tratador único (tabela em 0x80003000) que salva o resto dos registradores e escolhe a próxima thread;
 *   - preempção pelo decrementer (fatias pseudoaleatórias), espera por E/S com a interrupção do DI (comando Inquiry do
 *     dispositivo de verdade, hw/di.c), produtor/consumidor com seção crítica (MSR[EE] desligado) e troca cooperativa
 *     quando a thread dorme (como OSSleepThread -> __OSReschedule);
 *   - laço ocioso com EE ligado quando ninguém pode rodar (como o SelectThread do SDK).
 *
 * Cada thread guarda uma assinatura e o contador de iterações em registradores e confere a cada passo: qualquer
 * registrador perdido numa troca aparece. `sched_test deadlock` monta duas threads que esperam uma pela outra sem fonte
 * de interrupção: o chassi tem de parar com o watchdog (PASS_REGULAR_EXPRESSION no CTest), não travar. */
#include <stdio.h>
#include <string.h>

#include "am/bus.h"
#include "am/chassis.h"
#include "am/dispatch.h"
#include "am/exception.h"
#include "am/guest_cpu.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/ppc_sys.h"
#include "am/timebase.h"

#include "am_test.h"

#define TPL_START 0x80005000U
#define TPL_END   (TPL_START + 0x98U)
#define TPL_SETN  (TPL_START + 0x68U)

#define HANDLER  0x80007000U
#define SCHED    0x80008000U
#define IDLE     0x80009000U
#define THREAD0  0x80020000U
#define CTX0     0x80010000U
#define CTX_IDLE 0x80011000U
#define DI_BUF   0x80040000U

/* Estado do "SO" na memória do guest. */
#define OS_RUNNABLE 0x80030000U /* u32 por thread */
#define OS_CURRENT  0x80030040U /* índice, ou NTHREADS = ocioso */
#define OS_QUEUE    0x80030050U
#define OS_PRODUCED 0x80030054U
#define OS_CONSUMED 0x80030058U
#define OS_DI_WAIT  0x8003005CU
#define OS_DI_DONE  0x80030060U
#define OS_DI_REQS  0x80030064U
#define OS_ITER     0x80030100U /* u32 por thread */
#define OS_WAITING  0x80030200U /* u32 por thread: dormindo em quê (0 = nada) */

#define NTHREADS 4U
#define MSR_RUN  0x0000A032U /* EE | FP | IR | DR | RI */
#define MSR_EE   0x00008000U

#define WAIT_QUEUE 1U
#define WAIT_DI    2U
#define WAIT_PEER  3U

/* OSContext (SDK OSContext.h). */
#define CTX_GPR(i) ((u32)(i) * 4U)
#define CTX_CR     0x80U
#define CTX_LR     0x84U
#define CTX_CTR    0x88U
#define CTX_XER    0x8CU
#define CTX_SRR0   0x198U
#define CTX_SRR1   0x19CU

static struct {
    u32 lcg;
    u32 corruptions;
    u64 preemptions;
    u64 switches;
    u64 idle_spins;
    bool deadlock;
} s_t;

static u32 rd(GuestAddr a) {
    return mem_read32(NULL, a);
}

static void wr(GuestAddr a, u32 v) {
    mem_write32(NULL, a, v);
}

static GuestAddr ctx_of(u32 t) {
    return t < NTHREADS ? CTX0 + (t * 0x200U) : CTX_IDLE;
}

static u32 next_slice_ticks(void) {
    s_t.lcg = (s_t.lcg * 1103515245U) + 12345U;
    return 150U + ((s_t.lcg >> 8) % 1500U); /* 1800..19800 ciclos */
}

/* Põe `t` para rodar: carrega o contexto e faz rfi (o que OSLoadContext faz). */
static void load_thread(CPUState *cpu, u32 t) {
    const GuestAddr c = ctx_of(t);
    wr(0x800000C0U, c & 0x3FFFFFFFU);
    wr(0x800000D4U, c);
    wr(OS_CURRENT, t);
    for (u32 i = 0; i < 32U; i++) {
        cpu->gpr[i] = rd(c + CTX_GPR(i));
    }
    cpu->cr = rd(c + CTX_CR);
    cpu->lr = rd(c + CTX_LR);
    cpu->ctr = rd(c + CTX_CTR);
    cpu->xer = rd(c + CTX_XER);
    cpu->srr0 = rd(c + CTX_SRR0);
    cpu->srr1 = rd(c + CTX_SRR1);
    s_t.switches++;
    ppc_rfi(cpu, cpu->pc);
}

static void save_all(const CPUState *cpu, GuestAddr c, GuestAddr resume, u32 msr) {
    for (u32 i = 0; i < 32U; i++) {
        wr(c + CTX_GPR(i), cpu->gpr[i]);
    }
    wr(c + CTX_CR, cpu->cr);
    wr(c + CTX_LR, cpu->lr);
    wr(c + CTX_CTR, cpu->ctr);
    wr(c + CTX_XER, cpu->xer);
    wr(c + CTX_SRR0, resume);
    wr(c + CTX_SRR1, msr);
}

/* Próxima thread pronta depois de `from` (rodízio); NTHREADS se nenhuma. */
static u32 pick(u32 from) {
    for (u32 k = 1; k <= NTHREADS; k++) {
        const u32 t = (from + k) % NTHREADS;
        if (rd(OS_RUNNABLE + (t * 4U)) != 0U) {
            return t;
        }
    }
    return NTHREADS;
}

static void schedule(CPUState *cpu, u32 from) {
    const u32 next = pick(from);
    if (next == NTHREADS) {
        wr(OS_CURRENT, NTHREADS);
        wr(0x800000C0U, CTX_IDLE & 0x3FFFFFFFU);
        wr(0x800000D4U, CTX_IDLE);
        cpu->msr = MSR_RUN;
        cpu->pc = IDLE;
        return;
    }
    load_thread(cpu, next);
}

static void wake(u32 why) {
    for (u32 t = 0; t < NTHREADS; t++) {
        if (rd(OS_WAITING + (t * 4U)) == why) {
            wr(OS_WAITING + (t * 4U), 0);
            wr(OS_RUNNABLE + (t * 4U), 1);
        }
    }
}

/* Tratador do SO: o vetor (HLE) já salvou r3-r5, CR, LR, CTR, XER, SRR0, SRR1; r3 = número, r4 = contexto. */
static void os_handler(CPUState *cpu) {
    cpu->downcount -= 40;
    const u32 number = cpu->gpr[3];
    const GuestAddr c = cpu->gpr[4];
    for (u32 i = 0; i < 32U; i++) {
        if (i < 3U || i > 5U) {
            wr(c + CTX_GPR(i), cpu->gpr[i]);
        }
    }
    const u32 cur = rd(OS_CURRENT);
    if (number == EXCEPTION_NUM_DECREMENTER) {
        ppc_sys_mtspr(cpu, PPC_SPR_DEC, next_slice_ticks(), HANDLER);
        s_t.preemptions++;
    } else if (number == EXCEPTION_NUM_EXTERNAL) {
        if ((bus_read(0xCC006000U, 4) & 0x10U) != 0U) {
            bus_write(0xCC006000U, 0x10U | 0x08U, 4); /* apaga TCINT, mantém a máscara */
            wr(OS_DI_DONE, rd(OS_DI_DONE) + 1U);
            wake(WAIT_DI);
        }
    }
    if (cur == NTHREADS) {
        schedule(cpu, NTHREADS - 1U);
        return;
    }
    /* Preempção pelo decrementer e acordar alguém pela E/S (OSWakeupThread reescalona): rodízio. */
    schedule(cpu, cur);
}

/* Troca cooperativa (OSSleepThread): a thread já marcou que dorme e desligou EE; LR = onde continuar. */
static void os_sched(CPUState *cpu) {
    cpu->downcount -= 30;
    const u32 cur = rd(OS_CURRENT);
    save_all(cpu, ctx_of(cur), cpu->lr, cpu->msr | MSR_EE);
    schedule(cpu, cur);
}

static void os_idle(CPUState *cpu) {
    cpu->downcount -= 20;
    s_t.idle_spins++;
    if (pick(NTHREADS - 1U) != NTHREADS) {
        cpu->msr &= ~MSR_EE;
        schedule(cpu, NTHREADS - 1U);
    }
}

static void sleep_on(CPUState *cpu, u32 t, u32 why) {
    cpu->msr &= ~MSR_EE;
    wr(OS_RUNNABLE + (t * 4U), 0);
    wr(OS_WAITING + (t * 4U), why);
    cpu->lr = cpu->pc;
    cpu->pc = SCHED;
}

/* Corpo comum: confere a assinatura e o contador guardados em r20/r21. */
static bool step_common(CPUState *cpu, u32 t) {
    cpu->downcount -= 50;
    if (cpu->gpr[20] != (0xA0000000U | t) || cpu->gpr[21] != rd(OS_ITER + (t * 4U))) {
        s_t.corruptions++;
    }
    cpu->gpr[21]++;
    wr(OS_ITER + (t * 4U), cpu->gpr[21]);
    return true;
}

static void thread_producer(CPUState *cpu) {
    (void)step_common(cpu, 0);
    if (cpu->gpr[21] % 7U == 0U) {
        cpu->msr &= ~MSR_EE; /* seção crítica */
        wr(OS_QUEUE, rd(OS_QUEUE) + 1U);
        wr(OS_PRODUCED, rd(OS_PRODUCED) + 1U);
        wake(WAIT_QUEUE);
        cpu->msr |= MSR_EE;
    }
}

static void thread_consumer(CPUState *cpu) {
    (void)step_common(cpu, 1);
    cpu->msr &= ~MSR_EE;
    if (rd(OS_QUEUE) == 0U) {
        sleep_on(cpu, 1, WAIT_QUEUE);
        return;
    }
    wr(OS_QUEUE, rd(OS_QUEUE) - 1U);
    wr(OS_CONSUMED, rd(OS_CONSUMED) + 1U);
    cpu->msr |= MSR_EE;
}

/* "DVDRead": programa um Inquiry no DI e dorme até a interrupção. */
static void thread_io(CPUState *cpu) {
    (void)step_common(cpu, 2);
    bus_write(0xCC006014U, DI_BUF & 0x03FFFFFFU, 4); /* DIMAR */
    bus_write(0xCC006018U, 0x20U, 4);                /* DILENGTH */
    bus_write(0xCC006008U, 0x12000000U, 4);          /* Inquiry */
    bus_write(0xCC006000U, 0x08U, 4);                /* TCINTMASK */
    wr(OS_DI_REQS, rd(OS_DI_REQS) + 1U);
    cpu->msr &= ~MSR_EE;
    bus_write(0xCC00601CU, 1U, 4); /* TSTART */
    sleep_on(cpu, 2, WAIT_DI);
}

static void thread_spinner(CPUState *cpu) {
    (void)step_common(cpu, 3);
}

/* Cenário de impasse: as duas esperam a outra e ninguém acorda ninguém. */
static void thread_peer(CPUState *cpu) {
    const u32 t = (cpu->pc - THREAD0) / 0x1000U;
    (void)step_common(cpu, t);
    sleep_on(cpu, t, WAIT_PEER);
}

static void install_vectors(void) {
    for (GuestAddr a = TPL_START; a < TPL_END; a += 4U) {
        const u32 word = a == TPL_SETN ? 0x38600000U : 0x60000000U | (a & 0xFFFFU);
        wr(a, word);
        const u32 off = a - TPL_START;
        wr(0x80000500U + off, a == TPL_SETN ? word | EXCEPTION_NUM_EXTERNAL : word);
        wr(0x80000900U + off, a == TPL_SETN ? word | EXCEPTION_NUM_DECREMENTER : word);
    }
    wr(0x80003000U + (EXCEPTION_NUM_EXTERNAL * 4U), HANDLER);
    wr(0x80003000U + (EXCEPTION_NUM_DECREMENTER * 4U), HANDLER);
    const ExceptionLayout l = {TPL_START, TPL_END, TPL_SETN, 0x80006000U, 0x80006100U, 0x80006108U};
    exception_init(&l);
}

static void setup(CPUState *cpu, DispatchFn const *threads) {
    memset(&s_t, 0, sizeof(s_t));
    s_t.lcg = 12345U;
    EXPECT_TRUE(guest_cpu_create(cpu));
    ppc_sys_reset();
    timebase_reset(0);
    bus_reset();
    dispatch_clear();
    for (GuestAddr a = 0x80030000U; a < 0x80030400U; a += 4U) {
        wr(a, 0);
    }
    install_vectors();
    EXPECT_EQ(dispatch_register(HANDLER, HANDLER + 0x100U, os_handler), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(SCHED, SCHED + 0x100U, os_sched), DISPATCH_OK);
    EXPECT_EQ(dispatch_register(IDLE, IDLE + 0x100U, os_idle), DISPATCH_OK);
    for (u32 t = 0; t < NTHREADS; t++) {
        const GuestAddr pc = THREAD0 + (t * 0x1000U);
        EXPECT_EQ(dispatch_register(pc, pc + 0x100U, threads[t]), DISPATCH_OK);
        const GuestAddr c = ctx_of(t);
        for (u32 i = 0; i < 0x200U; i += 4U) {
            wr(c + i, 0);
        }
        wr(c + CTX_GPR(1), 0x81000000U - (t * 0x10000U));
        wr(c + CTX_GPR(20), 0xA0000000U | t);
        wr(c + CTX_SRR0, pc);
        wr(c + CTX_SRR1, MSR_RUN);
        wr(OS_RUNNABLE + (t * 4U), 1);
    }
    pi_write(PI_REG_INTMR, PI_INT_DI, 4);
    cpu->msr = 0x2032U;
    load_thread(cpu, 0);
}

TEST(stress_preemption_io_and_queue) {
    static DispatchFn const s_threads[NTHREADS] = {thread_producer, thread_consumer, thread_io, thread_spinner};
    CPUState cpu;
    setup(&cpu, s_threads);
    ppc_sys_mtspr(&cpu, PPC_SPR_DEC, next_slice_ticks(), THREAD0);
    const ChassisConfig cfg = {.max_steps = 600000U};
    ChassisStats st = {0};
    EXPECT_EQ(chassis_run(&cpu, &cfg, &st), CHASSIS_STOP_STEPS);
    (void)printf("  trocas %llu, preempções %llu, interrupções %llu, ocioso %llu, DI %u/%u, fila %u/%u\n",
                 (unsigned long long)s_t.switches, (unsigned long long)s_t.preemptions,
                 (unsigned long long)st.interrupts, (unsigned long long)s_t.idle_spins, rd(OS_DI_DONE), rd(OS_DI_REQS),
                 rd(OS_CONSUMED), rd(OS_PRODUCED));
    EXPECT_EQ(s_t.corruptions, 0U);
    for (u32 t = 0; t < NTHREADS; t++) {
        /* ninguém passa fome; a thread de E/S dá uma volta por comando do DI */
        EXPECT_TRUE(rd(OS_ITER + (t * 4U)) > (t == 2U ? 100U : 1000U));
    }
    EXPECT_TRUE(s_t.preemptions > 1000U);
    EXPECT_EQ(s_t.preemptions, exception_count_of(EXCEPTION_NUM_DECREMENTER));
    EXPECT_TRUE(rd(OS_DI_DONE) > 100U);
    /* No máximo um pedido de DI em voo e no máximo a fila em andamento. */
    EXPECT_TRUE(rd(OS_DI_REQS) - rd(OS_DI_DONE) <= 1U);
    EXPECT_EQ(rd(OS_PRODUCED) - rd(OS_CONSUMED), rd(OS_QUEUE));
    EXPECT_TRUE(rd(OS_CONSUMED) > 1000U);
}

/* Sem decrementer: só a E/S e o sono cooperativo trocam de thread, e o rodízio continua. */
TEST(cooperative_only_without_decrementer) {
    static DispatchFn const s_threads[NTHREADS] = {thread_producer, thread_consumer, thread_io, thread_spinner};
    CPUState cpu;
    setup(&cpu, s_threads);
    wr(OS_RUNNABLE + (3U * 4U), 0);
    load_thread(&cpu, 2); /* a E/S começa: sem timer, só a interrupção dela tira o produtor da CPU */
    const ChassisConfig cfg = {.max_steps = 200000U};
    ChassisStats st = {0};
    EXPECT_EQ(chassis_run(&cpu, &cfg, &st), CHASSIS_STOP_STEPS);
    EXPECT_EQ(s_t.corruptions, 0U);
    EXPECT_EQ(s_t.preemptions, 0U);
    EXPECT_TRUE(rd(OS_DI_DONE) > 10U);
    EXPECT_TRUE(st.interrupts >= rd(OS_DI_DONE));
}

static int run_deadlock(void) {
    static DispatchFn const s_threads[NTHREADS] = {thread_peer, thread_peer, thread_peer, thread_peer};
    CPUState cpu;
    setup(&cpu, s_threads);
    const ChassisConfig cfg = {.same_pc_limit = 5000U};
    ChassisStats st = {0};
    (void)chassis_run(&cpu, &cfg, &st);
    (void)printf("impasse não detectado\n");
    return 0;
}

int main(int argc, char **argv) {
    EXPECT_TRUE(mem_init());
    if (argc == 2 && strcmp(argv[1], "deadlock") == 0) {
        return run_deadlock();
    }
    RUN_TEST(stress_preemption_io_and_queue);
    RUN_TEST(cooperative_only_without_decrementer);
    dispatch_clear();
    mem_shutdown();
    return am_test_finish();
}
