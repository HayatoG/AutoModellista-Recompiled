/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tipos de tamanho fixo. São os mesmos do DolRecomp (common/types.h): typedef idêntico repetido é válido em C11,
 * então este header convive com o C gerado sem puxar o cpu.h inteiro. */
#ifndef AM_TYPES_H
#define AM_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NOLINTBEGIN(readability-identifier-naming) */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;
/* Definido em cpu/cpu.h do DolRecomp; aqui só a declaração antecipada. */
typedef struct CPUState CPUState;
/* NOLINTEND(readability-identifier-naming) */

/* Endereço no espaço do guest (PowerPC de 32 bits). Nunca guardar como void*. */
typedef u32 GuestAddr;

#endif /* AM_TYPES_H */
