// Test 5: guards, denormal and zero.
//
// One of the three that would have caught a real bug.
//
// Packing must never produce a value in a class the host solver does not
// expect, and it must never fabricate one out of nothing. A zero carrier with
// the field written into it stops being zero and becomes a denormal, which is a
// diameter that came from nowhere. A denormal carrier stays denormal but its
// value changes by an unbounded relative amount, because near zero the mantissa
// bits are the entire number rather than a correction to it.
//
// Both are refused, and the second half of this test asserts the consequence
// that matters: no admissible carrier can ever be packed into either class.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <random>

namespace sc = state_channel;

namespace {

bool is_denormal(float v) {
    const std::uint32_t b = sc::to_bits(v);
    return (b & sc::exponent_mask) == 0u && (b & 0x007FFFFFu) != 0u;
}

bool is_zero(float v) {
    return (sc::to_bits(v) & 0x7FFFFFFFu) == 0u;
}

}  // namespace

int main() {
    check::context ctx("guards, denormal and zero");

    const std::uint32_t patterns[] = {
        0x00000000u,  // +0
        0x80000000u,  // -0
        0x00000001u,  // smallest positive denormal
        0x80000001u,  // smallest negative denormal
        0x007FFFFFu,  // largest positive denormal
        0x00400000u,  // mid denormal
        0x00004000u,  // denormal that is nothing but the flag bit
        0x00007FFFu,  // denormal that is nothing but the field
    };

    for (const std::uint32_t bits : patterns) {
        const float v = sc::from_bits(bits);
        CHECK(ctx, is_zero(v) || is_denormal(v));
        CHECK(ctx, !sc::admissible(v));
        CHECK(ctx, !sc::has_payload(v));

        for (std::uint32_t payload : {0u, 1u, sc::payload_max}) {
            const float result = sc::pack(v, payload);
            CHECK(ctx, sc::to_bits(result) == bits);
            // Specifically: a zero did not become a denormal.
            CHECK(ctx, is_zero(v) == is_zero(result));
            CHECK(ctx, is_denormal(v) == is_denormal(result));
        }
        CHECK(ctx, sc::to_bits(sc::strip(v)) == bits);
    }

    // The whole denormal range, exhaustively, on the positive side. There are
    // only about eight million of them and every one must be refused.
    for (std::uint32_t bits = 0u; bits <= 0x007FFFFFu; ++bits) {
        const float v = sc::from_bits(bits);
        CHECK(ctx, !sc::admissible(v));
        CHECK(ctx, sc::to_bits(sc::pack(v, 12345u)) == bits);
    }

    // The consequence. No admissible carrier, packed or stripped, can land in
    // either class, because the exponent field is never written. The smallest
    // normal is the closest an admissible carrier gets to the denormal range,
    // so it is the case worth checking hardest.
    const float smallest_normal = sc::from_bits(0x00800000u);
    CHECK(ctx, sc::admissible(smallest_normal));
    for (std::uint32_t payload = 0u; payload <= sc::payload_max; ++payload) {
        const float packed = sc::pack(smallest_normal, payload);
        CHECK(ctx, !is_zero(packed));
        CHECK(ctx, !is_denormal(packed));
        CHECK(ctx, std::isnormal(packed));
        CHECK(ctx, packed >= smallest_normal);
        CHECK(ctx, !is_zero(sc::strip(packed)));
        CHECK(ctx, std::isnormal(sc::strip(packed)));
    }

    std::mt19937 rng(20260903u);
    std::uniform_int_distribution<std::uint32_t> normal_bits(0x00800000u, 0x7F7FFFFFu);
    std::uniform_int_distribution<std::uint32_t> payloads(0u, sc::payload_max);
    for (int i = 0; i < 500000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        const float packed = sc::pack(carrier, payloads(rng));
        CHECK(ctx, std::isnormal(packed));
        CHECK(ctx, std::isnormal(sc::strip(packed)));
        CHECK(ctx, packed > 0.0f);
    }

    return ctx.report();
}
