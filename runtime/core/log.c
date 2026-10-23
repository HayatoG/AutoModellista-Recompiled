/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "am/guest_cpu.h"

static struct {
    AmLogLevel level;
    const CPUState *cpu;
} s_log = {.level = AM_LOG_LEVEL_INFO, .cpu = NULL};

static const char *level_name(AmLogLevel level) {
    switch (level) {
    case AM_LOG_LEVEL_TRACE:
        return "TRACE";
    case AM_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case AM_LOG_LEVEL_INFO:
        return "INFO";
    case AM_LOG_LEVEL_WARN:
        return "WARN";
    case AM_LOG_LEVEL_ERROR:
        return "ERROR";
    }
    return "?";
}

void am_log_set_level(AmLogLevel level) {
    s_log.level = level;
}

AmLogLevel am_log_get_level(void) {
    return s_log.level;
}

void am_log(AmLogLevel level, const char *fmt, ...) {
    if (level < s_log.level) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)fprintf(stderr, "[%s] ", level_name(level));
    (void)vfprintf(stderr, fmt, args);
    (void)fputc('\n', stderr);
    va_end(args);
}

void am_fatal_set_cpu(const CPUState *cpu) {
    s_log.cpu = cpu;
}

static void print_guest_context(void) {
    if (s_log.cpu != NULL) {
        (void)fprintf(stderr, "        guest: pc=0x%08X lr=0x%08X ctr=0x%08X\n", s_log.cpu->pc, s_log.cpu->lr,
                      s_log.cpu->ctr);
    }
}

AM_NORETURN static void die(void) {
    print_guest_context();
    (void)fflush(stderr);
    exit(AM_EXIT_FATAL);
}

void am_fatal(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    (void)fprintf(stderr, "[FATAL] ");
    (void)vfprintf(stderr, fmt, args);
    (void)fputc('\n', stderr);
    va_end(args);
    die();
}

void am_assert_failed(const char *file, int line, const char *expr, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    (void)fprintf(stderr, "[FATAL] asserção falhou em %s:%d: %s\n        ", file, line, expr);
    (void)vfprintf(stderr, fmt, args);
    (void)fputc('\n', stderr);
    va_end(args);
    die();
}
