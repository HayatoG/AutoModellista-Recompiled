/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Provoca um am_fatal por cenário (argv[1]); o CTest confere a mensagem. Se o cenário NÃO parar, sai com 0 e o
 * CTest falha por não achar a mensagem. */
#include <stdio.h>
#include <string.h>

#include "am/bus.h"
#include "am/chassis.h"
#include "am/dispatch.h"
#include "am/exception.h"
#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/ppc_sys.h"
#include "am/trace.h"

/* Trecho sintético que nunca avança o pc: o watchdog tem de parar. */
static void stuck(CPUState *cpu) {
    cpu->downcount -= 300;
}

static void scenario_watchdog(CPUState *cpu) {
    (void)dispatch_register(0x80010000U, 0x80010010U, stuck);
    cpu->pc = 0x80010000U;
    const ChassisConfig cfg = {.same_pc_limit = 100};
    ChassisStats st = {0};
    (void)chassis_run(cpu, &cfg, &st);
}

/* icbi sobre código protegido que foi reescrito: código gerado em tempo de execução. */
static void scenario_icbi_smc(CPUState *cpu) {
    static const u8 s_original[32] = {0x60};
    mem_write32(NULL, 0x80004000U, 0x4E800020U);
    (void)ppc_sys_protect_code(0x80004000U, 32U, s_original);
    cpu->gpr[3] = 0x80004000U;
    ppc_sys_fallback(cpu, 0x7C001FACU, cpu->pc);
}

/* BAT válido que traduz para fora do mapeamento 0x80/0xC0 -> 0. */
static void scenario_bad_bat(CPUState *cpu) {
    ppc_sys_mtspr(cpu, PPC_SPR_IBAT0U + 9U, 0x00000002U, cpu->pc);
    ppc_sys_mtspr(cpu, PPC_SPR_IBAT0U + 8U, 0x90001FFFU, cpu->pc);
}

/* Vetor 0x500 diferente do modelo. */
static void scenario_vector(CPUState *cpu) {
    const ExceptionLayout l = {0x80005000U, 0x80005008U, 0x80005004U, 0, 0, 0};
    exception_init(&l);
    mem_write32(NULL, 0x80005000U, 0x60000000U);
    mem_write32(NULL, 0x80000500U, 0x7C000000U);
    exception_deliver(cpu, 4U, 0x500U);
}

static void scenario_trace_limit(CPUState *cpu) {
    (void)trace_open("fatal_test_trace.txt", NULL, 40U);
    mem_write32(NULL, 0x80001000U, 0x48001001U);
    cpu->lr = 0x80001004U;
    cpu->pc = 0x80002000U;
    (void)trace_on_dispatch(cpu);
    (void)remove("fatal_test_trace.txt");
    (void)trace_on_dispatch(cpu);
}

int main(int argc, char **argv) {
    if (argc != 2 || !mem_init()) {
        (void)fprintf(stderr, "uso: fatal_test <cenário>\n");
        return 2;
    }
    CPUState cpu;
    (void)guest_cpu_create(&cpu);
    am_fatal_set_cpu(&cpu);
    cpu.pc = 0x80001234U;
    cpu.lr = 0x80005678U;
    const char *scenario = argv[1];

    if (strcmp(scenario, "mmio_read") == 0) {
        (void)mem_read32(&cpu, 0xCC002000U);
    } else if (strcmp(scenario, "mmio_write") == 0) {
        mem_write16(&cpu, 0xCC006C00U, 1U);
    } else if (strcmp(scenario, "unmapped_read") == 0) {
        (void)mem_read32(&cpu, 0x01000000U);
    } else if (strcmp(scenario, "spr_read") == 0) {
        (void)ppc_mfspr(&cpu, 1020U, cpu.pc);
    } else if (strcmp(scenario, "spr_write") == 0) {
        ppc_mtspr(&cpu, 1021U, 0U, cpu.pc);
    } else if (strcmp(scenario, "fallback") == 0) {
        ppc_fallback_instruction(&cpu, 0x7C00046CU, cpu.pc); /* tlbsync */
    } else if (strcmp(scenario, "no_function") == 0) {
        dispatch_step(&cpu);
    } else if (strcmp(scenario, "aram_oob") == 0) {
        (void)mem_aram_read32(MEM_ARAM_SIZE);
    } else if (strcmp(scenario, "watchdog") == 0) {
        scenario_watchdog(&cpu);
    } else if (strcmp(scenario, "icbi_smc") == 0) {
        scenario_icbi_smc(&cpu);
    } else if (strcmp(scenario, "bad_bat") == 0) {
        scenario_bad_bat(&cpu);
    } else if (strcmp(scenario, "vector") == 0) {
        scenario_vector(&cpu);
    } else if (strcmp(scenario, "trace_limit") == 0) {
        scenario_trace_limit(&cpu);
    } else if (strcmp(scenario, "dsp_mail") == 0) {
        mem_write16(&cpu, 0xCC005002U, 1U);
    } else if (strcmp(scenario, "assertion") == 0) {
        AM_ASSERT(cpu.pc == 0U, "pc=0x%08X", cpu.pc);
    }
    (void)printf("cenário '%s' não parou\n", scenario);
    return 0;
}
