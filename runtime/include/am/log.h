/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Log, erro fatal e asserção (PADROES_DE_CODIGO.md §5). */
#ifndef AM_LOG_H
#define AM_LOG_H

#include "am/compiler.h"
#include "am/types.h"

/* Código de saída de am_fatal/AM_ASSERT: EX_SOFTWARE do sysexits.h, sem depender dele (não existe no Windows). */
#define AM_EXIT_FATAL 70

typedef enum AmLogLevel {
    AM_LOG_LEVEL_TRACE,
    AM_LOG_LEVEL_DEBUG,
    AM_LOG_LEVEL_INFO,
    AM_LOG_LEVEL_WARN,
    AM_LOG_LEVEL_ERROR,
} AmLogLevel;

void am_log_set_level(AmLogLevel level);
AmLogLevel am_log_get_level(void);
void am_log(AmLogLevel level, const char *fmt, ...) AM_PRINTF(2, 3);

/* Registra a CPU cujo pc/lr aparece na mensagem de am_fatal. NULL remove o registro. */
void am_fatal_set_cpu(const CPUState *cpu);

/* Situação que o jogo não deveria provocar: imprime mensagem, pc e lr do guest e encerra com AM_EXIT_FATAL. */
AM_NORETURN void am_fatal(const char *fmt, ...) AM_PRINTF(1, 2);
AM_NORETURN void am_assert_failed(const char *file, int line, const char *expr, const char *fmt, ...) AM_PRINTF(4, 5);

#define AM_LOG_TRACE(...) am_log(AM_LOG_LEVEL_TRACE, __VA_ARGS__)
#define AM_LOG_DEBUG(...) am_log(AM_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define AM_LOG_INFO(...)  am_log(AM_LOG_LEVEL_INFO, __VA_ARGS__)
#define AM_LOG_WARN(...)  am_log(AM_LOG_LEVEL_WARN, __VA_ARGS__)
#define AM_LOG_ERROR(...) am_log(AM_LOG_LEVEL_ERROR, __VA_ARGS__)

/* Invariante de programa; ativa também em Release. O formato é obrigatório. */
#define AM_ASSERT(cond, ...)                                                                                           \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            am_assert_failed(__FILE__, __LINE__, #cond, __VA_ARGS__);                                                  \
        }                                                                                                              \
    } while (0)

#endif /* AM_LOG_H */
