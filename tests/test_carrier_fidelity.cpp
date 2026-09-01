// Test 3: carrier fidelity.
//
// The relative error introduced into the carrier is bounded, the bound is
// asserted rather than assumed, and the bound is derived from the payload width
// rather than typed in. Test 7 asks what that means for a diameter in
// micrometres. This one asks whether it holds everywhere in the normal range,
// which is a different question and a stricter one.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <random>

namespace sc = state_channel;

// The bound is a consequence of the layout, not a measured constant. If the
// payload width changes, this still holds.
static_assert(sc::field_bits == sc::payload_bits + 1, "flag plus payload");
static_assert(sc::field_mask == sc::flag_mask + sc::payload_max, "field is flag plus payload");
static_assert(sc::carrier_error_above > sc::carrier_error_below, "the skew is upward");

// The generalised layout agrees with the fixed one at the chosen width. The
// design space figure is drawn from these functions, so if they drifted apart
// the figure would quietly stop describing the code that ships.
static_assert(sc::payload_max_for(sc::payload_bits) == sc::payload_max, "payload max agrees");
static_assert(sc::carrier_error_above_for(sc::payload_bits) == sc::carrier_error_above,
              "upper bound agrees");
static_assert(sc::carrier_error_below_for(sc::payload_bits) == sc::carrier_error_below,
              "lower bound agrees");
static_assert(sc::carrier_error_above_for(sc::payload_bits) ==
                  static_cast<double>(sc::field_mask) / 8388608.0,
              "and it is still the field width over the mantissa width");

int main() {
    check::context ctx("carrier fidelity");

    std::mt19937 rng(20260902u);
    std::uniform_int_distribution<std::uint32_t> mantissa_bits(0u, 0x007FFFFFu);
    std::uniform_int_distribution<std::uint32_t> payloads(0u, sc::payload_max);

    double worst_above = 0.0;
    double worst_below = 0.0;

    // Every exponent in the normal range, which is every magnitude a float can
    // represent without being a denormal.
    for (std::uint32_t exponent = 1u; exponent <= 254u; ++exponent) {
        for (int sample = 0; sample < 256; ++sample) {
            // Boundary mantissas first, then random ones. The boundaries are
            // where a bound is usually wrong.
            std::uint32_t mantissa;
            if (sample == 0) {
                mantissa = 0u;  // significand exactly 1, where the bound is largest
            } else if (sample == 1) {
                mantissa = 0x007FFFFFu;  // significand just below 2
            } else if (sample == 2) {
                mantissa = sc::field_mask;  // field entirely set
            } else if (sample == 3) {
                mantissa = 0x007FFFFFu & ~sc::field_mask;  // field entirely clear
            } else {
                mantissa = mantissa_bits(rng);
            }

            const float carrier = sc::from_bits((exponent << sc::mantissa_bits) | mantissa);
            CHECK(ctx, sc::admissible(carrier));

            for (int k = 0; k < 8; ++k) {
                const std::uint32_t payload =
                    (k == 0) ? 0u : (k == 1) ? sc::payload_max : payloads(rng);
                const float packed = sc::pack(carrier, payload);

                CHECK(ctx, sc::payload_of(packed) == payload);
                CHECK(ctx, sc::admissible(packed));

                // The sign and the exponent are untouched, so the carrier never
                // changes magnitude class and can never become denormal, zero,
                // infinite or NaN. This is what makes the bound a bound rather
                // than a typical case.
                CHECK(ctx, (sc::to_bits(packed) & sc::sign_mask) ==
                           (sc::to_bits(carrier) & sc::sign_mask));
                CHECK(ctx, (sc::to_bits(packed) & sc::exponent_mask) ==
                           (sc::to_bits(carrier) & sc::exponent_mask));

                const double relative = (static_cast<double>(packed) - static_cast<double>(carrier)) /
                                        static_cast<double>(carrier);
                CHECK(ctx, relative <= sc::carrier_error_above);
                CHECK(ctx, relative >= -sc::carrier_error_below);

                if (relative > worst_above) worst_above = relative;
                if (relative < worst_below) worst_below = relative;
            }
        }
    }

    // Both edges are reached somewhere in the sweep, so the bounds are tight
    // and not merely true.
    CHECK(ctx, worst_above > sc::carrier_error_above * 0.999999);
    CHECK(ctx, worst_below < -sc::carrier_error_below * 0.99);

    ctx.note("bound above, relative", sc::carrier_error_above);
    ctx.note("worst above observed, relative", worst_above);
    ctx.note("bound below, relative", -sc::carrier_error_below);
    ctx.note("worst below observed, relative", worst_below);

    return ctx.report();
}
