// Test 12: benchmark.
//
// The first question anyone asks is whether it slows the tracking down, and the
// answer has to be a number. An absolute nanosecond figure means nothing on its
// own, so it is reported against the cost of one particle position update, on
// the same machine in the same run.
//
// The reference is deliberately the cheapest honest one: a drag relaxation and
// a three-component integration, no cell search, no interpolation of the
// carrier phase, no rebound test. Real Lagrangian tracking costs a great deal
// more per particle per step than this, so the ratio reported here is an upper
// bound on the relative cost of the channel, not a typical value. Making the
// reference more realistic would only make the channel look cheaper.
//
// Each measurement is repeated and the fastest run is taken, which is the usual
// choice for a microbenchmark: the slow runs are the operating system, not the
// code.

#include "check.hpp"
#include "state_channel.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace sc = state_channel;

namespace {

constexpr int particle_count = 4096;
constexpr int repeats = 512;
constexpr int trials = 7;

// Turning auto-vectorisation off for one loop, so that the channel can be
// compared against a baseline compiled the way the channel loop is forced to
// be. Without this the comparison charges the channel for the vectorisation it
// costs the compiler, which is a real effect but a separate one.
#if defined(_MSC_VER)
#  define BENCH_NO_VECTOR __pragma(loop(no_vector))
#  define BENCH_NO_VECTOR_FN
#elif defined(__clang__)
#  define BENCH_NO_VECTOR _Pragma("clang loop vectorize(disable)")
#  define BENCH_NO_VECTOR_FN
#elif defined(__GNUC__)
#  define BENCH_NO_VECTOR
#  define BENCH_NO_VECTOR_FN __attribute__((optimize("no-tree-vectorize")))
#else
#  define BENCH_NO_VECTOR
#  define BENCH_NO_VECTOR_FN
#endif

struct particles {
    std::vector<float> diameter;
    std::vector<float> x, y, z;
    std::vector<float> vx, vy, vz;
    std::vector<std::uint32_t> code;
};

particles make_particles() {
    std::mt19937 rng(20260908u);
    std::uniform_real_distribution<double> log_diameter(-6.0, -3.0);
    std::uniform_real_distribution<float> velocity(-50.0f, 50.0f);
    std::uniform_int_distribution<std::uint32_t> payloads(0u, sc::payload_max);

    particles p;
    p.diameter.reserve(particle_count);
    for (int i = 0; i < particle_count; ++i) {
        p.diameter.push_back(static_cast<float>(std::pow(10.0, log_diameter(rng))));
        p.x.push_back(velocity(rng));
        p.y.push_back(velocity(rng));
        p.z.push_back(velocity(rng));
        p.vx.push_back(velocity(rng));
        p.vy.push_back(velocity(rng));
        p.vz.push_back(velocity(rng));
        p.code.push_back(payloads(rng));
    }
    return p;
}

// One position update. Drag relaxation towards a uniform carrier velocity, then
// an explicit Euler step. Two divisions and about a dozen flops, which is the
// floor of what a tracker does per particle per step.
inline void advance(float& x, float& y, float& z, float& vx, float& vy, float& vz,
                    float diameter, float dt) {
    const float tau = diameter * diameter * 1.0e6f;  // stand in for a relaxation time
    const float k = dt / (tau + dt);
    vx += k * (10.0f - vx);
    vy += k * (0.0f - vy);
    vz += k * (-9.81f * tau - vz);
    x += vx * dt;
    y += vy * dt;
    z += vz * dt;
}

double best_nanoseconds_per_op(double (*measure)(particles&), particles& p) {
    double best = 1.0e300;
    for (int t = 0; t < trials; ++t) {
        const double ns = measure(p);
        if (ns < best) best = ns;
    }
    return best;
}

// Sinks, written to so that nothing measured can be optimised away.
volatile std::uint32_t bit_sink = 0u;
volatile float float_sink = 0.0f;

double measure_pack(particles& p) {
    const auto start = std::chrono::steady_clock::now();
    std::uint32_t accumulator = 0u;
    for (int r = 0; r < repeats; ++r) {
        for (int i = 0; i < particle_count; ++i) {
            accumulator ^= sc::to_bits(sc::pack(p.diameter[i], p.code[i]));
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    bit_sink = accumulator;
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return ns / (static_cast<double>(repeats) * particle_count);
}

double measure_unpack(particles& p) {
    // Pack once, outside the timed region, so this measures the read alone.
    std::vector<float> packed(particle_count);
    for (int i = 0; i < particle_count; ++i) {
        packed[i] = sc::pack(p.diameter[i], p.code[i]);
    }

    const auto start = std::chrono::steady_clock::now();
    std::uint32_t accumulator = 0u;
    for (int r = 0; r < repeats; ++r) {
        for (int i = 0; i < particle_count; ++i) {
            std::uint32_t payload = 0u;
            if (sc::try_unpack(packed[i], payload)) accumulator += payload;
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    bit_sink = accumulator;
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return ns / (static_cast<double>(repeats) * particle_count);
}

double measure_advance(particles& p) {
    const auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < repeats; ++r) {
        for (int i = 0; i < particle_count; ++i) {
            advance(p.x[i], p.y[i], p.z[i], p.vx[i], p.vy[i], p.vz[i], p.diameter[i], 1.0e-6f);
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    float_sink = p.x[0] + p.y[0] + p.z[0];
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return ns / (static_cast<double>(repeats) * particle_count);
}

// The same baseline with vectorisation off. This is the honest comparison for
// the channel, because the channel loop cannot vectorise and a tracking loop in
// a real solver, which does a cell search and interpolates the carrier phase
// per particle, does not vectorise either.
BENCH_NO_VECTOR_FN double measure_advance_scalar(particles& p) {
    const auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < repeats; ++r) {
        BENCH_NO_VECTOR
        for (int i = 0; i < particle_count; ++i) {
            advance(p.x[i], p.y[i], p.z[i], p.vx[i], p.vy[i], p.vz[i], p.diameter[i], 1.0e-6f);
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    float_sink = p.x[0] + p.y[0] + p.z[0];
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return ns / (static_cast<double>(repeats) * particle_count);
}

// The same loop with the channel in it, which is what the solver runs: read the
// state out of the diameter, advance it, write it back, then use the diameter.
//
// This is the measurement that answers the question. The two standalone numbers
// above compare a branchy scalar loop against one the compiler is free to
// vectorise, so their ratio says as much about auto-vectorisation as about the
// channel. Here both timings come from the same loop compiled the same way, and
// the difference between them is the cost of carrying the state.
double measure_advance_with_channel(particles& p) {
    std::vector<float> carrier(p.diameter);
    for (int i = 0; i < particle_count; ++i) {
        carrier[i] = sc::pack(carrier[i], p.code[i]);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < repeats; ++r) {
        for (int i = 0; i < particle_count; ++i) {
            std::uint32_t state = 0u;
            if (sc::try_unpack(carrier[i], state)) {
                if (state < sc::payload_max) ++state;  // a sub-threshold impact
            }
            carrier[i] = sc::pack(carrier[i], state);
            advance(p.x[i], p.y[i], p.z[i], p.vx[i], p.vy[i], p.vz[i], carrier[i], 1.0e-6f);
        }
    }
    const auto stop = std::chrono::steady_clock::now();
    float_sink = p.x[0] + p.y[0] + p.z[0] + carrier[0];
    const double ns = std::chrono::duration<double, std::nano>(stop - start).count();
    return ns / (static_cast<double>(repeats) * particle_count);
}

}  // namespace

int main() {
    check::context ctx("benchmark");

    particles p = make_particles();

    const double pack_ns = best_nanoseconds_per_op(measure_pack, p);
    const double unpack_ns = best_nanoseconds_per_op(measure_unpack, p);
    const double advance_ns = best_nanoseconds_per_op(measure_advance, p);
    const double scalar_ns = best_nanoseconds_per_op(measure_advance_scalar, p);
    const double with_channel_ns = best_nanoseconds_per_op(measure_advance_with_channel, p);
    const double added_ns = with_channel_ns - scalar_ns;
    const double vectorisation_cost_ns = scalar_ns - advance_ns;

    ctx.note("pack, ns", pack_ns);
    ctx.note("unpack, ns", unpack_ns);
    ctx.note("one position update, vectorised, ns", advance_ns);
    ctx.note("one position update, scalar, ns", scalar_ns);
    ctx.note("the same update carrying state, ns", with_channel_ns);
    ctx.note("added by the channel, ns", added_ns);
    ctx.note("added, as a fraction of one scalar update", added_ns / scalar_ns);
    ctx.note("vectorisation forgone, ns", vectorisation_cost_ns);

    // Not a performance assertion so much as a guard against the benchmark
    // silently measuring nothing, or measuring the operating system. Both would
    // otherwise be reported as a very good result.
    CHECK(ctx, pack_ns > 0.0);
    CHECK(ctx, unpack_ns > 0.0);
    CHECK(ctx, advance_ns > 0.0);
    CHECK(ctx, with_channel_ns > 0.0);
    CHECK(ctx, pack_ns < 100.0);
    CHECK(ctx, unpack_ns < 100.0);
    CHECK(ctx, added_ns > 0.0);  // it is not free, and a zero here means a bug

    return ctx.report();
}
