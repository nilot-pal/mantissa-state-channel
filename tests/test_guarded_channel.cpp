// Test 13: the guarded channel.
//
// The design that replaces the flag bit with a seven bit checksum over the
// carrier. Everything the flag design promises, plus the two properties the
// flag design cannot have:
//
//   a carrier that was never packed is rejected about 127 times in 128
//   a carrier that anything else has written since packing is rejected
//
// The second is the important one and it gets the most care here, because it is
// the failure the flag design cannot see at all.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <random>

namespace sc = state_channel;
namespace gd = state_channel::guarded;

int main() {
    check::context ctx("guarded channel");

    std::mt19937 rng(20260909u);
    std::uniform_int_distribution<std::uint32_t> normal_bits(0x00800000u, 0x7F7FFFFFu);
    std::uniform_int_distribution<std::uint32_t> any_bits(0u, 0xFFFFFFFFu);

    // Round trip, exhaustive over the payload, across the physical range.
    for (int i = 0; i < 128; ++i) {
        const double t = static_cast<double>(i) / 127.0;
        const float d = static_cast<float>(1.0e-6 * std::pow(1000.0, t));

        for (std::uint32_t p = 0; p <= gd::payload_max; ++p) {
            const float packed = gd::pack(d, p);
            CHECK(ctx, gd::has_payload(packed));
            CHECK(ctx, gd::payload_of(packed) == p);
            CHECK(ctx, sc::admissible(packed));
            CHECK(ctx, (sc::to_bits(packed) & sc::exponent_mask) ==
                       (sc::to_bits(d) & sc::exponent_mask));

            const double relative = (static_cast<double>(packed) - static_cast<double>(d)) /
                                    static_cast<double>(d);
            CHECK(ctx, relative <= gd::carrier_error_above);
            CHECK(ctx, relative >= -gd::carrier_error_below);
        }
    }

    // Tamper detection. This is the property the whole design exists for.
    //
    // Take a packed carrier and disturb it the way another routine would: flip
    // a bit above the field, or change the exponent. The checksum was taken
    // over exactly those bits, so it no longer matches and the payload is
    // refused. The caller falls back instead of acting on a state that is not
    // there.
    long long tampered = 0, caught = 0;
    for (int i = 0; i < 200000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        const float packed = gd::pack(carrier, any_bits(rng) & gd::payload_max);
        CHECK(ctx, gd::has_payload(packed));

        // Disturb one bit somewhere above the field: the surviving mantissa
        // bits or the exponent.
        const int bit = 16 + static_cast<int>(any_bits(rng) % 15u);   // bits 16 to 30
        const float disturbed = sc::from_bits(sc::to_bits(packed) ^ (std::uint32_t(1) << bit));

        if (!sc::admissible(disturbed)) continue;   // flipped into inf or NaN
        ++tampered;
        if (!gd::has_payload(disturbed)) ++caught;
    }
    // Not every disturbance is caught: a seven bit checksum has 128 values, so
    // a change that happens to collide is missed. The claim is a rate, and the
    // rate is what is asserted.
    const double caught_fraction = static_cast<double>(caught) / static_cast<double>(tampered);
    CHECK(ctx, caught_fraction > 0.97);
    ctx.note("single bit disturbances caught, fraction", caught_fraction);
    ctx.note("expected, 1 - 1/128", 1.0 - gd::false_accept_rate);

    // The flag design cannot do this at all: a disturbance above the field
    // leaves bit 14 exactly where it was, so the payload is still accepted.
    long long flag_caught = 0;
    for (int i = 0; i < 20000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        const float packed = sc::pack(carrier, any_bits(rng) & sc::payload_max);
        const int bit = 16 + static_cast<int>(any_bits(rng) % 7u);
        const float disturbed = sc::from_bits(sc::to_bits(packed) ^ (std::uint32_t(1) << bit));
        if (!sc::admissible(disturbed)) continue;
        if (!sc::has_payload(disturbed)) ++flag_caught;
    }
    CHECK(ctx, flag_caught == 0);   // it cannot see the difference, by construction
    ctx.note("same disturbances caught by the flag design", static_cast<double>(flag_caught));

    // False accepts. A carrier that was never packed passes the check about one
    // time in 128, against one time in two for a flag bit.
    long long never_packed = 0, accepted = 0, flag_accepted = 0;
    for (int i = 0; i < 2000000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        ++never_packed;
        if (gd::has_payload(carrier)) ++accepted;
        if (sc::has_payload(carrier)) ++flag_accepted;
    }
    const double rate = static_cast<double>(accepted) / static_cast<double>(never_packed);
    const double flag_rate = static_cast<double>(flag_accepted) / static_cast<double>(never_packed);
    CHECK(ctx, rate < 2.0 * gd::false_accept_rate);
    CHECK(ctx, rate > 0.4 * gd::false_accept_rate);
    CHECK(ctx, flag_rate > 0.45 && flag_rate < 0.55);
    ctx.note("false accepts, guarded", rate);
    ctx.note("false accepts, expected 1/128", gd::false_accept_rate);
    ctx.note("false accepts, flag design", flag_rate);

    // The guards are unchanged, and refusal is still bit for bit.
    const std::uint32_t inadmissible[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x007FFFFFu,
        0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xBF800000u,
    };
    for (const std::uint32_t bits : inadmissible) {
        const float v = sc::from_bits(bits);
        CHECK(ctx, !gd::has_payload(v));
        CHECK(ctx, sc::to_bits(gd::pack(v, 100u)) == bits);
        CHECK(ctx, sc::to_bits(gd::strip(v)) == bits);
    }

    // Idempotence, and the payload clamps rather than wrapping.
    //
    // Note what is not asserted here. In the flag design, stripping guarantees
    // the result reads as carrying nothing, because clearing bit 14 is exactly
    // the question has_payload asks. The checksum design has no such guarantee:
    // stripping writes zero into the field, and about one carrier in 128 has a
    // checksum of zero over its own base bits, so its stripped form reads as
    // carrying payload zero.
    //
    // That is the same 1 in 128 as everywhere else in this design, pointing the
    // other way, and it is the one thing the flag does better. It costs nothing
    // in practice, because nothing strips a carrier and then asks whether it
    // still has a payload, but it is asserted as a rate rather than swept up.
    long long stripped_still_reads = 0;
    for (int i = 0; i < 100000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        const float once = gd::pack(carrier, 7u);
        CHECK(ctx, sc::to_bits(gd::pack(once, 7u)) == sc::to_bits(once));
        CHECK(ctx, gd::payload_of(gd::pack(once, 11u)) == 11u);
        CHECK(ctx, gd::payload_of(gd::pack(carrier, gd::payload_max + 1u)) == gd::payload_max);

        const float bare = gd::strip(once);
        CHECK(ctx, (sc::to_bits(bare) & gd::field_mask) == 0u);   // the field is cleared
        if (gd::has_payload(bare)) ++stripped_still_reads;
    }
    const double strip_rate = static_cast<double>(stripped_still_reads) / 100000.0;
    CHECK(ctx, strip_rate < 2.0 * gd::false_accept_rate);
    ctx.note("stripped carriers still reading as packed", strip_rate);

    // The flag design does guarantee it, and that is worth showing side by side.
    long long flag_stripped_still_reads = 0;
    for (int i = 0; i < 100000; ++i) {
        const float carrier = sc::from_bits(normal_bits(rng));
        if (sc::has_payload(sc::strip(sc::pack(carrier, 7u)))) ++flag_stripped_still_reads;
    }
    CHECK(ctx, flag_stripped_still_reads == 0);
    ctx.note("same for the flag design", static_cast<double>(flag_stripped_still_reads));

    // Known answers, computed by a separate implementation in a different
    // language, so that a refactor here cannot quietly change the wire format.
    //
    // This matters more than a usual regression test. The encoding is not
    // private to this code: a run writes packed diameters into an export, and a
    // post-processor written elsewhere decodes them afterwards. If the two ever
    // disagree about a single bit, every state read back is wrong and nothing
    // announces it. The vectors below pin the layout, the checksum polynomial
    // and the field positions all at once.
    struct vector { std::uint32_t carrier; std::uint32_t payload; std::uint32_t packed; };
    const vector vectors[] = {
        {0x38d1b717u, 0u, 0x38d15400u},    // 100 um
        {0x38d1b717u, 1u, 0x38d15401u},
        {0x38d1b717u, 255u, 0x38d154ffu},
        {0x38d1b717u, 511u, 0x38d155ffu},
        {0x358637bdu, 0u, 0x3586e200u},    // 1 um
        {0x358637bdu, 511u, 0x3586e3ffu},
        {0x3a83126fu, 0u, 0x3a83da00u},    // 1000 um
        {0x3a83126fu, 511u, 0x3a83dbffu},
        {0x3983126fu, 0u, 0x3983d400u},    // 250 um
        {0x3983126fu, 255u, 0x3983d4ffu},
        {0x3f800000u, 0u, 0x3f80cc00u},    // exactly 1, significand 1
        {0x3f800000u, 511u, 0x3f80cdffu},
        {0x381b3073u, 0u, 0x381bec00u},    // 37 um
        {0x381b3073u, 511u, 0x381bedffu},
    };
    for (const vector& v : vectors) {
        const float carrier = sc::from_bits(v.carrier);
        const float packed = gd::pack(carrier, v.payload);
        CHECK(ctx, sc::to_bits(packed) == v.packed);
        CHECK(ctx, gd::has_payload(packed));
        CHECK(ctx, gd::payload_of(packed) == v.payload);
    }

    ctx.note("payload states", gd::payload_max + 1.0);
    ctx.note("carrier given up, per cent", 100.0 * gd::carrier_error_above);

    return ctx.report();
}
