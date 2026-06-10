// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Micro-benchmarks for the division kernels: submul_single_limb throughput
// at production-realistic span lengths, the dependent-chain latencies of the
// preinv quotient steps and reciprocals, whole-call short division, and the
// wrapped-product-to-full-product ratio. These are the before/after numbers
// for the Phase D scalar and architecture work.
//
// Disabled by default; define BEMAN_BIG_INT_RUN_BENCHMARKS to run.
// Output is CSV on stdout: kernel,param,value (ns per limb, call, or digit;
// the mulmod rows report the wrapped/full ratio in the value column).

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/div_impl.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#include <gtest/gtest.h>

#include "benchmark_testing.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace local {

using uint_t           = ::beman::big_int::uint_multiprecision_t;
using stopwatch        = ::beman::big_int::benchmark_testing::stopwatch;
using std_allocator    = std::allocator<uint_t>;
using scratch_for_test = ::beman::big_int::detail::scratch_allocator<std_allocator>;

inline constexpr unsigned reps_per_point = 5;

constexpr uint_t top_bit = uint_t{1} << (::beman::big_int::detail::width_v<uint_t> - 1);

// NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
static std::mt19937_64 rng{0xd0ULL};

inline void fill_random(const std::span<uint_t> dest) {
    std::uniform_int_distribution<uint_t> dist;
    for (auto& x : dest) {
        x = dist(rng);
    }
    if (!dest.empty() && dest.back() == 0) {
        dest.back() = 1;
    }
}

void emit(const char* kernel, const std::size_t param, const double value) {
    std::cout << kernel << ',' << param << ',' << std::fixed << std::setprecision(3) << value << '\n';
}

// Throughput of result -= a * val over n-limb spans, ns per limb.
double submul_ns_per_limb(const std::size_t n) {
    std::vector<uint_t> r(n);
    std::vector<uint_t> a(n);
    fill_random(r);
    fill_random(a);
    const auto     r_view = std::span<uint_t>{r};
    const auto     a_view = std::span<const uint_t>{a};
    const unsigned iters  = static_cast<unsigned>(std::clamp<std::size_t>(20'000'000 / n, 16, 2'000'000));

    uint_t sink = 1;
    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            sink ^= ::beman::big_int::detail::submul_single_limb(r_view, a_view, sink | 1u);
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    if (sink == 42u) { // defeat dead-code elimination
        std::cout << "#\n";
    }
    return best * 1.0e9 / (static_cast<double>(iters) * static_cast<double>(n));
}

// Dependent-chain latency of one 3/2 quotient step, ns per digit (each
// iteration consumes the previous remainder, like the real digit loop).
double div_3by2_chain_ns() {
    const uint_t d1 = (static_cast<uint_t>(rng()) | top_bit);
    const uint_t d0 = static_cast<uint_t>(rng());
    const uint_t v  = ::beman::big_int::detail::reciprocal_word_3by2(d1, d0);

    uint_t         u2    = d1 - 1;
    uint_t         u1    = static_cast<uint_t>(rng());
    uint_t         u0    = static_cast<uint_t>(rng());
    const unsigned iters = 400'000;

    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            const auto step = ::beman::big_int::detail::div_3by2_preinv(u2, u1, u0, d1, d0, v);
            u2              = step.remainder.high_bits; // remainder < d keeps the precondition
            u1              = step.remainder.low_bits;
            u0 ^= step.quotient;
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    if ((u2 ^ u1 ^ u0) == 42u) {
        std::cout << "#\n";
    }
    return best * 1.0e9 / iters;
}

// Dependent-chain latency of the reciprocal setups, ns per call. The loop
// counter is folded into the chain so a fixed point cannot make the call
// loop-invariant (hoistable).
template <class F>
double reciprocal_chain_ns(F f) {
    uint_t         d     = static_cast<uint_t>(rng()) | top_bit;
    const unsigned iters = 200'000;

    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            d = (f(d) ^ static_cast<uint_t>(i)) | top_bit | 1u;
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    [[maybe_unused]] volatile uint_t sink = d; // the chain result must be observed
    return best * 1.0e9 / iters;
}

// Whole divide_unsigned_short calls (fresh divisor each call, like real
// small public divisions), ns per call.
double short_division_ns(const std::size_t m) {
    std::vector<uint_t> dividend(m);
    fill_random(dividend);
    std::vector<uint_t> quotient(m);
    uint_t              divisor = static_cast<uint_t>(rng()) | 1u;
    const unsigned      iters   = 200'000;

    double best = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        const stopwatch sw{};
        for (unsigned i = 0; i < iters; ++i) {
            const uint_t r = ::beman::big_int::detail::divide_unsigned_short(
                std::span<uint_t>{quotient}, std::span<const uint_t>{dividend}, divisor);
            divisor = (divisor ^ (r << 1)) | (uint_t{1} << 9) | 1u;
        }
        best = std::min(best, stopwatch::elapsed_time<double>(sw));
    }
    if (divisor == 42u) {
        std::cout << "#\n";
    }
    return best * 1.0e9 / iters;
}

// Wrapped product (multiply_mod_bnm1 at w = next_size(n + 1)) versus the
// full 2n-limb product of the same operands: the ratio the cyclic NTT entry
// is meant to push toward ~0.5 at FFT sizes.
double mulmod_ratio(const std::size_t n) {
    const std::size_t w = ::beman::big_int::detail::multiply_mod_bnm1_next_size(
        n + 1, ::beman::big_int::detail::multiply_mod_bnm1_cutoff);
    std::vector<uint_t> a(n);
    std::vector<uint_t> b(n);
    fill_random(a);
    fill_random(b);
    std::vector<uint_t> full(2 * n, 0);
    std::vector<uint_t> wrapped(w, 0);
    const auto          a_view = std::span<const uint_t>{a};
    const auto          b_view = std::span<const uint_t>{b};
    std_allocator       alloc;
    const unsigned      iters = static_cast<unsigned>(std::clamp<std::size_t>(3'000'000 / n, 1, 64));

    double best_full = 1.0e300;
    double best_mm   = 1.0e300;
    for (unsigned rep = 0; rep < reps_per_point; ++rep) {
        {
            const stopwatch sw{};
            for (unsigned i = 0; i < iters; ++i) {
                std::ranges::fill(full, uint_t{0});
                ::beman::big_int::detail::multiply_dispatch(std::span<uint_t>{full}, a_view, b_view, alloc);
            }
            best_full = std::min(best_full, stopwatch::elapsed_time<double>(sw));
        }
        {
            scratch_for_test scratch(::beman::big_int::detail::multiply_mod_bnm1_storage_size(w), alloc);
            const stopwatch  sw{};
            for (unsigned i = 0; i < iters; ++i) {
                ::beman::big_int::detail::multiply_mod_bnm1(std::span<uint_t>{wrapped}, a_view, b_view, scratch,
                                                            alloc);
            }
            best_mm = std::min(best_mm, stopwatch::elapsed_time<double>(sw));
        }
    }
    return best_mm / best_full;
}

void run_all() {
    std::cout << "kernel,param,value\n";

    for (const std::size_t n : {std::size_t{8}, std::size_t{16}, std::size_t{32}, std::size_t{40}, std::size_t{64},
                                std::size_t{256}, std::size_t{4096}}) {
        emit("submul_ns_per_limb", n, submul_ns_per_limb(n));
        std::cout.flush();
    }

    emit("div_3by2_chain_ns", 0, div_3by2_chain_ns());
    emit("reciprocal_word_ns", 0, reciprocal_chain_ns([](const uint_t d) {
             return ::beman::big_int::detail::reciprocal_word(d) ^ d;
         }));
    emit("reciprocal_word_3by2_ns", 0, reciprocal_chain_ns([](const uint_t d) {
             return ::beman::big_int::detail::reciprocal_word_3by2(d, ~d) ^ d;
         }));
    std::cout.flush();

    for (const std::size_t m : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{6}, std::size_t{8}}) {
        emit("short_division_ns", m, short_division_ns(m));
    }
    std::cout.flush();

    for (const std::size_t n :
         {std::size_t{4096}, std::size_t{16384}, std::size_t{65536}, std::size_t{262144}}) {
        emit("mulmod_over_full", n, mulmod_ratio(n));
        std::cout.flush();
    }
}

} // namespace local

TEST(Division, KernelMicroBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_all();
    SUCCEED();
#else
    GTEST_SKIP() << "Stress benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
