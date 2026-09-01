// Test 2: flag discrimination.
//
// A value whose field is clear is not a channel, is returned bit for bit
// unchanged, and never yields a payload. A flagged carrier whose payload is
// zero is distinguishable from it, which is the entire reason the flag exists.
//
// The test also asserts the limitation in the other direction, because it is
// real and a reviewer will find it: the flag is one bit in a field this code
// does not own, so a carrier it never packed whose bit 14 happens to be set
// does read as flagged. See the note at the end.

#include "check.hpp"
#include "state_channel.hpp"

#include <cstdint>
#include <random>

namespace sc = state_channel;

int main() {
    check::context ctx("flag discrimination");

    std::mt19937 rng(20260901u);
    std::uniform_int_distribution<std::uint32_t> any_bits(0u, 0xFFFFFFFFu);

    for (int i = 0; i < 1000000; ++i) {
        const std::uint32_t bits = any_bits(rng);

        // An admissible carrier with the field deliberately cleared.
        const std::uint32_t exponent = 1u + (bits % 254u);
        const std::uint32_t mantissa = any_bits(rng) & 0x007FFFFFu;
        const float clear = sc::from_bits((exponent << sc::mantissa_bits) |
                                          (mantissa & ~sc::field_mask));

        CHECK(ctx, sc::admissible(clear));
        CHECK(ctx, !sc::has_payload(clear));
        CHECK(ctx, sc::to_bits(sc::strip(clear)) == sc::to_bits(clear));

        std::uint32_t recovered = 0xDEADu;
        CHECK(ctx, !sc::try_unpack(clear, recovered));
        CHECK(ctx, recovered == 0xDEADu);  // left alone on refusal

        // The same carrier with a payload of zero. Without the flag this would
        // be indistinguishable from the value above, and the fallback path
        // would be unreachable.
        const float zero_payload = sc::pack(clear, 0u);
        CHECK(ctx, sc::has_payload(zero_payload));
        CHECK(ctx, sc::payload_of(zero_payload) == 0u);
        CHECK(ctx, sc::to_bits(zero_payload) != sc::to_bits(clear));
        CHECK(ctx, sc::try_unpack(zero_payload, recovered));
        CHECK(ctx, recovered == 0u);

        // Stripping returns to the unflagged value exactly, because the field
        // was already clear before packing.
        CHECK(ctx, sc::to_bits(sc::strip(zero_payload)) == sc::to_bits(clear));
    }

    // No inadmissible value is ever read as a channel, however its bits fall,
    // including when bit 14 is set.
    const std::uint32_t inadmissible[] = {
        0x00000000u,  // +0
        0x80000000u,  // -0
        0x00004000u,  // denormal with the flag bit set
        0x007FFFFFu,  // largest denormal
        0x7F800000u,  // +inf
        0xFF800000u,  // -inf
        0x7FC00000u,  // quiet NaN
        0x7F804000u,  // signalling NaN with the flag bit set
        0xBF804000u,  // negative normal with the flag bit set
    };
    for (const std::uint32_t bits : inadmissible) {
        const float v = sc::from_bits(bits);
        CHECK(ctx, !sc::admissible(v));
        CHECK(ctx, !sc::has_payload(v));
        CHECK(ctx, sc::to_bits(sc::strip(v)) == bits);
        CHECK(ctx, sc::to_bits(sc::pack(v, 9999u)) == bits);
    }

    // The limitation, asserted rather than left to be discovered.
    //
    // has_payload is a test of one bit in a field this code does not own. An
    // ordinary diameter that was never packed, whose bit 14 happens to be set,
    // reads as flagged and yields whatever its low bits contain. Roughly half
    // of all arbitrary carriers do.
    //
    // So the flag means "this particle passed through the injection routine",
    // not "this value was packed by me", and it is only worth that much because
    // the host initialises every particle exactly once at injection. The
    // fallback covers carriers that were inadmissible at that moment, not
    // carriers nobody ever touched.
    int falsely_flagged = 0;
    for (int i = 0; i < 10000; ++i) {
        const std::uint32_t exponent = 1u + (any_bits(rng) % 254u);
        const std::uint32_t mantissa = any_bits(rng) & 0x007FFFFFu;
        const float never_packed = sc::from_bits((exponent << sc::mantissa_bits) | mantissa);
        if (sc::has_payload(never_packed)) ++falsely_flagged;
    }
    CHECK(ctx, falsely_flagged > 4000);  // about half, and that is the point
    CHECK(ctx, falsely_flagged < 6000);
    ctx.note("unpacked carriers reading as flagged, per 10000",
             static_cast<double>(falsely_flagged));

    return ctx.report();
}
