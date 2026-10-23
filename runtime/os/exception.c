/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/exception.h"

#include <string.h>

#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/ppc_sys.h"

/* Globais do SO em memória baixa (YAGCD 4.2.1; SDK OSContext.h): */
#define OS_CURRENT_CONTEXT_PADDR 0x800000C0U /* endereço FÍSICO do OSContext atual */
#define OS_CURRENT_CONTEXT       0x800000D4U /* endereço virtual do mesmo */
#define OS_EXCEPTION_TABLE       0x80003000U /* tratador por número de exceção (4 bytes cada) */

/* Deslocamentos em OSContext (SDK OSContext.h; conferidos nos stw do modelo em 0x800C2C94..0x800C2CD4). */
#define OS_CONTEXT_R3        0x00CU
#define OS_CONTEXT_R4        0x010U
#define OS_CONTEXT_R5        0x014U
#define OS_CONTEXT_CR        0x080U
#define OS_CONTEXT_LR        0x084U
#define OS_CONTEXT_CTR       0x088U
#define OS_CONTEXT_XER       0x08CU
#define OS_CONTEXT_SRR0      0x198U
#define OS_CONTEXT_SRR1      0x19CU
#define OS_CONTEXT_STATE     0x1A2U
#define OS_CONTEXT_STATE_EXC 0x0002U

#define PPC_MSR_RI 0x00000002U

static struct {
    ExceptionLayout layout;
    bool ready;
    u64 delivered;
    u64 system_calls;
    u64 by_number[16];
} s_exc;

void exception_init(const ExceptionLayout *layout) {
    s_exc.layout = *layout;
    s_exc.ready = true;
    s_exc.delivered = 0;
    s_exc.system_calls = 0;
    memset(s_exc.by_number, 0, sizeof(s_exc.by_number));
}

u64 exception_count_of(u32 number) {
    return number < 16U ? s_exc.by_number[number] : 0U;
}

u64 exception_delivered_count(void) {
    return s_exc.delivered;
}

/* O vetor em `vector` tem de ser cópia exata do modelo, com o número no "li r3,N". */
static void verify_vector(u32 number, u32 vector) {
    const ExceptionLayout *l = &s_exc.layout;
    const GuestAddr dst = MEM_CACHED_BASE | vector;
    for (GuestAddr src = l->ev_start; src < l->ev_end; src += 4U) {
        const u32 expected = mem_read32(NULL, src) | (src == l->ev_set_number ? number : 0U);
        const u32 actual = mem_read32(NULL, dst + (src - l->ev_start));
        if (actual != expected) {
            am_fatal("vetor de exceção 0x%X: palavra em +0x%X é 0x%08X, modelo __OSEVStart (0x%08X) dá 0x%08X; HLE do "
                     "vetor não se aplica",
                     vector, src - l->ev_start, actual, l->ev_start, expected);
        }
    }
}

/* CR0 de uma instrução com ponto (rlwinm.): LT/GT/EQ pelo resultado com sinal, SO copiado do XER. */
static void set_cr0(CPUState *cpu, u32 result) {
    u32 bits = 0x2U; /* EQ */
    if ((s32)result < 0) {
        bits = 0x8U;
    } else if (result != 0U) {
        bits = 0x4U;
    }
    bits |= (cpu->xer >> 31) & 1U;
    cpu->cr = (cpu->cr & 0x0FFFFFFFU) | (bits << 28);
}

/* Tradução, instrução a instrução, do modelo __OSEVStart deste DOL (0x800C2C8C..0x800C2D24, disassembly em
 * docs/M4_BOOT.md §3.4). Com MSR[DR]=0 os endereços são físicos; o mapeamento padrão do BS2 (BAT 0x80000000 ->
 * 0x00000000) dá o endereço virtual somando 0x80000000. */
static void run_vector_stub(CPUState *cpu, u32 number, u32 vector) {
    ppc_sys_set_spr(PPC_SPR_SPRG0, cpu->gpr[4]);                                        /* mtsprg 0,r4 */
    const GuestAddr ctx = mem_read32(NULL, OS_CURRENT_CONTEXT_PADDR) | MEM_CACHED_BASE; /* lwz r4,0xC0(0) */
    mem_write32(cpu, ctx + OS_CONTEXT_R3, cpu->gpr[3]);
    mem_write32(cpu, ctx + OS_CONTEXT_R4, ppc_sys_spr(PPC_SPR_SPRG0)); /* mfsprg r3,0; stw */
    mem_write32(cpu, ctx + OS_CONTEXT_R5, cpu->gpr[5]);
    mem_write16(cpu, ctx + OS_CONTEXT_STATE, (u16)(mem_read16(NULL, ctx + OS_CONTEXT_STATE) | OS_CONTEXT_STATE_EXC));
    mem_write32(cpu, ctx + OS_CONTEXT_CR, cpu->cr);
    mem_write32(cpu, ctx + OS_CONTEXT_LR, cpu->lr);
    mem_write32(cpu, ctx + OS_CONTEXT_CTR, cpu->ctr);
    mem_write32(cpu, ctx + OS_CONTEXT_XER, cpu->xer);
    mem_write32(cpu, ctx + OS_CONTEXT_SRR0, cpu->srr0);
    mem_write32(cpu, ctx + OS_CONTEXT_SRR1, cpu->srr1);
    cpu->gpr[5] = cpu->srr1; /* mr r5,r3 */
    /* __DBVECTOR: nop (conferido em verify_vector). */
    cpu->gpr[3] = cpu->msr | 0x30U;                     /* mfmsr; ori r3,r3,0x30 */
    cpu->srr1 = cpu->gpr[3];                            /* mtsrr1 r3: IR|DR no rfi */
    cpu->gpr[3] = number;                               /* li r3,N (remendado) */
    cpu->gpr[4] = mem_read32(NULL, OS_CURRENT_CONTEXT); /* lwz r4,0xD4(0) */
    cpu->gpr[5] &= PPC_MSR_RI;                          /* rlwinm. r5,r5,0,30,30 */
    set_cr0(cpu, cpu->gpr[5]);
    if (cpu->gpr[5] == 0U) {
        cpu->gpr[5] = s_exc.layout.default_handler; /* lis/addi r5; mtsrr0 */
    } else {
        cpu->gpr[5] = mem_read32(NULL, OS_EXCEPTION_TABLE + (number << 2)); /* rlwinm; lwz r5,0x3000(r5) */
    }
    cpu->srr0 = cpu->gpr[5];
    cpu->pc = vector;
    ppc_rfi(cpu, MEM_CACHED_BASE | vector); /* rfi: pc = SRR0, MSR = SRR1 */
}

void exception_deliver(CPUState *cpu, u32 number, u32 vector) {
    AM_ASSERT(s_exc.ready, "exception_deliver antes de exception_init");
    verify_vector(number, vector);
    /* Mesma transição de MSR/SRR0/SRR1 do 750CL que o cpu.c aplica às exceções síncronas. */
    ppc_take_exception(cpu, 0U, vector, cpu->pc, 0U);
    run_vector_stub(cpu, number, vector);
    s_exc.delivered++;
    s_exc.by_number[number & 15U]++;
}

void exception_enter_taken(CPUState *cpu, u32 number, u32 vector, u32 exc_bit) {
    AM_ASSERT(s_exc.ready, "exception_enter_taken antes de exception_init");
    AM_ASSERT(cpu->pc == vector, "exceção %u tomada com pc=0x%08X, vetor 0x%X", number, cpu->pc, vector);
    verify_vector(number, vector);
    cpu->exception &= ~exc_bit;
    run_vector_stub(cpu, number, vector);
    s_exc.delivered++;
    s_exc.by_number[number & 15U]++;
}

u64 exception_system_call_count(void) {
    return s_exc.system_calls;
}

/* Tradução do modelo em GAME_ADDR_OS_SC_START (7 instruções; docs/M4_BOOT.md §3.4):
 *   mfhid0 r9; ori r10,r9,8; mthid0 r10; isync; sync; mthid0 r9; rfi
 * HID0 bit 0x8 (ABE) só muda o broadcast de operações de cache no barramento: sem efeito no port, mas r9/r10 e HID0
 * ficam como no hardware. */
void exception_system_call(CPUState *cpu) {
    AM_ASSERT(s_exc.ready, "exception_system_call antes de exception_init");
    const ExceptionLayout *l = &s_exc.layout;
    const GuestAddr dst = MEM_CACHED_BASE | EXCEPTION_VEC_SYSCALL;
    for (GuestAddr src = l->sc_start; src < l->sc_end; src += 4U) {
        const u32 expected = mem_read32(NULL, src);
        const u32 actual = mem_read32(NULL, dst + (src - l->sc_start));
        if (actual != expected) {
            am_fatal("vetor de system call: palavra em +0x%X é 0x%08X, modelo (0x%08X) dá 0x%08X; HLE não se aplica",
                     src - l->sc_start, actual, l->sc_start, expected);
        }
    }
    const u32 cia = cpu->pc;
    cpu->gpr[9] = ppc_sys_mfspr(cpu, PPC_SPR_HID0, cia);
    cpu->gpr[10] = cpu->gpr[9] | 8U;
    ppc_sys_mtspr(cpu, PPC_SPR_HID0, cpu->gpr[10], cia);
    ppc_sys_mtspr(cpu, PPC_SPR_HID0, cpu->gpr[9], cia);
    cpu->exception &= ~(u32)PPC_EXC_SYSTEM_CALL;
    ppc_rfi(cpu, dst + 0x18U);
    s_exc.system_calls++;
}
