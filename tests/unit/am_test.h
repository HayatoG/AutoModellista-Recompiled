/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mini framework de testes (PADROES_DE_CODIGO.md §8): sem dependências, um executável por módulo.
 *
 *   TEST(nome) { EXPECT_EQ(1 + 1, 2); }
 *   int main(void) { RUN_TEST(nome); return am_test_finish(); }
 *
 * EXPECT_* não interrompem o teste; o executável sai com 1 se alguma falhou. */
#ifndef AM_TEST_H
#define AM_TEST_H

#include <stdio.h>
#include <string.h>

static int s_am_test_failures = 0;
static int s_am_test_checks = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        const int failures_before = s_am_test_failures;                                                                \
        test_##name();                                                                                                 \
        (void)printf("[%s] %s\n", s_am_test_failures == failures_before ? " OK " : "FAIL", #name);                     \
    } while (0)

#define AM_TEST_FAIL(...)                                                                                              \
    do {                                                                                                               \
        s_am_test_failures++;                                                                                          \
        (void)printf("  %s:%d: ", __FILE__, __LINE__);                                                                 \
        (void)printf(__VA_ARGS__);                                                                                     \
        (void)printf("\n");                                                                                            \
    } while (0)

#define EXPECT_TRUE(cond)                                                                                              \
    do {                                                                                                               \
        s_am_test_checks++;                                                                                            \
        if (!(cond)) {                                                                                                 \
            AM_TEST_FAIL("esperado verdadeiro: %s", #cond);                                                            \
        }                                                                                                              \
    } while (0)

/* Comparam como inteiros sem sinal de 64 bits: para valores negativos use EXPECT_TRUE((a) == (b)). */
#define EXPECT_EQ(actual, expected)                                                                                    \
    do {                                                                                                               \
        const unsigned long long am_actual = (unsigned long long)(actual);                                             \
        const unsigned long long am_expected = (unsigned long long)(expected);                                         \
        s_am_test_checks++;                                                                                            \
        if (am_actual != am_expected) {                                                                                \
            AM_TEST_FAIL("%s == %s: obtido 0x%llX, esperado 0x%llX", #actual, #expected, am_actual, am_expected);      \
        }                                                                                                              \
    } while (0)

#define EXPECT_NE(actual, unexpected)                                                                                  \
    do {                                                                                                               \
        const unsigned long long am_actual = (unsigned long long)(actual);                                             \
        const unsigned long long am_unexpected = (unsigned long long)(unexpected);                                     \
        s_am_test_checks++;                                                                                            \
        if (am_actual == am_unexpected) {                                                                              \
            AM_TEST_FAIL("%s != %s: ambos 0x%llX", #actual, #unexpected, am_actual);                                   \
        }                                                                                                              \
    } while (0)

#define EXPECT_NULL(ptr)     EXPECT_TRUE((ptr) == NULL)
#define EXPECT_NOT_NULL(ptr) EXPECT_TRUE((ptr) != NULL)

#define EXPECT_STREQ(actual, expected)                                                                                 \
    do {                                                                                                               \
        const char *am_actual_str = (actual);                                                                          \
        const char *am_expected_str = (expected);                                                                      \
        s_am_test_checks++;                                                                                            \
        if (strcmp(am_actual_str, am_expected_str) != 0) {                                                             \
            AM_TEST_FAIL("%s == %s: obtido \"%s\", esperado \"%s\"", #actual, #expected, am_actual_str,                \
                         am_expected_str);                                                                             \
        }                                                                                                              \
    } while (0)

/* Resumo e código de saída do executável. */
static int am_test_finish(void) {
    (void)printf("%d verificações, %d falha(s)\n", s_am_test_checks, s_am_test_failures);
    return s_am_test_failures == 0 ? 0 : 1;
}

#endif /* AM_TEST_H */
