/* Minimal assertion helpers shared by the Session 4 host test suites. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <string.h>

static int t_checks = 0;
static int t_failed = 0;

#define CHECK(cond) do {                                              \
        t_checks++;                                                   \
        if (!(cond)) {                                                \
            t_failed++;                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                             \
    } while (0)

#define CHECK_EQ(actual, expected) do {                               \
        long t_a = (long)(actual);                                    \
        long t_e = (long)(expected);                                  \
        t_checks++;                                                   \
        if (t_a != t_e) {                                             \
            t_failed++;                                               \
            printf("  FAIL %s:%d: %s = %ld (0x%lX), expected %ld (0x%lX)\n", \
                   __FILE__, __LINE__, #actual, t_a,                  \
                   (unsigned long)t_a, t_e, (unsigned long)t_e);      \
        }                                                             \
    } while (0)

#define CHECK_MEM(actual, expected, len) do {                         \
        size_t t_i;                                                   \
        t_checks++;                                                   \
        if (memcmp((actual), (expected), (len)) != 0) {               \
            t_failed++;                                               \
            printf("  FAIL %s:%d: octets differ\n    got     :",      \
                   __FILE__, __LINE__);                               \
            for (t_i = 0; t_i < (size_t)(len); t_i++)                 \
                printf(" %02X", ((const unsigned char *)(actual))[t_i]);   \
            printf("\n    expected:");                                \
            for (t_i = 0; t_i < (size_t)(len); t_i++)                 \
                printf(" %02X", ((const unsigned char *)(expected))[t_i]); \
            printf("\n");                                             \
        }                                                             \
    } while (0)

#define RUN(test_fn) do {                                             \
        int t_before = t_failed;                                      \
        test_fn();                                                    \
        printf("[%s] %s\n", t_failed == t_before ? " OK " : "FAIL",   \
               #test_fn);                                             \
    } while (0)

#define TEST_SUMMARY() (                                              \
        printf("\n%d checks, %d failed\n", t_checks, t_failed),       \
        t_failed == 0 ? 0 : 1)

#endif /* TEST_UTIL_H */
