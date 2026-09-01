// Test 10: type-punning safety.
//
// Conversion is through memcpy. Not reinterpret_cast, which is a strict
// aliasing violation the optimiser is entitled to act on, and not a union,
// which is defined behaviour in C and not in C++ whatever every compiler
// happens to do. std::bit_cast would be the modern answer and it is C++20.
//
// What a test can and cannot show here is worth being straight about. A test
// cannot prove the absence of undefined behaviour: code with a strict aliasing
// bug usually passes at low optimisation and fails at high. So this asserts the
// observable contract, exercises it at the optimisation level the library is
// built at, and the real guarantee comes from memcpy being the construct the
// standard blesses rather than from anything below.

#include "check.hpp"
#include "state_channel.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <random>

namespace sc = state_channel;

static_assert(sizeof(float) == sizeof(std::uint32_t), "binary32 assumed");
static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 assumed");
static_assert(std::numeric_limits<float>::digits == 24, "23 explicit mantissa bits assumed");

int main() {
    check::context ctx("type-punning safety");

    std::mt19937 rng(20260906u);
    std::uniform_int_distribution<std::uint32_t> any_bits(0u, 0xFFFFFFFFu);

    // The conversions are exact inverses over every bit pattern, including the
    // ones that are not numbers. Going through the float and back must not
    // canonicalise a NaN or flush a denormal, which is the failure mode that
    // shows up when a compiler moves the value through an x87 register or a
    // flush-to-zero mode is set.
    for (int i = 0; i < 5000000; ++i) {
        const std::uint32_t bits = any_bits(rng);
        CHECK(ctx, sc::to_bits(sc::from_bits(bits)) == bits);
    }

    // The boundary patterns explicitly, since random sampling reaches them
    // rarely and they are where a flush-to-zero or a NaN canonicalisation shows.
    const std::uint32_t patterns[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu,
        0x00800000u, 0x7F7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7F800001u,
        0x7FBFFFFFu, 0x7FC00000u, 0xFFFFFFFFu, 0x3F800000u, 0x40000000u,
    };
    for (const std::uint32_t bits : patterns) {
        CHECK(ctx, sc::to_bits(sc::from_bits(bits)) == bits);
    }

    // And the other direction, from float to bits and back.
    std::uniform_real_distribution<float> diameters(1.0e-6f, 1.0e-3f);
    for (int i = 0; i < 1000000; ++i) {
        const float d = diameters(rng);
        CHECK(ctx, sc::from_bits(sc::to_bits(d)) == d);
    }

    // The conversion agrees with a memcpy written out by hand, which is the
    // whole of what it claims to be.
    for (int i = 0; i < 100000; ++i) {
        const std::uint32_t bits = any_bits(rng);
        float via_library = sc::from_bits(bits);
        float via_memcpy;
        std::memcpy(&via_memcpy, &bits, sizeof via_memcpy);
        std::uint32_t back_library = sc::to_bits(via_library);
        std::uint32_t back_memcpy;
        std::memcpy(&back_memcpy, &via_memcpy, sizeof back_memcpy);
        CHECK(ctx, back_library == back_memcpy);
        CHECK(ctx, back_library == bits);
    }

    // A packed value survives being copied about as raw storage, which is what
    // happens to it in the solver: it is written into a buffer, sent across a
    // rank boundary and read back as a float by code that knows nothing about
    // any of this.
    std::uniform_int_distribution<std::uint32_t> payloads(0u, sc::payload_max);
    for (int i = 0; i < 200000; ++i) {
        const float carrier = diameters(rng);
        const std::uint32_t payload = payloads(rng);
        const float packed = sc::pack(carrier, payload);

        unsigned char buffer[sizeof(float)];
        std::memcpy(buffer, &packed, sizeof buffer);
        float received;
        std::memcpy(&received, buffer, sizeof received);

        CHECK(ctx, sc::to_bits(received) == sc::to_bits(packed));
        CHECK(ctx, sc::payload_of(received) == payload);
        CHECK(ctx, received == packed);
    }

    return ctx.report();
}
