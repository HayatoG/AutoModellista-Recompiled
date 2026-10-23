/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/chassis.h"

#include "am/bus.h"
#include "am/dispatch.h"
#include "am/exception.h"
#include "am/guest_cpu.h"
#include "am/hook.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/pi.h"
#include "am/timebase.h"
#include "am/trace.h"

#define PPC_MSR_EE 0x00008000U
#define PPC_MSR_IR 0x00000020U

/* Exceções síncronas que o C gerado levanta via ppc_take_exception (sc, programa, FP indisponível, DSI...): o SO do
 * jogo trataria pelo vetor. Só o "sc" (barreira de DCFlushRange) acontece até main; as outras são do marco que
 * precisar delas. */
static void check_sync_exception(CPUState *cpu) {
    if (cpu->exception == PPC_EXC_SYSTEM_CALL && cpu->pc == EXCEPTION_VEC_SYSCALL) {
        exception_system_call(cpu);
        return;
    }
    /* MSR[FP]=0 numa instrução de FP: o SDK troca o contexto de FPU sob demanda (OSSwitchFPUContext, número 7). */
    if (cpu->exception == PPC_EXC_FP_UNAVAILABLE && cpu->pc == EXCEPTION_VEC_FP_UNAVAILABLE) {
        exception_enter_taken(cpu, EXCEPTION_NUM_FP_UNAVAILABLE, EXCEPTION_VEC_FP_UNAVAILABLE, PPC_EXC_FP_UNAVAILABLE);
        return;
    }
    if (cpu->exception != 0U) {
        am_fatal("exceção síncrona 0x%08X (programa=0x%08X) levantada pelo código; srr0=0x%08X, vetor pc=0x%08X: "
                 "entrega não implementada (TODO(M5))",
                 cpu->exception, cpu->program_exception, cpu->srr0, cpu->pc);
    }
}

static void check_interrupts(CPUState *cpu, ChassisStats *stats) {
    if ((cpu->msr & PPC_MSR_EE) == 0U) {
        return;
    }
    /* Prioridade do 750CL (manual, tabela 4-3): externa antes do decrementer. */
    if (pi_external_pending()) {
        exception_deliver(cpu, EXCEPTION_NUM_EXTERNAL, EXCEPTION_VEC_EXTERNAL);
        stats->interrupts++;
    } else if (timebase_dec_expired(cpu)) {
        timebase_dec_take();
        exception_deliver(cpu, EXCEPTION_NUM_DECREMENTER, EXCEPTION_VEC_DECREMENTER);
        stats->interrupts++;
    }
}

/* Com MSR[IR]=0 o pc é físico (RealMode do SDK, 0x800C629C, salta assim para a função que troca os BATs). O código
 * físico em x é o mesmo da MEM1 em 0x80000000|x (mapeamento do BS2), e é lá que o dispatcher o conhece. */
static void real_mode_pc(CPUState *cpu) {
    if ((cpu->msr & PPC_MSR_IR) == 0U && cpu->pc < MEM_MEM1_SIZE) {
        cpu->pc |= MEM_CACHED_BASE;
    }
}

typedef struct Watchdog {
    GuestAddr last_pc;
    u64 same_pc;
    u64 limit;
    u64 started_ms;
} Watchdog;

/* Laço sem progresso vira am_fatal com o pc, em vez de rodar (e gravar trace) para sempre. */
static void watchdog_check(Watchdog *w, const CPUState *cpu, const ChassisConfig *config, u64 steps) {
    if (cpu->pc == w->last_pc) {
        if (++w->same_pc >= w->limit) {
            am_fatal("watchdog: pc=0x%08X despachado %llu vezes seguidas (lr=0x%08X); laço esperando algo que não vem?",
                     cpu->pc, (unsigned long long)w->same_pc, cpu->lr);
        }
    } else {
        w->last_pc = cpu->pc;
        w->same_pc = 0;
    }
    if (config->max_wall_ms != 0U && (steps & 0xFFFFU) == 0U &&
        config->now_ms() - w->started_ms > config->max_wall_ms) {
        am_fatal("watchdog: %llu ms de tempo real sem chegar à parada (pc=0x%08X, %llu voltas)",
                 (unsigned long long)config->max_wall_ms, cpu->pc, (unsigned long long)steps);
    }
}

ChassisStop chassis_run(CPUState *cpu, const ChassisConfig *config, ChassisStats *stats) {
    AM_ASSERT(config->max_wall_ms == 0U || config->now_ms != NULL, "max_wall_ms sem relógio");
    Watchdog wd = {.last_pc = 0xFFFFFFFFU,
                   .limit = config->same_pc_limit != 0U ? config->same_pc_limit : CHASSIS_DEFAULT_SAME_PC_LIMIT,
                   .started_ms = config->now_ms != NULL ? config->now_ms() : 0U};
    for (;;) {
        real_mode_pc(cpu);
        bus_update();
        check_interrupts(cpu, stats);
        (void)trace_on_dispatch(cpu);
        if (config->stop_at != 0U && cpu->pc == config->stop_at) {
            return CHASSIS_STOP_ADDRESS;
        }
        if (config->max_steps != 0U && stats->steps >= config->max_steps) {
            return CHASSIS_STOP_STEPS;
        }
        if (hook_run(cpu) == HOOK_STOP) {
            return CHASSIS_STOP_HOOK;
        }
        watchdog_check(&wd, cpu, config, stats->steps);
        timebase_begin_slice(cpu);
        dispatch_step(cpu);
        timebase_end_slice(cpu);
        check_sync_exception(cpu);
        stats->steps++;
    }
}
