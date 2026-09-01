// Test 11: fuzz.
//
// Ten million random carrier and payload pairs, every invariant from the tests
// above asserted on each. The carriers are drawn from two streams: arbitrary
// 32-bit patterns, which are mostly not diameters and are there to hammer the
// guards, and plausible diameters, which are what the solver actually supplies
// and which would otherwise be reached only by accident.
//
// The seed is fixed. A fuzz test that cannot be re-run on the input that broke
// it is a slot machine.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <random>

namespace sc = state_channel;

int main() {
    check::context ctx("fuzz");

    constexpr int iterations = 10000000;

    std::mt19937 rng(20260907u);
    std::uniform_int_distribution<std::uint32_t> any_bits(0u, 0xFFFFFFFFu);
    std::uniform_int_distribution<std::uint32_t> payloads(0u, 0xFFFFFFFFu);
    std::uniform_real_distribution<double> log_diameter(-6.0, -3.0);

    long long admissible_seen = 0;
    long long refused_seen = 0;
    double worst_above = 0.0;
    double worst_below = 0.0;

    for (int i = 0; i < iterations; ++i) {
        // Half arbitrary bit patterns, half plausible diameters.
        float carrier;
        if ((i & 1) == 0) {
            carrier = sc::from_bits(any_bits(rng));
        } else {
            carrier = static_cast<float>(std::pow(10.0, log_diameter(rng)));
        }

        // Payloads are drawn over the whole 32-bit range so that the clamp is
        // exercised as often as the ordinary path.
        const std::uint32_t requested = payloads(rng);
        const std::uint32_t expected = requested > sc::payload_max ? sc::payload_max : requested;

        const std::uint32_t before = sc::to_bits(carrier);
        const float packed = sc::pack(carrier, requested);

        if (!sc::admissible(carrier)) {
            ++refused_seen;
            // Refused, bit for bit, and never mistaken for a channel.
            CHECK(ctx, sc::to_bits(packed) == before);
            CHECK(ctx, !sc::has_payload(packed));
            CHECK(ctx, sc::to_bits(sc::strip(carrier)) == before);
            std::uint32_t recovered = 0x5A5Au;
            CHECK(ctx, !sc::try_unpack(carrier, recovered));
            CHECK(ctx, recovered == 0x5A5Au);
            continue;
        }

        ++admissible_seen;

        // Round trip, clamped rather than wrapped.
        std::uint32_t recovered = 0u;
        CHECK(ctx, sc::has_payload(packed));
        CHECK(ctx, sc::try_unpack(packed, recovered));
        CHECK(ctx, recovered == expected);
        CHECK(ctx, sc::payload_of(packed) == expected);

        // Class preserved. Sign and exponent untouched, so nothing can become
        // denormal, zero, infinite or NaN.
        CHECK(ctx, std::isnormal(packed));
        CHECK(ctx, packed > 0.0f);
        CHECK(ctx, (sc::to_bits(packed) & sc::exponent_mask) == (before & sc::exponent_mask));
        CHECK(ctx, (sc::to_bits(packed) & sc::sign_mask) == 0u);

        // Carrier fidelity, signed.
        const double relative = (static_cast<double>(packed) - static_cast<double>(carrier)) /
                                static_cast<double>(carrier);
        CHECK(ctx, relative <= sc::carrier_error_above);
        CHECK(ctx, relative >= -sc::carrier_error_below);
        if (relative > worst_above) worst_above = relative;
        if (relative < worst_below) worst_below = relative;

        // Idempotence, and stripping never leaves the normal range.
        CHECK(ctx, sc::to_bits(sc::pack(packed, expected)) == sc::to_bits(packed));
        const float stripped = sc::strip(packed);
        CHECK(ctx, std::isnormal(stripped));
        CHECK(ctx, !sc::has_payload(stripped));
        CHECK(ctx, stripped <= carrier);  // strip only ever reads low
        CHECK(ctx, sc::to_bits(sc::strip(stripped)) == sc::to_bits(stripped));
    }

    // Both streams have to have reached both paths, or the run proved less than
    // it appears to.
    CHECK(ctx, admissible_seen > iterations / 4);
    CHECK(ctx, refused_seen > iterations / 8);

    ctx.note("iterations", static_cast<double>(iterations));
    ctx.note("carriers admitted", static_cast<double>(admissible_seen));
    ctx.note("carriers refused", static_cast<double>(refused_seen));
    ctx.note("worst above observed, relative", worst_above);
    ctx.note("worst below observed, relative", worst_below);

    return ctx.report();
}
