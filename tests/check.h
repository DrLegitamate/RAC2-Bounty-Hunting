// SPDX-License-Identifier: GPL-3.0-or-later
//
// A minimal test harness: CHECK records a failure and goes on; each test file's main returns the
// failure count, which ctest reads.

#pragma once

#include <cstdio>

namespace check {

inline int& failures() {
    static int count = 0;
    return count;
}

inline int& passes() {
    static int count = 0;
    return count;
}

inline int finish(const char* suite) {
    std::printf("%s: %d passed, %d failed\n", suite, passes(), failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK(condition)                                                                        \
    do {                                                                                        \
        if (condition) {                                                                        \
            ++check::passes();                                                                  \
        } else {                                                                                \
            ++check::failures();                                                                \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);           \
        }                                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        const auto check_a = (a);                                                               \
        const auto check_b = (b);                                                               \
        if (check_a == check_b) {                                                               \
            ++check::passes();                                                                  \
        } else {                                                                                \
            ++check::failures();                                                                \
            std::printf(                                                                        \
                "%s:%d: CHECK_EQ failed: %s == %s (%lld vs %lld)\n",                            \
                __FILE__,                                                                       \
                __LINE__,                                                                       \
                #a,                                                                             \
                #b,                                                                             \
                static_cast<long long>(check_a),                                                \
                static_cast<long long>(check_b)                                                 \
            );                                                                                  \
        }                                                                                       \
    } while (0)
