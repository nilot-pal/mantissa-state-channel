#ifndef STATE_CHANNEL_HPP
#define STATE_CHANNEL_HPP

// A per-object state channel carried in the low mantissa bits of an IEEE-754
// binary32 field that already migrates with the object.
//
// The carrier here is a particle diameter. Nothing in the code depends on that;
// it depends only on the carrier being a positive normal float whose low bits
// are below the resolution anything downstream can act on.
//
// Header only, C++17, no dependencies.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace state_channel {

// Layout of the channel inside the 23 explicit mantissa bits.
//
//   bit 22 ......... bit 15 | bit 14 | bit 13 ......... bit 0
//   carrier, 8 bits kept    | flag   | payload, 14 bits
//
// The flag occupies the top bit of the field so that an unflagged carrier is
// distinguishable from a flagged carrier whose payload happens to be zero.
// Without it the fallback path is unreachable.

inline constexpr int mantissa_bits = 23;
inline constexpr int payload_bits = 14;
inline constexpr int field_bits = payload_bits + 1;

inline constexpr std::uint32_t payload_max = (std::uint32_t(1) << payload_bits) - 1u;
inline constexpr std::uint32_t flag_mask = std::uint32_t(1) << payload_bits;
inline constexpr std::uint32_t field_mask = (std::uint32_t(1) << field_bits) - 1u;
inline constexpr std::uint32_t exponent_mask = 0x7F800000u;
inline constexpr std::uint32_t sign_mask = 0x80000000u;

// The same layout at any payload width, so that the cost of a different choice
// can be derived rather than guessed. The constants below are these functions
// evaluated at payload_bits, and a test asserts they agree.
//
// Widening the payload buys states and costs carrier precision, and the two
// move in opposite directions at different rates. That trade is the whole
// design decision, and it is worth being able to compute rather than argue.
constexpr std::uint32_t payload_max_for(int bits) {
    return (std::uint32_t(1) << bits) - 1u;
}

constexpr double carrier_error_above_for(int bits) {
    return static_cast<double>((std::uint32_t(1) << (bits + 1)) - 1u) /
           static_cast<double>(std::uint32_t(1) << mantissa_bits);
}

constexpr double carrier_error_below_for(int bits) {
    return static_cast<double>((std::uint32_t(1) << bits) - 1u) /
           static_cast<double>(std::uint32_t(1) << mantissa_bits);
}

// Relative step between adjacent codes for a log scale of the given span.
inline double state_resolution_for(int bits, double lo, double hi) {
    return std::pow(hi / lo, 1.0 / static_cast<double>(payload_max_for(bits))) - 1.0;
}

// Worst case relative perturbation of the carrier, at a significand of 1 where
// it is largest. The tests assert these relationships rather than a magic
// number: change payload_bits and the bounds move with them.
//
// The perturbation is not symmetric, and it is not a plain truncation either.
// The field is masked away and then the flag and the payload are written back
// into it, so the packed carrier lands somewhere in the upper half of the
// truncation interval. It can therefore sit further above the true value than
// below it:
//
//   above  the whole field, flag_mask + payload_max, over the mantissa width
//   below  the flag bit alone minus one, over the mantissa width
//
// The magnitude bound is the larger of the two.
inline constexpr double carrier_error_above = carrier_error_above_for(payload_bits);
inline constexpr double carrier_error_below = carrier_error_below_for(payload_bits);
inline constexpr double carrier_error_bound = carrier_error_above;

// Type punning through memcpy. Not a union, not reinterpret_cast: those are a
// strict aliasing violation in C++ and the optimiser is entitled to act on it.
// Every compiler in use turns this into a register move.
inline std::uint32_t to_bits(float x) {
    std::uint32_t b;
    std::memcpy(&b, &x, sizeof b);
    return b;
}

inline float from_bits(std::uint32_t b) {
    float x;
    std::memcpy(&x, &b, sizeof x);
    return x;
}

// A carrier can hold a payload only if rewriting its low mantissa bits cannot
// move it into a class the host solver does not expect. That rules out three
// things, and the production version guards exactly these three:
//
//   negative and negative zero  a diameter is positive by definition
//   zero and denormal           writing mantissa bits would fabricate a value
//                               out of nothing, or leave the denormal class
//   infinity and NaN            the mantissa is not a number there
//
// Refusal is not an error path. The caller proceeds with no history and the
// host solver stays correct.
inline bool admissible(float d) {
    const std::uint32_t b = to_bits(d);
    if ((b & sign_mask) != 0u) return false;
    const std::uint32_t biased_exponent = (b & exponent_mask) >> mantissa_bits;
    if (biased_exponent == 0u) return false;
    if (biased_exponent == 0xFFu) return false;
    return true;
}

// True only for an admissible carrier with the flag set. An inadmissible value
// may have bit 14 set for reasons of its own; it is not a channel.
inline bool has_payload(float d) {
    return admissible(d) && (to_bits(d) & flag_mask) != 0u;
}

// Returns the carrier untouched if it cannot safely hold a payload. A payload
// above the maximum clamps; it never wraps, because a wrapped state is a wrong
// state that looks right.
inline float pack(float carrier, std::uint32_t payload) {
    if (!admissible(carrier)) return carrier;
    if (payload > payload_max) payload = payload_max;
    const std::uint32_t b = to_bits(carrier);
    return from_bits((b & ~field_mask) | flag_mask | payload);
}

// Valid only where has_payload is true. Returns zero otherwise, which is why
// the flag exists and why callers test it first.
inline std::uint32_t payload_of(float d) {
    return has_payload(d) ? (to_bits(d) & payload_max) : 0u;
}

inline bool try_unpack(float d, std::uint32_t& payload) {
    if (!has_payload(d)) return false;
    payload = to_bits(d) & payload_max;
    return true;
}

// Clears the field, giving the truncated carrier. A value without a payload is
// returned bit for bit unchanged.
//
// This does not recover the original diameter. The low bits are gone at pack
// time and nothing brings them back; strip only removes the part this code put
// there. It is the right call for a consumer that would rather read low than
// read wrong.
inline float strip(float d) {
    if (!has_payload(d)) return d;
    return from_bits(to_bits(d) & ~field_mask);
}

// ---------------------------------------------------------------------------
// The guarded channel: a checksum instead of a flag.
//
// The design above spends one bit saying "there is a payload here". That bit
// cannot tell a carrier this code packed from a carrier it never saw, because
// half of all diameters have it set already, and it cannot tell a packed
// carrier from one that some other routine has since overwritten. Both read as
// valid and return a payload that is not a payload.
//
// Spending seven bits on a checksum over the carrier instead of one bit on a
// flag answers both questions. The checksum is taken over the exponent and the
// mantissa bits that survive encoding, so:
//
//   a carrier that was never packed    accepts with probability 1 in 128
//   a carrier written by anything else fails the check and falls back
//
// The second is the one that matters. It converts the worst failure mode of the
// flag design, a wrong state wearing the costume of a valid one, into an
// ordinary refusal.
//
// It is not free. The field grows to 16 bits, so the carrier gives up 0.78 per
// cent instead of 0.39, and the payload drops from 14 bits to 9, from 16384
// states to 512. Whether that is a good trade depends on how much resolution
// the state actually needs, which is a question about the model rather than
// about the channel: the resolution only has to be finer than the scatter in
// the quantity being carried.
// ---------------------------------------------------------------------------
namespace guarded {

inline constexpr int payload_bits = 9;
inline constexpr int checksum_bits = 7;
inline constexpr int field_bits = payload_bits + checksum_bits;

inline constexpr std::uint32_t payload_max = (std::uint32_t(1) << payload_bits) - 1u;
inline constexpr std::uint32_t checksum_max = (std::uint32_t(1) << checksum_bits) - 1u;
inline constexpr std::uint32_t field_mask = (std::uint32_t(1) << field_bits) - 1u;

inline constexpr double carrier_error_above = carrier_error_above_for(field_bits - 1);
inline constexpr double carrier_error_below = carrier_error_below_for(field_bits - 1);
inline constexpr double carrier_error_bound = carrier_error_above;

// One in this many carriers that were never packed will pass the check anyway.
// There is no way to drive it to zero, only down, and every bit spent comes out
// of the payload.
inline constexpr double false_accept_rate = 1.0 / static_cast<double>(checksum_max + 1u);

// Checksum over the parts of the carrier that survive encoding: the biased
// exponent, and the mantissa bits above the field. The two shifted exclusive
// ors are an avalanche step, so that carriers differing in one bit do not
// produce checksums differing in one bit.
inline std::uint32_t checksum_of(std::uint32_t biased_exponent, std::uint32_t base_mantissa) {
    std::uint32_t h = (base_mantissa >> field_bits) ^ biased_exponent;
    h ^= h >> 4;
    h ^= h >> 2;
    return h & checksum_max;
}

inline std::uint32_t checksum_of(float carrier) {
    const std::uint32_t b = to_bits(carrier);
    const std::uint32_t biased_exponent = (b & exponent_mask) >> mantissa_bits;
    const std::uint32_t base = b & 0x007FFFFFu & ~field_mask;
    return checksum_of(biased_exponent, base);
}

// The same three guards, and the same refusal.
inline float pack(float carrier, std::uint32_t payload) {
    if (!admissible(carrier)) return carrier;
    if (payload > payload_max) payload = payload_max;

    const std::uint32_t b = to_bits(carrier);
    const std::uint32_t base = b & ~field_mask;
    const std::uint32_t biased_exponent = (b & exponent_mask) >> mantissa_bits;
    const std::uint32_t chk = checksum_of(biased_exponent, base & 0x007FFFFFu);

    return from_bits(base | (chk << payload_bits) | payload);
}

// Accepts the payload only if its checksum still matches the carrier it was
// written against. A carrier that has been modified since packing fails here,
// which is the whole reason the checksum exists.
inline bool has_payload(float d) {
    if (!admissible(d)) return false;
    const std::uint32_t stored = (to_bits(d) & field_mask) >> payload_bits;
    return stored == checksum_of(d);
}

inline bool try_unpack(float d, std::uint32_t& payload) {
    if (!has_payload(d)) return false;
    payload = to_bits(d) & payload_max;
    return true;
}

inline std::uint32_t payload_of(float d) {
    return has_payload(d) ? (to_bits(d) & payload_max) : 0u;
}

inline float strip(float d) {
    if (!has_payload(d)) return d;
    return from_bits(to_bits(d) & ~field_mask);
}

}  // namespace guarded

// Log quantisation of the state variable onto the payload codes.
//
// A damage state has multiplicative resolution: the difference between 1% and
// 2% matters, the difference between 80% and 81% does not. Log spacing spends
// the codes where the physics can use them. The range belongs to the caller; it
// is a property of the state variable, not of the channel.
class log_scale {
public:
    constexpr log_scale(double lo, double hi) : lo_(lo), hi_(hi) {}

    // Out of range clamps to the ends. Wrapping a saturated state round to zero
    // would turn a fully damaged particle into a pristine one.
    std::uint32_t encode(double state) const {
        if (!(state > lo_)) return 0u;
        if (state >= hi_) return payload_max;
        const double t = std::log(state / lo_) / std::log(hi_ / lo_);
        const double code = std::floor(t * static_cast<double>(payload_max) + 0.5);
        if (code <= 0.0) return 0u;
        if (code >= static_cast<double>(payload_max)) return payload_max;
        return static_cast<std::uint32_t>(code);
    }

    double decode(std::uint32_t code) const {
        if (code == 0u) return lo_;
        if (code >= payload_max) return hi_;
        return lo_ * std::pow(hi_ / lo_,
                              static_cast<double>(code) / static_cast<double>(payload_max));
    }

    // Relative step between adjacent codes, constant by construction.
    double resolution() const {
        return std::pow(hi_ / lo_, 1.0 / static_cast<double>(payload_max)) - 1.0;
    }

private:
    double lo_;
    double hi_;
};

}  // namespace state_channel

#endif
