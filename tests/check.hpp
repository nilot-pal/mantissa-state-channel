#ifndef STATE_CHANNEL_CHECK_HPP
#define STATE_CHANNEL_CHECK_HPP

// A counting assertion harness, deliberately tiny. The exhaustive tests run
// millions of assertions, so a failure prints once and the rest are counted.

#include <cstdio>

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
