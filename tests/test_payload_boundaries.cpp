// Test 9: payload boundaries.
//
// One of the three that would have caught a real bug.
//
// A payload above the maximum must clamp, never wrap. Wrapping is the dangerous
// failure here because it is silent and it inverts the physics: a state that
// has saturated would come back as a state that has not accumulated at all, and
// a particle at the end of its damage history would be read as pristine. A
// clamped state is merely saturated, which is what the physics means anyway.
//
// The same argument governs the quantisation layer, so both are tested here.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>

namespace sc = state_channel;

int main() {
    check::context ctx("payload boundaries");

    const float carriers[] = {1.0e-6f, 1.0e-4f, 1.0f, 1.220703125e-4f};

    for (const float carrier : carriers) {
        // Zero, one below the maximum, and the maximum all round trip exactly.
        for (std::uint32_t payload : {0u, 1u, sc::payload_max - 1u, sc::payload_max}) {
            const float packed = sc::pack(carrier, payload);
            CHECK(ctx, sc::has_payload(packed));
            CHECK(ctx, sc::payload_of(packed) == payload);
        }

        // Above the maximum clamps. It does not wrap to a small payload, and it
        // does not spill into the carrier bits above the field.
        const float at_max = sc::pack(carrier, sc::payload_max);
        for (std::uint32_t payload : {sc::payload_max + 1u, sc::payload_max + 2u,
                                      0x00008000u, 0x0000FFFFu, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
            const float packed = sc::pack(carrier, payload);
            CHECK(ctx, sc::payload_of(packed) == sc::payload_max);
            CHECK(ctx, sc::to_bits(packed) == sc::to_bits(at_max));
            CHECK(ctx, sc::to_bits(sc::strip(packed)) == sc::to_bits(sc::strip(at_max)));
        }

        // Zero is a real payload, not an absence. This is the pairing with the
        // flag bit: without it the two would be the same value.
        const float at_zero = sc::pack(carrier, 0u);
        CHECK(ctx, sc::payload_of(at_zero) == 0u);
        CHECK(ctx, sc::has_payload(at_zero));
        CHECK(ctx, sc::to_bits(at_zero) != sc::to_bits(sc::strip(at_zero)));
    }

    // The quantisation layer clamps at both ends rather than wrapping or
    // extrapolating.
    const sc::log_scale scale(1.0e-3, 1.0e3);

    CHECK(ctx, scale.encode(1.0e-3) == 0u);
    CHECK(ctx, scale.encode(1.0e3) == sc::payload_max);
    CHECK(ctx, scale.encode(1.0e-9) == 0u);        // far below the range
    CHECK(ctx, scale.encode(1.0e9) == sc::payload_max);   // far above it
    CHECK(ctx, scale.encode(0.0) == 0u);
    CHECK(ctx, scale.encode(-1.0) == 0u);          // a state that cannot exist
    CHECK(ctx, scale.encode(std::numeric_limits<double>::infinity()) == sc::payload_max);

    CHECK(ctx, scale.decode(0u) == 1.0e-3);
    CHECK(ctx, scale.decode(sc::payload_max) == 1.0e3);
    CHECK(ctx, scale.decode(sc::payload_max + 1u) == 1.0e3);  // clamped, not extrapolated

    // Monotonic across the whole code range. A quantisation that is not
    // monotonic would let a damaged particle read as less damaged than a
    // fresher one.
    double previous = scale.decode(0u);
    for (std::uint32_t code = 1u; code <= sc::payload_max; ++code) {
        const double value = scale.decode(code);
        CHECK(ctx, value > previous);
        previous = value;
    }

    // A saturated state stays saturated through a full cycle, which is the
    // property wrapping would break.
    const std::uint32_t saturated = scale.encode(1.0e6);
    CHECK(ctx, saturated == sc::payload_max);
    const float packed_saturated = sc::pack(1.0e-4f, saturated);
    CHECK(ctx, sc::payload_of(packed_saturated) == sc::payload_max);
    CHECK(ctx, scale.decode(sc::payload_of(packed_saturated)) == 1.0e3);

    return ctx.report();
}
