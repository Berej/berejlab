#ifndef BEREJLAB_TESTS_TEST_H
#define BEREJLAB_TESTS_TEST_H

// Minimal in-house unit-test framework.
//
//   TEST(name) { ASSERT_EQ_INT(1 + 1, 2); }
//
// Tests register themselves through a constructor, so a new tests/test_*.c file is
// picked up by the build without touching the Makefile or a central list.
// A failed assertion reports file:line and ends the current test.

#include <stddef.h>

typedef struct test_case {
    const char *name;
    const char *file;
    void (*fn)(void);
    struct test_case *next;
} test_case;

void test_register(test_case *tc);

// Each returns 1 when the check passed; on failure it records and prints the reason.
int test_check_true(int cond, const char *expr, const char *file, int line);
int test_check_eq_int(long long actual, long long expected, const char *actual_expr,
                      const char *expected_expr, const char *file, int line);
int test_check_eq_str(const char *actual, const char *expected, const char *actual_expr,
                      const char *expected_expr, const char *file, int line);
int test_check_mem_eq(const void *actual, const void *expected, size_t len,
                      const char *actual_expr, const char *expected_expr, const char *file,
                      int line);

#define TEST(name)                                                                          \
    static void test_fn_##name(void);                                                       \
    static test_case test_case_##name = {#name, __FILE__, test_fn_##name, NULL};            \
    __attribute__((constructor)) static void test_reg_##name(void) {                       \
        test_register(&test_case_##name);                                                   \
    }                                                                                       \
    static void test_fn_##name(void)

#define ASSERT_TRUE(cond)                                                                   \
    do {                                                                                    \
        if (!test_check_true((cond) ? 1 : 0, #cond, __FILE__, __LINE__)) {                  \
            return;                                                                         \
        }                                                                                   \
    } while (0)

#define ASSERT_EQ_INT(actual, expected)                                                     \
    do {                                                                                    \
        if (!test_check_eq_int((long long)(actual), (long long)(expected), #actual,         \
                               #expected, __FILE__, __LINE__)) {                            \
            return;                                                                         \
        }                                                                                   \
    } while (0)

#define ASSERT_EQ_STR(actual, expected)                                                     \
    do {                                                                                    \
        if (!test_check_eq_str((actual), (expected), #actual, #expected, __FILE__,          \
                               __LINE__)) {                                                 \
            return;                                                                         \
        }                                                                                   \
    } while (0)

#define ASSERT_MEM_EQ(actual, expected, len)                                                \
    do {                                                                                    \
        if (!test_check_mem_eq((actual), (expected), (len), #actual, #expected, __FILE__,   \
                               __LINE__)) {                                                 \
            return;                                                                         \
        }                                                                                   \
    } while (0)

#endif
