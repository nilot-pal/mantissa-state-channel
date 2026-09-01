// Test 4: guards, non-finite.
//
// Infinities and NaN are refused and returned bit for bit unchanged. Bit for
// bit matters: a NaN carries a payload of its own in the mantissa, and quietly
// rewriting it would destroy whatever diagnostic put it there. It can also turn
// a signalling NaN into a quiet one, which changes the behaviour of the host
// solver rather than merely its data.

#include "check.hpp"
#include "state_channel.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace sc = state_channel;

int main() {
    check::context ctx("guards, non-finite");

    const std::uint32_t patterns[] = {
        0x7F800000u,  // +inf
        0xFF800000u,  // -inf
        0x7FC00000u,  // quiet NaN, canonical
        0xFFC00000u,  // quiet NaN, negative
        0x7FFFFFFFu,  // quiet NaN, all payload bits set
        0x7F800001u,  // signalling NaN, smallest payload
        0x7FBFFFFFu,  // signalling NaN, largest payload
        0x7F804000u,  // NaN whose flag bit is set
        0x7FC0002Au,  // NaN carrying a diagnostic value
    };

    for (const std::uint32_t bits : patterns) {
        const float v = sc::from_bits(bits);

        CHECK(ctx, !sc::admissible(v));
        CHECK(ctx, !sc::has_payload(v));

        // Refused, and returned exactly as given.
        for (std::uint32_t payload : {0u, 1u, sc::payload_max, 0xFFFFFFFFu}) {
            CHECK(ctx, sc::to_bits(sc::pack(v, payload)) == bits);
        }
        CHECK(ctx, sc::to_bits(sc::strip(v)) == bits);

        std::uint32_t recovered = 0xBEEFu;
        CHECK(ctx, !sc::try_unpack(v, recovered));
        CHECK(ctx, recovered == 0xBEEFu);
        CHECK(ctx, sc::payload_of(v) == 0u);
    }

    // The classification is done on the integer bits, never by comparing the
    // float, so it does not depend on how the compiler treats NaN in a
    // comparison. A NaN compares false against everything including itself, and
    // a guard written as a comparison is easy to get subtly wrong.
    const float nan_value = sc::from_bits(0x7FC00000u);
    CHECK(ctx, std::isnan(nan_value));
    CHECK(ctx, !(nan_value == nan_value));
    CHECK(ctx, !sc::admissible(nan_value));

    const float inf_value = sc::from_bits(0x7F800000u);
    CHECK(ctx, std::isinf(inf_value));
    CHECK(ctx, inf_value > 0.0f);  // positive, and still refused
    CHECK(ctx, !sc::admissible(inf_value));

    return ctx.report();
}
