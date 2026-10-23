/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ponte para o C gerado pelo DolRecomp (DOL + RELs). Com AM_WITH_GENERATED=OFF todas as funções respondem "vazio". */
#ifndef AM_RECOMP_H
#define AM_RECOMP_H

#include "am/types.h"

/* true se o executável foi ligado ao C gerado. */
bool recomp_is_generated(void);

/* Registra no dispatcher todos os trechos de todos os módulos. Devolve quantos trechos entraram. */
size_t recomp_register_all(void);

size_t recomp_module_count(void);
const char *recomp_module_name(size_t index);
/* Ponto de entrada (DOLRECOMP_ENTRY_POINT) do módulo; para RELs é o endereço virtual, não o de carga. */
GuestAddr recomp_module_entry(size_t index);
/* Trechos registrados por recomp_register_all para o módulo. */
size_t recomp_module_chunks(size_t index);

#endif /* AM_RECOMP_H */
