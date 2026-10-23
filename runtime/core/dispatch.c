/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/dispatch.h"

#include <stdlib.h>
#include <string.h>

#include "am/guest_cpu.h"
#include "am/log.h"

#define DISPATCH_INITIAL_CAPACITY 256U

static struct {
    DispatchEntry *entries;
    size_t count;
    size_t capacity;
} s_dispatch;

/* Índice da primeira entrada com start > addr (limite superior). */
static size_t upper_bound(GuestAddr addr) {
    size_t lo = 0;
    size_t hi = s_dispatch.count;
    while (lo < hi) {
        const size_t mid = lo + ((hi - lo) / 2U);
        if (s_dispatch.entries[mid].start <= addr) {
            lo = mid + 1U;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static bool reserve_one(void) {
    if (s_dispatch.count < s_dispatch.capacity) {
        return true;
    }
    const size_t capacity = s_dispatch.capacity == 0U ? DISPATCH_INITIAL_CAPACITY : s_dispatch.capacity * 2U;
    DispatchEntry *grown = realloc(s_dispatch.entries, capacity * sizeof(*grown));
    if (grown == NULL) {
        return false;
    }
    s_dispatch.entries = grown;
    s_dispatch.capacity = capacity;
    return true;
}

DispatchResult dispatch_register(GuestAddr start, GuestAddr end, DispatchFn fn) {
    if (fn == NULL || start >= end || ((start | end) & 3U) != 0U) {
        return DISPATCH_ERR_INVALID;
    }
    const size_t at = upper_bound(start);
    if (at > 0U && s_dispatch.entries[at - 1U].end > start) {
        return DISPATCH_ERR_OVERLAP;
    }
    if (at < s_dispatch.count && s_dispatch.entries[at].start < end) {
        return DISPATCH_ERR_OVERLAP;
    }
    if (!reserve_one()) {
        return DISPATCH_ERR_NOMEM;
    }
    memmove(&s_dispatch.entries[at + 1U], &s_dispatch.entries[at], (s_dispatch.count - at) * sizeof(DispatchEntry));
    s_dispatch.entries[at] = (DispatchEntry){.start = start, .end = end, .fn = fn};
    s_dispatch.count++;
    return DISPATCH_OK;
}

void dispatch_clear(void) {
    free(s_dispatch.entries);
    s_dispatch.entries = NULL;
    s_dispatch.count = 0;
    s_dispatch.capacity = 0;
}

size_t dispatch_count(void) {
    return s_dispatch.count;
}

const DispatchEntry *dispatch_entries(void) {
    return s_dispatch.entries;
}

DispatchFn dispatch_lookup(GuestAddr pc) {
    const size_t after = upper_bound(pc);
    if (after == 0U) {
        return NULL;
    }
    const DispatchEntry *entry = &s_dispatch.entries[after - 1U];
    return pc < entry->end ? entry->fn : NULL;
}

void dispatch_step(CPUState *cpu) {
    const DispatchFn fn = dispatch_lookup(cpu->pc);
    if (fn == NULL) {
        am_fatal("dispatcher: nenhuma função recompilada cobre pc=0x%08X (lr=0x%08X)", cpu->pc, cpu->lr);
    }
    fn(cpu);
}
