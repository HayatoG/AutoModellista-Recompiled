/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Chassi: o laço que leva o pc do guest de função recompilada em função recompilada (PLANO_PORT_C.md §2).
 *
 * A cada volta: (1) entrega interrupção externa pendente se MSR[EE]; (2) grava o evento de trace; (3) para se o pc é
 * o endereço de parada; (4) chama o C gerado que cobre o pc (dispatch_step) numa fatia de tempo; (5) trata exceção
 * síncrona que o gerado tenha levantado. O C gerado devolve ao chassi em todo bl/bctrl/rfi e ao estourar o orçamento
 * de ciclos (DOLRECOMP_C_LOOP_CYCLE_BUDGET), então esses são os únicos pontos seguros para interrupções. */
#ifndef AM_CHASSIS_H
#define AM_CHASSIS_H

#include "am/types.h"

typedef enum ChassisStop {
    CHASSIS_STOP_ADDRESS, /* pc chegou em stop_at (antes de executar) */
    CHASSIS_STOP_STEPS,   /* limite de voltas */
    CHASSIS_STOP_HOOK,    /* um gancho (am/hook.h) pediu parada */
} ChassisStop;

/* Watchdog padrão: o mesmo pc despachado tantas vezes seguidas é laço sem saída (espera por hardware que não
 * responde, ou um trecho que devolve ao chassi sem avançar o pc). */
#define CHASSIS_DEFAULT_SAME_PC_LIMIT 2000000ULL

typedef struct ChassisConfig {
    GuestAddr stop_at;   /* 0 = sem parada por endereço */
    u64 max_steps;       /* 0 = sem limite */
    u64 same_pc_limit;   /* 0 = CHASSIS_DEFAULT_SAME_PC_LIMIT */
    u64 max_wall_ms;     /* 0 = sem limite de tempo real */
    u64 (*now_ms)(void); /* relógio do host (platform_ticks_ms); obrigatório se max_wall_ms != 0 */
} ChassisConfig;

typedef struct ChassisStats {
    u64 steps;
    u64 interrupts;
} ChassisStats;

ChassisStop chassis_run(CPUState *cpu, const ChassisConfig *config, ChassisStats *stats);

#endif /* AM_CHASSIS_H */
