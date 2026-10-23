/* SPDX-License-Identifier: GPL-3.0-or-later */
/* M3 (esqueleto): inicializa tudo, confere que o C gerado foi ligado e registrado no dispatcher e encerra.
 * Não há boot: __start -> OSInit -> main é o M4. */
#include <stdio.h>
#include <stdlib.h>

#include "am/dispatch.h"
#include "am/guest_cpu.h"
#include "am/log.h"
#include "am/mem.h"
#include "am/platform.h"
#include "am/recomp.h"

/* PLANO_PORT_C.md §4 (M4): OSInit e main do DOL. */
#define GAME_ADDR_OS_INIT 0x800C25DCU
#define GAME_ADDR_MAIN    0x800719ACU

static bool check_entry(const char *what, GuestAddr addr) {
    if (dispatch_lookup(addr) == NULL) {
        AM_LOG_ERROR("%s (0x%08X) não está no dispatcher", what, addr);
        return false;
    }
    return true;
}

static bool verify_generated(void) {
    bool ok = true;
    for (size_t i = 0; i < recomp_module_count(); i++) {
        AM_LOG_INFO("módulo %-9s entrada 0x%08X  trechos %zu", recomp_module_name(i), recomp_module_entry(i),
                    recomp_module_chunks(i));
        ok = check_entry(recomp_module_name(i), recomp_module_entry(i)) && ok;
    }
    ok = check_entry("OSInit", GAME_ADDR_OS_INIT) && ok;
    ok = check_entry("main", GAME_ADDR_MAIN) && ok;
    return ok;
}

int main(void) {
    int status = EXIT_FAILURE;
    CPUState cpu;

    if (!platform_init()) {
        AM_LOG_ERROR("SDL: %s", platform_last_error());
        return status;
    }
    const int sdl = platform_sdl_version();
    AM_LOG_INFO("SDL %d.%d.%d", sdl / 1000000, (sdl / 1000) % 1000, sdl % 1000);

    if (!mem_init()) {
        AM_LOG_ERROR("sem memória para MEM1/ARAM");
        platform_shutdown();
        return status;
    }
    if (guest_cpu_create(&cpu)) {
        am_fatal_set_cpu(&cpu);
        const size_t chunks = recomp_register_all();
        AM_LOG_INFO("C gerado: %s; %zu trecho(s) no dispatcher", recomp_is_generated() ? "ligado" : "ausente (stub)",
                    chunks);
        if (!recomp_is_generated() || verify_generated()) {
            AM_LOG_INFO("M3: esqueleto OK");
            status = EXIT_SUCCESS;
        }
        am_fatal_set_cpu(NULL);
        guest_cpu_destroy(&cpu);
    }
    dispatch_clear();
    mem_shutdown();
    platform_shutdown();
    return status;
}
