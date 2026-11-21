#include <string.h>

#include "common/version.h"
#include "test.h"

TEST(version_string_is_expected) {
    ASSERT_EQ_STR(BEREJLAB_VERSION, "0.1.0-dev");
}

TEST(version_starts_with_semver_core) {
    const char *v = BEREJLAB_VERSION;
    int dots = 0;
    size_t i = 0;
    for (; v[i] && v[i] != '-'; i++) {
        if (v[i] == '.') {
            dots++;
        } else {
            ASSERT_TRUE(v[i] >= '0' && v[i] <= '9');
        }
    }
    ASSERT_EQ_INT(dots, 2);
    ASSERT_TRUE(i > 0);
}

TEST(version_bytes_match) {
    static const unsigned char expected[] = {'0', '.', '1', '.', '0'};
    ASSERT_MEM_EQ(BEREJLAB_VERSION, expected, sizeof expected);
    ASSERT_EQ_INT(strlen(BEREJLAB_VERSION), 9);
}
