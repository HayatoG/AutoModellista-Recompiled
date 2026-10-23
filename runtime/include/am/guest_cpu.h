/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CPUState do DolRecomp ligado à runtime: memória, MMIO e callbacks de sistema. */
#ifndef AM_GUEST_CPU_H
#define AM_GUEST_CPU_H

#include "am/types.h"

#include "cpu/cpu.h"

/* PVR do Gekko lido por mfspr 287 (PPC_GEKKO_PVR no cpu.h). */
#define GUEST_CPU_SPR_PVR 287U

/* Zera `cpu` e liga ram/ram_size e todos os callbacks. Exige mem_init() antes. */
bool guest_cpu_create(CPUState *cpu);
void guest_cpu_destroy(CPUState *cpu);

#endif /* AM_GUEST_CPU_H */
