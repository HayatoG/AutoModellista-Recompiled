/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/os_threads.h"

#include <stdio.h>

#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"

#define OS_ACTIVE_HEAD     0x800000DCU
#define OS_CURRENT_THREAD  0x800000E4U
#define OS_CTX_GPR1        0x004U
#define OS_CTX_LR          0x084U
#define OS_CTX_SRR0        0x198U
#define OS_THREAD_STATE    0x2C8U
#define OS_THREAD_SUSPEND  0x2CCU
#define OS_THREAD_PRIORITY 0x2D0U
#define OS_THREAD_ACTIVE   0x2FCU /* linkActive.next */
#define OS_THREADS_MAX     64U

static bool in_mem1(GuestAddr a) {
    return a >= MEM_CACHED_BASE && a < MEM_CACHED_BASE + MEM_MEM1_SIZE - 8U;
}

static void back_chain(GuestAddr sp, u32 depth) {
    char line[256];
    int len = 0;
    for (u32 i = 0; i < depth && in_mem1(sp); i++) {
        const GuestAddr next = mem_read32(NULL, sp);
        if (!in_mem1(next) || next <= sp) {
            break;
        }
        const int n = snprintf(line + len, sizeof(line) - (size_t)len, " %08X", mem_read32(NULL, next + 4U));
        if (n < 0 || (size_t)len + (size_t)n >= sizeof(line)) {
            break;
        }
        len += n;
        sp = next;
    }
    AM_LOG_INFO("      retornos:%s", len > 0 ? line : " (nenhum)");
}

void os_threads_dump(const CPUState *cpu, u32 depth) {
    const GuestAddr current = mem_read32(NULL, OS_CURRENT_THREAD);
    AM_LOG_INFO("threads (atual 0x%08X; pc=0x%08X lr=0x%08X sp=0x%08X):", current, cpu->pc, cpu->lr, cpu->gpr[1]);
    GuestAddr t = mem_read32(NULL, OS_ACTIVE_HEAD);
    for (u32 n = 0; n < OS_THREADS_MAX && in_mem1(t); n++) {
        const bool live = t == current;
        const GuestAddr sp = live ? cpu->gpr[1] : mem_read32(NULL, t + OS_CTX_GPR1);
        AM_LOG_INFO("  0x%08X%s estado %u prioridade %d suspensa %d pc 0x%08X lr 0x%08X sp 0x%08X", t,
                    live ? " (atual)" : "", mem_read16(NULL, t + OS_THREAD_STATE),
                    (int)mem_read32(NULL, t + OS_THREAD_PRIORITY), (int)mem_read32(NULL, t + OS_THREAD_SUSPEND),
                    live ? cpu->pc : mem_read32(NULL, t + OS_CTX_SRR0),
                    live ? cpu->lr : mem_read32(NULL, t + OS_CTX_LR), sp);
        back_chain(sp, depth);
        t = mem_read32(NULL, t + OS_THREAD_ACTIVE);
    }
}
