// Test 7: graceful degradation.
//
// The property the whole technique rests on. A consumer that knows nothing
// about the encoding reads the packed carrier as an ordinary diameter, and the
// value it gets must be within the stated bound of the true one. Every other
// routine in the host solver is that consumer: drag, rebound, erosion, exports.
//
// The bound is asserted in two forms. Relative, because that is the form the
// bit budget produces and it is scale free. And absolute in micrometres for a
// grain of stated size, because that is the form a reviewer asks the question
// in.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>

namespace sc = state_channel;

int main() {
    check::context ctx("graceful degradation");

    constexpr int carrier_count = 96;
    const double d_lo = 1.0e-6;
    const double d_hi = 1.0e-3;

    double worst_relative = 0.0;
    double most_negative = 0.0;
    double most_positive = 0.0;

    for (int i = 0; i < carrier_count; ++i) {
        const double t = static_cast<double>(i) / (carrier_count - 1);
        const float d = static_cast<float>(d_lo * std::pow(d_hi / d_lo, t));

        for (std::uint32_t p = 0; p <= sc::payload_max; ++p) {
            // The unaware consumer. It does not call unpack, it does not call
            // strip, it just reads the float.
            const float seen = sc::pack(d, p);

            const double relative = (static_cast<double>(seen) - static_cast<double>(d)) /
                                    static_cast<double>(d);

            CHECK(ctx, relative <= sc::carrier_error_above);
            CHECK(ctx, relative >= -sc::carrier_error_below);
            CHECK(ctx, seen > 0.0f);
            CHECK(ctx, std::isfinite(seen));

            if (std::fabs(relative) > worst_relative) worst_relative = std::fabs(relative);
            if (relative < most_negative) most_negative = relative;
            if (relative > most_positive) most_positive = relative;
        }
    }

    // The same thing said in micrometres, for a grain of about 100 um.
    //
    // Sweeping payloads against one exact carrier understates this: how much of
    // the field that carrier already occupied is an accident of its low bits.
    // The number a reviewer wants is the worst case over grains of that size,
    // so sweep the carriers as well.
    double worst_micrometres = 0.0;
    for (int i = 0; i < 4096; ++i) {
        const double nominal_um = 95.0 + 10.0 * static_cast<double>(i) / 4095.0;
        const float grain = static_cast<float>(nominal_um * 1.0e-6);
        for (std::uint32_t p = 0; p <= sc::payload_max; p += 13) {
            const float seen = sc::pack(grain, p);
            const double error_um =
                std::fabs(static_cast<double>(seen) - static_cast<double>(grain)) * 1.0e6;
            CHECK(ctx, error_um < 0.4);
            if (error_um > worst_micrometres) worst_micrometres = error_um;
        }
    }

    // Tightness. The bound has to be attained, not merely respected, or it is
    // not the bound. It is reached by a carrier whose significand is exactly 1
    // and whose field is already clear, carrying the largest payload.
    //
    // This is also why the micrometre figure above is smaller than the bound
    // times 100 um. The relative bound is largest at a significand of 1 and
    // falls as the significand rises towards 2, and a 100 um grain sits at a
    // significand of 1.6384.
    const float aligned = 1.220703125e-4f;  // two to the power minus thirteen
    const float cleared = sc::from_bits(sc::to_bits(aligned) & ~sc::field_mask);
    const float moved = sc::pack(cleared, sc::payload_max);
    const double tight =
        (static_cast<double>(moved) - static_cast<double>(cleared)) / static_cast<double>(cleared);
    CHECK(ctx, tight <= sc::carrier_error_above);
    CHECK(ctx, tight > sc::carrier_error_above * 0.999999);

    // The downward extreme, likewise constructed: a carrier whose field is
    // entirely set, carrying payload zero, which is the furthest the packed
    // value can fall below the true one. It approaches the lower bound rather
    // than attaining it, because a carrier with a full field has a significand
    // slightly above 1 and the relative error is measured against that.
    const float full = sc::from_bits(sc::to_bits(aligned) | sc::field_mask);
    const float dropped = sc::pack(full, 0u);
    const double low =
        (static_cast<double>(dropped) - static_cast<double>(full)) / static_cast<double>(full);
    CHECK(ctx, low < 0.0);
    CHECK(ctx, low >= -sc::carrier_error_below);
    CHECK(ctx, low < -sc::carrier_error_below * 0.99);

    // An aware consumer that strips the field instead reads the truncated
    // carrier, which is never above the true diameter. Same bound, opposite
    // skew, and worth knowing which one you are getting.
    for (int i = 0; i < carrier_count; ++i) {
        const double t = static_cast<double>(i) / (carrier_count - 1);
        const float d = static_cast<float>(d_lo * std::pow(d_hi / d_lo, t));
        const float stripped = sc::strip(sc::pack(d, 12345u));
        const double relative = (static_cast<double>(stripped) - static_cast<double>(d)) /
                                static_cast<double>(d);
        CHECK(ctx, relative <= 0.0);
        CHECK(ctx, std::fabs(relative) <= sc::carrier_error_bound);
    }

    ctx.note("bound asserted above, relative", sc::carrier_error_above);
    ctx.note("bound asserted below, relative", -sc::carrier_error_below);
    ctx.note("worst observed, relative", worst_relative);
    ctx.note("most negative observed, relative", most_negative);
    ctx.note("most positive observed, relative", most_positive);
    ctx.note("upper bound attained, relative", tight);
    ctx.note("lower bound approached, relative", low);
    ctx.note("worst observed, um on a 100 um grain", worst_micrometres);

    return ctx.report();
}
