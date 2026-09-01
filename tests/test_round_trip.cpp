// Test 1: round trip, exhaustive.
//
// For every representable payload value, across carriers spanning the physical
// diameter range, unpack recovers the payload exactly and the carrier stays
// admissible. Written before the implementation: this is the contract.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>

namespace sc = state_channel;

int main() {
    check::context ctx("round trip, exhaustive");

    // Carriers spanning the physical range of interest, 1 um to 1 mm, held in
    // metres because that is what the host solver stores.
    constexpr int carrier_count = 96;
    const double d_lo = 1.0e-6;
    const double d_hi = 1.0e-3;

    for (int i = 0; i < carrier_count; ++i) {
        const double t = static_cast<double>(i) / (carrier_count - 1);
        const float d = static_cast<float>(d_lo * std::pow(d_hi / d_lo, t));

        CHECK(ctx, sc::admissible(d));

        for (std::uint32_t p = 0; p <= sc::payload_max; ++p) {
            const float packed = sc::pack(d, p);
            CHECK(ctx, sc::admissible(packed));
            CHECK(ctx, sc::has_payload(packed));
            CHECK(ctx, sc::payload_of(packed) == p);

            // The exponent is never touched, so the carrier never changes
            // magnitude class.
            CHECK(ctx, (sc::to_bits(packed) & sc::exponent_mask) ==
                       (sc::to_bits(d) & sc::exponent_mask));
        }
    }

    // The same contract at the extremes of the normal range, which no physical
    // diameter reaches but which the guards must not be relied on to exclude.
    const float extremes[] = {1.0f, 1.17549435e-38f /* smallest normal */,
                              3.40282347e+38f /* largest finite */};
    for (const float d : extremes) {
        CHECK(ctx, sc::admissible(d));
        for (std::uint32_t p = 0; p <= sc::payload_max; p += 7) {
            const float packed = sc::pack(d, p);
            CHECK(ctx, std::isfinite(packed));
            CHECK(ctx, sc::payload_of(packed) == p);
        }
    }

    // The quantisation layer round trips too: every code decodes to a state
    // that encodes back to the same code. Without this the exact payload round
    // trip above would be a statement about bits and nothing more.
    const sc::log_scale scale(1.0e-3, 1.0e3);
    for (std::uint32_t code = 0; code <= sc::payload_max; ++code) {
        const double state = scale.decode(code);
        CHECK(ctx, scale.encode(state) == code);
    }
    ctx.note("payload codes", sc::payload_max + 1.0);
    ctx.note("state resolution, relative", scale.resolution());

    return ctx.report();
}
