// Test 6: guards, sign.
//
// Negative carriers are refused. A diameter is positive by definition, so a
// negative one means something upstream has already gone wrong, and the right
// response to a value that should not exist is to leave it alone rather than
// write into it. Overwriting it would hide the fault and hand the solver a
// slightly different wrong number.

#include "check.hpp"
#include "state_channel.hpp"

#include <cstdint>
#include <initializer_list>
#include <random>

namespace sc = state_channel;

int main() {
    check::context ctx("guards, sign");

    // Held as bit patterns rather than as float literals in an array. Under
    // fast maths the compiler is allowed to assume signed zero does not occur,
    // so a -0.0f sitting in a const float array can be stored as +0.0f and the
    // sign this test is about disappears before the test runs. The bits cannot
    // be optimised away in the same manner.
    const std::uint32_t negatives[] = {
        0xBF800000u,   // -1
        0xB8D1B717u,   // a plausible diameter, negated
        0xFF7FFFFFu,   // most negative finite
        0x80800000u,   // smallest negative normal
        0x80000000u,   // -0
        0x80004000u,   // negative denormal with the flag set
        0xBF804000u,   // negative normal with the flag set
    };

    for (const std::uint32_t bits : negatives) {
        const float v = sc::from_bits(bits);
        CHECK(ctx, (bits & sc::sign_mask) != 0u);
        CHECK(ctx, !sc::admissible(v));
        CHECK(ctx, !sc::has_payload(v));
        CHECK(ctx, sc::payload_of(v) == 0u);

        for (std::uint32_t payload : {0u, 1u, sc::payload_max, 0xFFFFFFFFu}) {
            CHECK(ctx, sc::to_bits(sc::pack(v, payload)) == bits);
        }
        CHECK(ctx, sc::to_bits(sc::strip(v)) == bits);

        std::uint32_t recovered = 0x1234u;
        CHECK(ctx, !sc::try_unpack(v, recovered));
        CHECK(ctx, recovered == 0x1234u);
    }

    // Every negative normal carrier, sampled across the whole range, and the
    // mirror property: the positive twin of each one is admissible. The guard
    // rejects on the sign and on nothing else.
    std::mt19937 rng(20260904u);
    std::uniform_int_distribution<std::uint32_t> normal_bits(0x00800000u, 0x7F7FFFFFu);
    for (int i = 0; i < 500000; ++i) {
        const std::uint32_t positive = normal_bits(rng);
        const float negative_carrier = sc::from_bits(positive | sc::sign_mask);
        CHECK(ctx, !sc::admissible(negative_carrier));
        CHECK(ctx, sc::to_bits(sc::pack(negative_carrier, 777u)) == (positive | sc::sign_mask));
        CHECK(ctx, sc::admissible(sc::from_bits(positive)));
    }

    // -0 and +0 are both refused, but for different reasons, and both reasons
    // have to hold. -0 fails on the sign, +0 fails on the exponent. Neither
    // guard alone would catch both.
    CHECK(ctx, !sc::admissible(sc::from_bits(0x80000000u)));
    CHECK(ctx, !sc::admissible(sc::from_bits(0x00000000u)));

    return ctx.report();
}
