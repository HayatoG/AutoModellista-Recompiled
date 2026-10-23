/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Versão de AM_WITH_GENERATED=OFF: o C gerado é material do jogo e não existe no CI. */
#include "am/recomp.h"

bool recomp_is_generated(void) {
    return false;
}

size_t recomp_register_all(void) {
    return 0;
}

size_t recomp_module_count(void) {
    return 0;
}

const char *recomp_module_name(size_t index) {
    (void)index;
    return "";
}

GuestAddr recomp_module_entry(size_t index) {
    (void)index;
    return 0;
}

size_t recomp_module_chunks(size_t index) {
    (void)index;
    return 0;
}
