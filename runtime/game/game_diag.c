/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/game_diag.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "am/guest_cpu.h"
#include "am/hook.h"
#include "am/log.h"
#include "am/profile.h"
#include "am/timebase.h"

#define GAME_DIAG_MAX_PROBES  32U
#define GAME_DIAG_PROFILE_TOP 120U

static struct {
    u64 profile_from;
    u64 profile_to;
    FILE *profile;
    u64 probe_from;
    FILE *timebase;
    u64 frame;
} s_diag;

static HookAction on_probe(CPUState *cpu, void *user) {
    (void)user;
    if (s_diag.frame >= s_diag.probe_from) {
        /* Depois de pc, lr e r3: o último mftb (o que o GDB do Dolphin mostra) e o timebase exato. */
        (void)fprintf(s_diag.timebase, "probe %08X %08X %08X %" PRIu64 " %" PRIu64 "\n", cpu->pc, cpu->lr, cpu->gpr[3],
                      timebase_guest_last(), timebase_read(cpu));
    }
    return HOOK_CONTINUE;
}

static bool init_profile(const char *spec) {
    char *end = NULL;
    s_diag.profile_from = strtoull(spec, &end, 10);
    if (end == NULL || *end != ':') {
        return false;
    }
    s_diag.profile_to = strtoull(end + 1, &end, 10);
    if (end == NULL || *end != ':' || s_diag.profile_to <= s_diag.profile_from) {
        return false;
    }
    s_diag.profile = fopen(end + 1, "w");
    if (s_diag.profile == NULL) {
        AM_LOG_ERROR("não consegui criar %s", end + 1);
        return false;
    }
    return true;
}

static bool init_probes(const char *spec) {
    char *end = NULL;
    s_diag.probe_from = strtoull(spec, &end, 10);
    if (end == NULL || *end != ':' || s_diag.timebase == NULL) {
        return false;
    }
    const char *p = end + 1;
    for (u32 n = 0; *p != '\0'; n++) {
        const unsigned long addr = strtoul(p, &end, 16);
        if (n == GAME_DIAG_MAX_PROBES || end == p || addr == 0UL ||
            !hook_register((GuestAddr)addr, "sonda (--probes)", on_probe, NULL)) {
            return false;
        }
        p = *end == ',' ? end + 1 : end;
    }
    return true;
}

bool game_diag_init(const char *profile, const char *probes, FILE *timebase) {
    memset(&s_diag, 0, sizeof(s_diag));
    s_diag.timebase = timebase;
    if (profile != NULL && !init_profile(profile)) {
        AM_LOG_ERROR("--profile espera <q0>:<q1>:<arquivo>, recebeu \"%s\"", profile);
        return false;
    }
    if (s_diag.profile != NULL && s_diag.profile_from == 0U) {
        profile_begin(); /* q0 = 0: desde o entry point */
    }
    if (probes != NULL && !init_probes(probes)) {
        AM_LOG_ERROR("--probes espera <q0>:<hex>,<hex>,... e --timebase-out, recebeu \"%s\"", probes);
        return false;
    }
    return true;
}

void game_diag_frame(u64 frame) {
    s_diag.frame = frame;
    if (s_diag.profile == NULL) {
        return;
    }
    if (frame == s_diag.profile_from) {
        profile_begin();
    } else if (frame == s_diag.profile_to) {
        (void)fprintf(s_diag.profile, "# quadros %" PRIu64 " a %" PRIu64 "\n", s_diag.profile_from, frame);
        profile_end(s_diag.profile, GAME_DIAG_PROFILE_TOP);
        (void)fclose(s_diag.profile);
        s_diag.profile = NULL;
    }
}

void game_diag_shutdown(void) {
    if (s_diag.profile != NULL) {
        profile_end(s_diag.profile, GAME_DIAG_PROFILE_TOP);
        (void)fclose(s_diag.profile);
    }
    memset(&s_diag, 0, sizeof(s_diag));
}
