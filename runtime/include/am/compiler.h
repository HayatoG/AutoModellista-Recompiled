/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Único lugar com extensões de compilador (PADROES_DE_CODIGO.md §1). */
#ifndef AM_COMPILER_H
#define AM_COMPILER_H

#if defined(__clang__) || defined(__GNUC__)
#define AM_NORETURN                     __attribute__((noreturn))
#define AM_PRINTF(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#elif defined(_MSC_VER)
#define AM_NORETURN __declspec(noreturn)
#define AM_PRINTF(fmt_index, first_arg)
#else
#define AM_NORETURN _Noreturn
#define AM_PRINTF(fmt_index, first_arg)
#endif

#endif /* AM_COMPILER_H */
