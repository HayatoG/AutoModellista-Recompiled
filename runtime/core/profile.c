/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/profile.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* Endereçamento aberto; 2^16 trechos distintos bastam (o jogo tem ~20 mil funções entre DOL e RELs). */
#define PROFILE_SLOTS   65536U
#define PROFILE_THREADS 64U

typedef struct ProfileSlot {
    GuestAddr key;
    u64 cycles;
    u64 slices;
} ProfileSlot;

static struct {
    bool active;
    u64 total;
    ProfileSlot pcs[PROFILE_SLOTS];
    ProfileSlot threads[PROFILE_THREADS];
} s_prof;

void profile_begin(void) {
    memset(&s_prof, 0, sizeof(s_prof));
    s_prof.active = true;
}

bool profile_active(void) {
    return s_prof.active;
}

static void add_to(ProfileSlot *table, u32 size, GuestAddr key, u64 cycles) {
    u32 i = (key >> 2) * 2654435761U;
    for (u32 n = 0; n < size; n++, i++) {
        ProfileSlot *s = &table[i & (size - 1U)];
        if (s->key == key || s->slices == 0U) {
            s->key = key;
            s->cycles += cycles;
            s->slices++;
            return;
        }
    }
}

void profile_add(GuestAddr pc, GuestAddr thread, u64 cycles) {
    if (!s_prof.active) {
        return;
    }
    s_prof.total += cycles;
    add_to(s_prof.pcs, PROFILE_SLOTS, pc, cycles);
    add_to(s_prof.threads, PROFILE_THREADS, thread, cycles);
}

static int by_cycles(const void *a, const void *b) {
    const u64 x = ((const ProfileSlot *)a)->cycles;
    const u64 y = ((const ProfileSlot *)b)->cycles;
    if (x < y) {
        return 1;
    }
    return x > y ? -1 : 0;
}

static void dump(FILE *out, const char *title, ProfileSlot *table, u32 size, u32 top) {
    qsort(table, size, sizeof(*table), by_cycles);
    (void)fprintf(out, "# %s: endereço ciclos fatias %%\n", title);
    for (u32 i = 0; i < size && i < top && table[i].slices != 0U; i++) {
        const double pct = s_prof.total != 0U ? 100.0 * (double)table[i].cycles / (double)s_prof.total : 0.0;
        (void)fprintf(out, "%08X %" PRIu64 " %" PRIu64 " %.2f\n", table[i].key, table[i].cycles, table[i].slices, pct);
    }
}

void profile_end(FILE *out, u32 top) {
    if (!s_prof.active) {
        return;
    }
    s_prof.active = false;
    (void)fprintf(out, "# total %" PRIu64 " ciclos\n", s_prof.total);
    dump(out, "threads", s_prof.threads, PROFILE_THREADS, top);
    dump(out, "trechos", s_prof.pcs, PROFILE_SLOTS, top);
}
