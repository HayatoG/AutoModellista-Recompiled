/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/hook.h"

#include <string.h>

#include "am/dispatch.h"
#include "am/guest_cpu.h"
#include "am/log.h"

typedef struct Hook {
    GuestAddr addr;
    const char *name;
    HookFn fn;
    void *user;
    u64 hits;
} Hook;

static struct {
    Hook hooks[HOOK_MAX];
    u32 count;
    GuestAddr lo; /* faixa dos endereços com gancho: corta a busca na maioria dos despachos */
    GuestAddr hi;
} s_hook;

void hook_clear(void) {
    memset(&s_hook, 0, sizeof(s_hook));
}

bool hook_register(GuestAddr addr, const char *name, HookFn fn, void *user) {
    if (s_hook.count == HOOK_MAX || fn == NULL || (addr & 3U) != 0U) {
        return false;
    }
    s_hook.hooks[s_hook.count++] = (Hook){.addr = addr, .name = name, .fn = fn, .user = user};
    s_hook.lo = s_hook.count == 1U || addr < s_hook.lo ? addr : s_hook.lo;
    s_hook.hi = addr > s_hook.hi ? addr : s_hook.hi;
    return true;
}

/* Roda os ganchos de um pc; devolve true se algum desviou o pc (o novo pc pode ter os seus). */
static bool run_at(CPUState *cpu, HookAction *action) {
    const GuestAddr pc = cpu->pc;
    for (u32 i = 0; i < s_hook.count; i++) {
        Hook *h = &s_hook.hooks[i];
        if (h->addr != pc) {
            continue;
        }
        h->hits++;
        if (h->fn(cpu, h->user) == HOOK_STOP) {
            *action = HOOK_STOP;
            return false;
        }
        if (cpu->pc != pc) {
            return true;
        }
    }
    return false;
}

HookAction hook_run(CPUState *cpu) {
    HookAction action = HOOK_CONTINUE;
    for (u32 guard = 0; guard < HOOK_MAX; guard++) {
        if (s_hook.count == 0U || cpu->pc < s_hook.lo || cpu->pc > s_hook.hi || !run_at(cpu, &action)) {
            return action;
        }
    }
    am_fatal("ganchos desviando em cadeia sem parar (pc=0x%08X)", cpu->pc);
}

u64 hook_hits(GuestAddr addr) {
    u64 n = 0;
    for (u32 i = 0; i < s_hook.count; i++) {
        n += s_hook.hooks[i].addr == addr ? s_hook.hooks[i].hits : 0U;
    }
    return n;
}

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

/* b/bl relativos (opcode 18, AA = 0): alvo = cia + LI com sinal. */
static bool branch_target(u32 insn, GuestAddr cia, GuestAddr *target) {
    if ((insn >> 26) != 18U || (insn & 2U) != 0U) {
        return false;
    }
    u32 li = insn & 0x03FFFFFCU;
    if ((li & 0x02000000U) != 0U) {
        li |= 0xFC000000U;
    }
    *target = cia + li;
    return true;
}

bool hook_check_reachable(const HookCodeRange *ranges, u32 count) {
    bool ok = true;
    for (u32 r = 0; r < count; r++) {
        for (u32 off = 0; off + 4U <= ranges[r].size; off += 4U) {
            const GuestAddr cia = ranges[r].addr + off;
            GuestAddr target = 0;
            if (!branch_target(be32(&ranges[r].data[off]), cia, &target)) {
                continue;
            }
            for (u32 i = 0; i < s_hook.count; i++) {
                if (s_hook.hooks[i].addr == target && dispatch_lookup(cia) == dispatch_lookup(target)) {
                    AM_LOG_ERROR("gancho %s (0x%08X): desvio em 0x%08X está no mesmo trecho do gerado e não passa "
                                 "pelo chassi",
                                 s_hook.hooks[i].name, target, cia);
                    ok = false;
                }
            }
        }
    }
    return ok;
}
