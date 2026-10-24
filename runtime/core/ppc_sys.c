/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/ppc_sys.h"

#include <string.h>

#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/timebase.h"

#define PPC_SYS_MAX_CODE_RANGES 8U
#define CACHE_BLOCK             32U

/* MSR[PR] (750CL manual 2.1.2.3). */
#define PPC_MSR_PR 0x00004000U

/* DMAL: bit T dispara a DMA do locked cache (750CL manual 9.3; Dolphin mtspr SPR_DMAL). */
#define PPC_DMAL_T 0x00000002U

typedef struct CodeRange {
    GuestAddr addr;
    u32 size;
    const u8 *original;
} CodeRange;

static struct {
    u32 spr[PPC_SPR_COUNT];
    CodeRange code[PPC_SYS_MAX_CODE_RANGES];
    u32 code_count;
    PpcSysStats stats;
} s_sys;

void ppc_sys_reset(void) {
    memset(&s_sys, 0, sizeof(s_sys));
}

u32 ppc_sys_spr(u16 spr) {
    AM_ASSERT(spr < PPC_SPR_COUNT, "SPR %u fora da faixa", spr);
    return s_sys.spr[spr];
}

void ppc_sys_set_spr(u16 spr, u32 value) {
    AM_ASSERT(spr < PPC_SPR_COUNT, "SPR %u fora da faixa", spr);
    s_sys.spr[spr] = value;
}

static bool is_bat(u16 spr) {
    return spr >= PPC_SPR_IBAT0U && spr <= PPC_SPR_DBAT3L;
}

/* A MEM1 é resolvida pelo mapeamento fixo do BS2 (mem.c): efetivo 0x80000000|x e 0xC0000000|x -> físico x. O
 * __OSInitMemoryProtection troca os BATs (DBAT0/IBAT0 de 16 MiB em 0x80000000 e DBAT2/IBAT2 de 8 MiB em 0x81000000
 * para 24 MiB), o que mantém essa tradução. Um BAT válido (Vs|Vp) que traduza diferente é am_fatal.
 * Campos: 750CL manual 7.4.3 (BEPI = bits 0-14 do U, BRPN = bits 0-14 do L, Vs/Vp = bits 30/31 do U). */
static void check_bat_pair(u16 spr, u32 cia) {
    const u16 upper = (u16)(spr & ~1U);
    const u32 u = s_sys.spr[upper];
    const u32 l = s_sys.spr[upper + 1U];
    if ((u & 3U) == 0U) {
        return; /* BAT desligado */
    }
    const u32 bepi = u & 0xFFFE0000U;
    const u32 brpn = l & 0xFFFE0000U;
    if (bepi - brpn != MEM_CACHED_BASE && bepi - brpn != MEM_UNCACHED_BASE) {
        am_fatal("BAT (SPR %u/%u) U=0x%08X L=0x%08X em 0x%08X traduz fora do mapeamento 0x80/0xC0 -> 0 suportado",
                 upper, upper + 1U, u, l, cia);
    }
}

/* SPRs que são só armazenamento: o valor escrito é o valor lido, sem efeito colateral que o port precise emular.
 * SPRG0-3 (rascunho do SO), SDR1 (tabela de páginas: o jogo roda só com BATs), MMCR/PMC (monitor de desempenho,
 * Dolphin só usa para contar), IABR/DABR (breakpoints de hardware, sem depurador), ICTC, DMAU. */
static bool is_plain_storage(u16 spr) {
    return (spr >= PPC_SPR_SPRG0 && spr < PPC_SPR_SPRG0 + 4U) || spr == PPC_SPR_SDR1 ||
           (spr >= PPC_SPR_MMCR0 && spr <= PPC_SPR_PMC4) || spr == PPC_SPR_IABR || spr == PPC_SPR_DABR ||
           spr == PPC_SPR_ICTC || spr == PPC_SPR_DMAU || spr == PPC_SPR_L2CR || spr == PPC_SPR_HID0 ||
           spr == PPC_SPR_HID1;
}

/* SPRs com campo próprio no CPUState (o C gerado e o cpu.c leem de lá). */
static bool cpu_field_read(const CPUState *cpu, u16 spr, u32 *out) {
    switch (spr) {
    case PPC_SPR_XER:
        *out = cpu->xer;
        return true;
    case PPC_SPR_LR:
        *out = cpu->lr;
        return true;
    case PPC_SPR_CTR:
        *out = cpu->ctr;
        return true;
    case PPC_SPR_DSISR:
        *out = cpu->dsisr;
        return true;
    case PPC_SPR_DAR:
        *out = cpu->dar;
        return true;
    case PPC_SPR_SRR0:
        *out = cpu->srr0;
        return true;
    case PPC_SPR_SRR1:
        *out = cpu->srr1;
        return true;
    case PPC_SPR_EAR:
        *out = cpu->ear;
        return true;
    case PPC_SPR_HID2:
        *out = cpu->hid2;
        return true;
    default:
        break;
    }
    if (spr >= PPC_SPR_GQR0 && spr < PPC_SPR_GQR0 + 8U) {
        *out = cpu->gqr[spr - PPC_SPR_GQR0];
        return true;
    }
    return false;
}

static bool cpu_field_write(CPUState *cpu, u16 spr, u32 value) {
    switch (spr) {
    case PPC_SPR_XER:
        cpu->xer = value;
        return true;
    case PPC_SPR_LR:
        cpu->lr = value;
        return true;
    case PPC_SPR_CTR:
        cpu->ctr = value;
        return true;
    case PPC_SPR_DSISR:
        cpu->dsisr = value;
        return true;
    case PPC_SPR_DAR:
        cpu->dar = value;
        return true;
    case PPC_SPR_SRR0:
        cpu->srr0 = value;
        return true;
    case PPC_SPR_SRR1:
        cpu->srr1 = value;
        return true;
    case PPC_SPR_EAR:
        cpu->ear = value;
        return true;
    case PPC_SPR_HID2:
        /* Dolphin mtspr SPR_HID2: só a metade baixa é gravável, exceto DMAQL (bits 4-7), que é somente leitura. */
        cpu->hid2 = (value & 0xF0FF0000U) | (cpu->hid2 & 0x0F000000U);
        return true;
    default:
        break;
    }
    if (spr >= PPC_SPR_GQR0 && spr < PPC_SPR_GQR0 + 8U) {
        cpu->gqr[spr - PPC_SPR_GQR0] = value;
        return true;
    }
    return false;
}

static void require_supervisor(const CPUState *cpu, const char *what, u16 spr, u32 cia) {
    if ((cpu->msr & PPC_MSR_PR) != 0U) {
        am_fatal("%s de SPR %u em modo usuário (MSR[PR]=1) em 0x%08X: exceção de programa não suportada", what, spr,
                 cia);
    }
}

u32 ppc_sys_mfspr(CPUState *cpu, u16 spr, u32 cia) {
    u32 value = 0;
    if (cpu_field_read(cpu, spr, &value)) {
        return value;
    }
    require_supervisor(cpu, "mfspr", spr, cia);
    switch (spr) {
    case PPC_SPR_DEC:
        return timebase_dec_read(cpu);
    case PPC_SPR_TBL:
        return (u32)timebase_read(cpu);
    case PPC_SPR_TBU:
        return (u32)(timebase_read(cpu) >> 32);
    case PPC_SPR_PVR:
        return PPC_GEKKO_PVR;
    case PPC_SPR_WPAR: /* bit 0 (BNE) = pipe com dados; o port consome o pipe na escrita, então sempre vazio */
    case PPC_SPR_IABR: /* Dolphin mfspr: o bit TE do IABR sempre lê 0 no hardware */
        return s_sys.spr[spr] & ~1U;
    default:
        break;
    }
    if (is_plain_storage(spr) || is_bat(spr) || spr == PPC_SPR_DMAL) {
        return s_sys.spr[spr];
    }
    am_fatal("mfspr de SPR %u desconhecido em 0x%08X", spr, cia);
}

/* Escrita com efeito colateral. Devolve false se o SPR não é desta lista. */
static bool mtspr_special(CPUState *cpu, u16 spr, u32 value, u32 cia) {
    switch (spr) {
    case PPC_SPR_DEC:
        (void)cia;
        timebase_dec_write(cpu, value);
        return true;
    case PPC_SPR_TBL_W:
        timebase_write_lower(cpu, value);
        return true;
    case PPC_SPR_TBU_W:
        timebase_write_upper(cpu, value);
        return true;
    case PPC_SPR_PVR:
        return true; /* somente leitura (Dolphin mantém o valor antigo) */
    case PPC_SPR_HID0:
        /* Dolphin mtspr SPR_HID0: ICFI (flash invalidate) se apaga sozinho; sem cache de instruções a emular. */
        s_sys.spr[spr] = value & ~PPC_HID0_ICFI;
        return true;
    case PPC_SPR_HID1:
        s_sys.spr[spr] = value & 0xF8000000U; /* Dolphin mtspr SPR_HID1: só os bits 0-4 */
        return true;
    case PPC_SPR_WPAR:
        if ((value & ~0x1FU) != PPC_WPAR_GATHER_PIPE) {
            am_fatal("mtspr WPAR=0x%08X em 0x%08X: gather pipe fora de 0x0C008000 (Dolphin também recusa)", value, cia);
        }
        s_sys.spr[spr] = value;
        return true;
    case PPC_SPR_DMAL:
        if ((value & PPC_DMAL_T) != 0U) {
            am_fatal("mtspr DMAL=0x%08X em 0x%08X: DMA do locked cache não implementada (TODO(M6))", value, cia);
        }
        s_sys.spr[spr] = value;
        return true;
    default:
        return false;
    }
}

void ppc_sys_mtspr(CPUState *cpu, u16 spr, u32 value, u32 cia) {
    if (cpu_field_write(cpu, spr, value)) {
        return;
    }
    require_supervisor(cpu, "mtspr", spr, cia);
    if (mtspr_special(cpu, spr, value, cia)) {
        return;
    }
    if (is_bat(spr)) {
        s_sys.spr[spr] = value;
        check_bat_pair(spr, cia);
        return;
    }
    if (is_plain_storage(spr)) {
        s_sys.spr[spr] = value;
        return;
    }
    am_fatal("mtspr de SPR %u (valor 0x%08X) desconhecido em 0x%08X", spr, value, cia);
}

bool ppc_sys_protect_code(GuestAddr addr, u32 size, const u8 *original) {
    if (s_sys.code_count >= PPC_SYS_MAX_CODE_RANGES || size == 0U || original == NULL) {
        return false;
    }
    s_sys.code[s_sys.code_count++] = (CodeRange){.addr = addr, .size = size, .original = original};
    return true;
}

const PpcSysStats *ppc_sys_stats(void) {
    return &s_sys.stats;
}

static void check_icbi(GuestAddr ea, u32 cia) {
    const GuestAddr block = (ea & ~(CACHE_BLOCK - 1U)) | MEM_CACHED_BASE;
    PpcSysStats *st = &s_sys.stats;
    if (st->icbi_first_count < sizeof(st->icbi_first) / sizeof(st->icbi_first[0])) {
        st->icbi_first[st->icbi_first_count++] = block;
    }
    for (u32 i = 0; i < s_sys.code_count; i++) {
        const CodeRange *r = &s_sys.code[i];
        if (block + CACHE_BLOCK <= r->addr || block >= r->addr + r->size) {
            continue;
        }
        /* Compara só a parte do bloco que cai dentro da faixa. */
        const GuestAddr lo = block > r->addr ? block : r->addr;
        const GuestAddr hi = block + CACHE_BLOCK < r->addr + r->size ? block + CACHE_BLOCK : r->addr + r->size;
        const u8 *now = mem_host_ptr(lo, hi - lo);
        if (now == NULL || memcmp(now, r->original + (lo - r->addr), hi - lo) != 0) {
            am_fatal("icbi em 0x%08X (pc=0x%08X) sobre código recompilado que mudou na MEM1: código gerado em tempo "
                     "de execução não é suportado",
                     block, cia);
        }
        st->icbi_in_code++;
        return;
    }
    st->icbi_elsewhere++;
}

void ppc_sys_cache_op(CPUState *cpu, PpcCacheOp op, GuestAddr ea, u32 cia) {
    /* A memória do host é coerente e não há JIT: dcbst/dcbf/dcbi não têm efeito observável. dcbi descarta dados
     * sujos no hardware, mas sem cache emulada não há o que descartar (Dolphin também só invalida o JIT). */
    (void)cpu;
    AM_ASSERT((u32)op < 4U, "operação de cache %u", (u32)op);
    s_sys.stats.cache_ops[op]++;
    if (op == PPC_SYS_CACHE_ICBI) {
        check_icbi(ea, cia);
    }
}

/* Campos da forma X/XFX (750CL manual cap. 12). */
#define INSN_OPCD(raw) ((raw) >> 26)
#define INSN_XO(raw)   (((raw) >> 1) & 0x3FFU)
#define INSN_RD(raw)   (((raw) >> 21) & 0x1FU)
#define INSN_RA(raw)   (((raw) >> 16) & 0x1FU)
#define INSN_RB(raw)   (((raw) >> 11) & 0x1FU)
/* O campo SPR/TBR vem com as metades de 5 bits trocadas. */
#define INSN_SPR(raw) ((u16)((((raw) >> 16) & 0x1FU) | ((((raw) >> 11) & 0x1FU) << 5)))

enum {
    XO_DCBST = 54,
    XO_DCBF = 86,
    XO_MTSR = 210,
    XO_MTSRIN = 242,
    XO_MFSPR = 339,
    XO_MFTB = 371,
    XO_MTSPR = 467,
    XO_DCBI = 470,
    XO_MFSR = 595,
    XO_MFSRIN = 659,
    XO_ICBI = 982,
};

static bool fallback_cache(CPUState *cpu, u32 raw, u32 cia) {
    PpcCacheOp op;
    switch (INSN_XO(raw)) {
    case XO_DCBST:
        op = PPC_SYS_CACHE_DCBST;
        break;
    case XO_DCBF:
        op = PPC_SYS_CACHE_DCBF;
        break;
    case XO_DCBI:
        op = PPC_SYS_CACHE_DCBI;
        break;
    case XO_ICBI:
        op = PPC_SYS_CACHE_ICBI;
        break;
    default:
        return false;
    }
    const u32 ra = INSN_RA(raw);
    const GuestAddr ea = (ra != 0U ? cpu->gpr[ra] : 0U) + cpu->gpr[INSN_RB(raw)];
    ppc_sys_cache_op(cpu, op, ea, cia);
    return true;
}

static bool fallback_segment(CPUState *cpu, u32 raw) {
    switch (INSN_XO(raw)) {
    case XO_MFSR:
        cpu->gpr[INSN_RD(raw)] = cpu->sr[INSN_RA(raw) & 0xFU];
        return true;
    case XO_MTSR:
        cpu->sr[INSN_RA(raw) & 0xFU] = cpu->gpr[INSN_RD(raw)];
        return true;
    case XO_MFSRIN:
        cpu->gpr[INSN_RD(raw)] = cpu->sr[cpu->gpr[INSN_RB(raw)] >> 28];
        return true;
    case XO_MTSRIN:
        cpu->sr[cpu->gpr[INSN_RB(raw)] >> 28] = cpu->gpr[INSN_RD(raw)];
        return true;
    default:
        return false;
    }
}

/* Contrato do C gerado: depois de ppc_fallback_instruction o trecho faz "return" para o chassi sem avançar o pc
 * (ctx->pc continua = cia), como um interpretador de uma instrução. Quem trata a instrução avança para cia + 4. */
static bool fallback_execute(CPUState *cpu, u32 raw, u32 cia) {
    if (INSN_OPCD(raw) != 31U) {
        return false;
    }
    switch (INSN_XO(raw)) {
    case XO_MFSPR:
        cpu->gpr[INSN_RD(raw)] = ppc_sys_mfspr(cpu, INSN_SPR(raw), cia);
        return true;
    case XO_MTSPR:
        ppc_sys_mtspr(cpu, INSN_SPR(raw), cpu->gpr[INSN_RD(raw)], cia);
        return true;
    case XO_MFTB:
        cpu->gpr[INSN_RD(raw)] = ppc_mftb(cpu, INSN_SPR(raw), cia);
        return true;
    default:
        return fallback_cache(cpu, raw, cia) || fallback_segment(cpu, raw);
    }
}

/* M8: ciclos que o Dolphin conta para a instrução (Source/Core/Core/PowerPC/PPCTables.cpp, num_cycles; o JIT soma
 * isso no downcount do bloco). O DolRecomp dá 0 ciclos a toda instrução de fallback (c_cfg.c: instruction_cycles), e
 * o laço do DCFlushRange (dcbf, addi, bdnz) contava 2 ciclos por linha de 32 bytes em vez de 7: nos carregamentos o
 * jogo passa a maior parte do tempo nele, e era essa a diferença de tempo emulado contra o Dolphin (docs/M8_CORRIDA.md
 * §2). */
u32 ppc_sys_fallback_cycles(u32 raw) {
    if (INSN_OPCD(raw) != 31U) {
        return 0;
    }
    switch (INSN_XO(raw)) {
    case XO_DCBST:
    case XO_DCBF:
    case XO_DCBI:
        return 5;
    case XO_ICBI:
        return 4;
    case XO_MFSR:
    case XO_MFSRIN:
        return 3;
    case XO_MTSPR:
        return 2;
    default: /* mfspr, mftb, mtsr, mtsrin */
        return 1;
    }
}

void ppc_sys_fallback(CPUState *cpu, u32 raw, u32 cia) {
    if (!fallback_execute(cpu, raw, cia)) {
        am_fatal("instrução sem recompilação nativa: opcode=0x%08X em 0x%08X", raw, cia);
    }
    /* Depois da instrução, como no JIT (o downcount do bloco é descontado na saída): um mftb aqui não se vê. */
    cpu->downcount -= (s64)ppc_sys_fallback_cycles(raw);
    cpu->pc = cia + 4U;
}
