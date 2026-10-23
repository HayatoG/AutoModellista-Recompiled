/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Instruções de sistema que o C gerado entrega à runtime (instruction_fallback): mfspr/mtspr, mfsr/mtsr e as
 * operações de cache dcbst/dcbf/dcbi/icbi. Também guarda os SPRs que não têm campo no CPUState.
 *
 * O DolRecomp (commit de scripts/fetch_tools.sh) emite TODO mfspr/mtspr e operação de cache deste jogo como
 * ppc_fallback_instruction(ctx, palavra, endereço): são 310 sítios no DOL, nenhum ppc_mfspr nem ppc_cache_control.
 * Semântica de cada SPR: PowerPC 750CL User's Manual (cap. 2) e Dolphin
 * Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp (mfspr/mtspr), tag 2609a. */
#ifndef AM_PPC_SYS_H
#define AM_PPC_SYS_H

#include "am/types.h"

/* Números de SPR (750CL manual, tabela 2-3; Dolphin Core/PowerPC/Gekko.h). */
#define PPC_SPR_XER    1U
#define PPC_SPR_LR     8U
#define PPC_SPR_CTR    9U
#define PPC_SPR_DSISR  18U
#define PPC_SPR_DAR    19U
#define PPC_SPR_DEC    22U
#define PPC_SPR_SDR1   25U
#define PPC_SPR_SRR0   26U
#define PPC_SPR_SRR1   27U
#define PPC_SPR_TBL    268U
#define PPC_SPR_TBU    269U
#define PPC_SPR_SPRG0  272U
#define PPC_SPR_EAR    282U
#define PPC_SPR_TBL_W  284U
#define PPC_SPR_TBU_W  285U
#define PPC_SPR_PVR    287U
#define PPC_SPR_IBAT0U 528U /* 528..535 IBAT0U..IBAT3L, 536..543 DBAT0U..DBAT3L */
#define PPC_SPR_DBAT3L 543U
#define PPC_SPR_GQR0   912U
#define PPC_SPR_HID2   920U
#define PPC_SPR_WPAR   921U
#define PPC_SPR_DMAU   922U
#define PPC_SPR_DMAL   923U
#define PPC_SPR_UMMCR0 936U /* 936..942: espelhos de usuário do monitor de desempenho */
#define PPC_SPR_UPMC4  942U
#define PPC_SPR_MMCR0  952U
#define PPC_SPR_PMC1   953U
#define PPC_SPR_PMC2   954U
#define PPC_SPR_SIA    955U
#define PPC_SPR_MMCR1  956U
#define PPC_SPR_PMC3   957U
#define PPC_SPR_PMC4   958U
#define PPC_SPR_HID0   1008U
#define PPC_SPR_HID1   1009U
#define PPC_SPR_IABR   1010U
#define PPC_SPR_DABR   1013U
#define PPC_SPR_L2CR   1017U
#define PPC_SPR_ICTC   1019U

#define PPC_SPR_COUNT 1024U

/* Bits de HID0 (750CL manual 2.1.2.1). */
#define PPC_HID0_ICFI 0x00000800U

/* Endereço físico do write-gather pipe (Dolphin GPFifo.h: GATHER_PIPE_PHYSICAL_ADDRESS). */
#define PPC_WPAR_GATHER_PIPE 0x0C008000U

/* Operações de cache, na mesma numeração do cpu.h (PPC_CACHE_DCBST...). */
typedef enum PpcCacheOp {
    PPC_SYS_CACHE_DCBST,
    PPC_SYS_CACHE_DCBF,
    PPC_SYS_CACHE_DCBI,
    PPC_SYS_CACHE_ICBI,
} PpcCacheOp;

typedef struct PpcSysStats {
    u64 cache_ops[4];   /* por PpcCacheOp */
    u64 icbi_in_code;   /* icbi sobre uma faixa de código protegida (ppc_sys_protect_code), conteúdo intacto */
    u64 icbi_elsewhere; /* icbi fora de toda faixa protegida (ex.: vetores de exceção em 0x80000x00) */
    GuestAddr icbi_first[8];
    u32 icbi_first_count;
} PpcSysStats;

void ppc_sys_reset(void);

/* Valor cru de um SPR guardado aqui (os que vivem no CPUState não passam por este módulo). */
u32 ppc_sys_spr(u16 spr);
void ppc_sys_set_spr(u16 spr, u32 value);

u32 ppc_sys_mfspr(CPUState *cpu, u16 spr, u32 cia);
void ppc_sys_mtspr(CPUState *cpu, u16 spr, u32 value, u32 cia);

/* Faixa de código recompilado e o conteúdo original (o buffer do DOL, que precisa viver enquanto o jogo roda). Um
 * icbi num bloco de 32 bytes dessa faixa cujo conteúdo na MEM1 difere do original é código gerado em tempo de
 * execução, que o port não suporta: am_fatal. */
bool ppc_sys_protect_code(GuestAddr addr, u32 size, const u8 *original);
void ppc_sys_cache_op(CPUState *cpu, PpcCacheOp op, GuestAddr ea, u32 cia);
const PpcSysStats *ppc_sys_stats(void);

/* instruction_fallback do CPUState: decodifica `raw` (em `cia`) e executa; o que não é suportado é am_fatal. */
void ppc_sys_fallback(CPUState *cpu, u32 raw, u32 cia);

#endif /* AM_PPC_SYS_H */
