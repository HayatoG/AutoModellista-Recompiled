/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Provoca um am_fatal por cenário (argv[1]); o CTest confere a mensagem. Se o cenário NÃO parar, sai com 0 e o
 * CTest falha por não achar a mensagem. */
#include <stdio.h>
#include <string.h>

#include "am/bus.h"
#include "am/dispatch.h"
#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"

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
        (void)ppc_mfspr(&cpu, 1008U, cpu.pc);
    } else if (strcmp(scenario, "spr_write") == 0) {
        ppc_mtspr(&cpu, 1009U, 0U, cpu.pc);
    } else if (strcmp(scenario, "fallback") == 0) {
        ppc_fallback_instruction(&cpu, 0x7C002FACU, cpu.pc);
    } else if (strcmp(scenario, "no_function") == 0) {
        dispatch_step(&cpu);
    } else if (strcmp(scenario, "aram_oob") == 0) {
        (void)mem_aram_read32(MEM_ARAM_SIZE);
    } else if (strcmp(scenario, "assertion") == 0) {
        AM_ASSERT(cpu.pc == 0U, "pc=0x%08X", cpu.pc);
    }
    (void)printf("cenário '%s' não parou\n", scenario);
    return 0;
}
