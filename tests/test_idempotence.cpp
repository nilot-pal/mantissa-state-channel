// Test 8: idempotence and stability.
//
// The state is updated every time a particle is hit, so a carrier is packed
// again and again over its life. If each pack cost a little more carrier
// precision the diameter would drift downward over thousands of impacts, and
// the bound in test 3 would be a bound on one operation rather than on the run.
//
// It does not drift, because pack clears the whole field before writing and the
// bits below it are already gone after the first pack. The error is taken once.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <random>

namespace sc = state_channel;

int main() {
    check::context ctx("idempotence and stability");

    std::mt19937 rng(20260905u);
    std::uniform_int_distribution<std::uint32_t> normal_bits(0x00800000u, 0x7F7FFFFFu);
    std::uniform_int_distribution<std::uint32_t> payloads(0u, sc::payload_max);

    for (int i = 0; i < 200000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        const std::uint32_t first = payloads(rng);
        const std::uint32_t second = payloads(rng);

        const float once = sc::pack(carrier, first);
        const float twice = sc::pack(once, second);

        // Packing over a packed value replaces the payload and leaves the
        // carrier where it already was.
        CHECK(ctx, sc::payload_of(twice) == second);
        CHECK(ctx, sc::to_bits(twice) == sc::to_bits(sc::pack(carrier, second)));
        CHECK(ctx, sc::to_bits(sc::strip(twice)) == sc::to_bits(sc::strip(once)));

        // Packing the same payload twice is a no-op.
        CHECK(ctx, sc::to_bits(sc::pack(once, first)) == sc::to_bits(once));

        // Stripping is idempotent, and unpacking never modifies anything.
        CHECK(ctx, sc::to_bits(sc::strip(sc::strip(once))) == sc::to_bits(sc::strip(once)));
        CHECK(ctx, !sc::has_payload(sc::strip(once)));
    }

    // The property stated as the run sees it: ten thousand updates to the same
    // particle, and the carrier is bit for bit where it was after the first.
    for (int trial = 0; trial < 64; ++trial) {
        const float original = sc::from_bits(normal_bits(rng));
        float carrier = sc::pack(original, 0u);
        const std::uint32_t after_first = sc::to_bits(sc::strip(carrier));

        for (int step = 1; step <= 10000; ++step) {
            carrier = sc::pack(carrier, payloads(rng));
            CHECK(ctx, sc::to_bits(sc::strip(carrier)) == after_first);
        }

        const double drift = (static_cast<double>(carrier) - static_cast<double>(original)) /
                             static_cast<double>(original);
        CHECK(ctx, drift <= sc::carrier_error_above);
        CHECK(ctx, drift >= -sc::carrier_error_below);
    }

    // Unpacking an unpacked value is a no-op, and a value that never held a
    // payload survives a strip untouched.
    for (int i = 0; i < 100000; ++i) {
        const std::uint32_t bits = normal_bits(rng) & ~sc::field_mask;
        const float clear = sc::from_bits(bits);
        CHECK(ctx, sc::to_bits(sc::strip(clear)) == bits);
        CHECK(ctx, sc::to_bits(sc::strip(sc::strip(clear))) == bits);
    }

    return ctx.report();
}
