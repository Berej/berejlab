// Test runner: executes every registered TEST in registration order and reports counts.

#include <stdio.h>
#include <string.h>

#include "test.h"

static test_case *tests_head;
static test_case *tests_tail;
static int current_failed;

void test_register(test_case *tc) {
    tc->next = NULL;
    if (tests_tail) {
        tests_tail->next = tc;
    } else {
        tests_head = tc;
    }
    tests_tail = tc;
}

static void fail_header(const char *file, int line) {
    current_failed = 1;
    fprintf(stderr, "    %s:%d: ", file, line);
}

int test_check_true(int cond, const char *expr, const char *file, int line) {
    if (cond) {
        return 1;
    }
    fail_header(file, line);
    fprintf(stderr, "ASSERT_TRUE(%s) failed\n", expr);
    return 0;
}

int test_check_eq_int(long long actual, long long expected, const char *actual_expr,
                      const char *expected_expr, const char *file, int line) {
    if (actual == expected) {
        return 1;
    }
    fail_header(file, line);
    fprintf(stderr, "ASSERT_EQ_INT(%s, %s) failed: %lld != %lld\n", actual_expr, expected_expr,
            actual, expected);
    return 0;
}

int test_check_eq_str(const char *actual, const char *expected, const char *actual_expr,
                      const char *expected_expr, const char *file, int line) {
    if (actual && expected && strcmp(actual, expected) == 0) {
        return 1;
    }
    if (!actual && !expected) {
        return 1;
    }
    fail_header(file, line);
    fprintf(stderr, "ASSERT_EQ_STR(%s, %s) failed: ", actual_expr, expected_expr);
    if (actual) {
        fprintf(stderr, "\"%s\"", actual);
    } else {
        fputs("NULL", stderr);
    }
    fputs(" != ", stderr);
    if (expected) {
        fprintf(stderr, "\"%s\"\n", expected);
    } else {
        fputs("NULL\n", stderr);
    }
    return 0;
}

int test_check_mem_eq(const void *actual, const void *expected, size_t len,
                      const char *actual_expr, const char *expected_expr, const char *file,
                      int line) {
    if (len == 0) {
        return 1;
    }
    if (!actual || !expected) {
        fail_header(file, line);
        fprintf(stderr, "ASSERT_MEM_EQ(%s, %s, %zu) failed: NULL pointer\n", actual_expr,
                expected_expr, len);
        return 0;
    }
    const unsigned char *a = actual;
    const unsigned char *e = expected;
    for (size_t i = 0; i < len; i++) {
        if (a[i] != e[i]) {
            fail_header(file, line);
            fprintf(stderr,
                    "ASSERT_MEM_EQ(%s, %s, %zu) failed: first difference at byte %zu: "
                    "0x%02x != 0x%02x\n",
                    actual_expr, expected_expr, len, i, a[i], e[i]);
            return 0;
        }
    }
    return 1;
}

int main(void) {
    int passed = 0;
    int failed = 0;

    for (test_case *tc = tests_head; tc; tc = tc->next) {
        current_failed = 0;
        tc->fn();
        if (current_failed) {
            failed++;
            fprintf(stderr, "FAIL  %s (%s)\n", tc->name, tc->file);
        } else {
            passed++;
            printf("ok    %s\n", tc->name);
        }
        // Keep stdout and stderr lines in order when both go to a terminal or a log.
        fflush(stdout);
    }

    printf("\n%d tests: %d passed, %d failed\n", passed + failed, passed, failed);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        perror("test_runner: stdout");
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
