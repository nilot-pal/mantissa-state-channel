#ifndef STATE_CHANNEL_CHECK_HPP
#define STATE_CHANNEL_CHECK_HPP

// A counting assertion harness, deliberately tiny. The exhaustive tests run
// millions of assertions, so a failure prints once and the rest are counted.

#include <cstdio>

// True when this translation unit is compiled with fast maths.
//
// Under it the compiler is told that NaN, infinity and signed zero do not
// occur, and is entitled to act on that: std::isnan folds to false, a NaN
// compares equal to itself, and -0.0f may be stored as +0.0f. Clang says so out
// loud, with -Wnan-infinity-disabled.
//
// That makes any assertion *about those values as floats* undefined, so those
// assertions are compiled out below. Assertions about the library are not: the
// guards classify the integer bits obtained through memcpy, which no floating
// point flag can reach, and they are checked in both configurations.
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#  define STATE_CHANNEL_FAST_MATH 1
#else
#  define STATE_CHANNEL_FAST_MATH 0
#endif

namespace check {

struct context {
    const char* name;
    long long checks = 0;
    long long failures = 0;

    explicit context(const char* n) : name(n) { std::printf("test: %s\n", n); }

    void pass() { ++checks; }

    void fail(const char* expr, const char* file, int line) {
        ++checks;
        ++failures;
        if (failures <= 5) {
            std::printf("  FAIL %s:%d\n    %s\n", file, line, expr);
        } else if (failures == 6) {
            std::printf("  ... further failures suppressed\n");
        }
    }

    void note(const char* label, double value) {
        std::printf("  %-44s %.9g\n", label, value);
    }

    int report() const {
        std::printf("  %lld checks, %lld failures\n\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
};

}  // namespace check

#define CHECK(ctx, expr) ((expr) ? (ctx).pass() : (ctx).fail(#expr, __FILE__, __LINE__))

#endif
