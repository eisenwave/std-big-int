// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Stress benchmark for tuning the Karatsuba and Toom-Cook 3 cutoffs.
// Sweeps each algorithm (schoolbook, Karatsuba, Toom-Cook 3, ...) over a
// range of limb counts and prints average time per multiplication.
//
// Disabled by default. To run:
//   cmake --preset appleclang-release -DBEMAN_BIG_INT_RUN_BENCHMARKS=ON
//   cmake --build build/appleclang-release
//   ./build/appleclang-release/tests/beman/big_int/beman.big_int.tests.multiplication_stress_bench
//
// Output is CSV on stdout (algorithm,limbs,trials,ns_per_mul). Pipe to a file
// or to a plotting script to find crossover points.
//
// To add a new algorithm (Toom-Cook 4, 5, ...):
//   1. Add a `run_toom_cook_4_at` function that calls the new
//      `detail::multiply_toom_cook_4` and returns ns per multiplication.
//   2. Append an entry to the `algorithms[]` table below with sensible
//      min/max limb counts.
//   3. Extend `sweep_limbs[]` if the new algorithm is interesting at limb
//      counts not already covered.

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/mul_impl.hpp>

#include <gtest/gtest.h>

#include "benchmark_testing.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace local {

using uint_t           = ::beman::big_int::uint_multiprecision_t;
using stopwatch        = ::beman::big_int::benchmark_testing::stopwatch;
using std_allocator    = std::allocator<uint_t>;
using scratch_for_test = ::beman::big_int::detail::scratch_allocator<std_allocator>;

// Fill a span with random limbs and force the top limb non-zero so the
// operand actually has `dest.size()` significant limbs (no accidental trim).
inline void fill_random(const std::span<uint_t> dest, std::mt19937_64& rng) {
    std::uniform_int_distribution<uint_t> dist;
    for (auto& x : dest) {
        x = dist(rng);
    }
    if (!dest.empty() && dest.back() == 0) {
        dest.back() = 1;
    }
}

// Run `algo(result, a, b, scratch)` for `trials` iterations after one warmup
// and return the average wall time in nanoseconds. `scratch_size` is the
// total limbs to pre-allocate for the algorithm's scratch (0 -> no scratch
// needed, but we still construct a minimal allocator to keep the lambda
// signature uniform).
template <class Algo>
double measure_algorithm(const std::size_t limbs, const unsigned trials, const std::size_t scratch_size, Algo algo) {
    std::mt19937_64     rng{0xC0FFEEULL};
    std::vector<uint_t> a(limbs);
    std::vector<uint_t> b(limbs);
    fill_random(a, rng);
    fill_random(b, rng);

    std::vector<uint_t> result(2 * limbs, uint_t{0});

    std_allocator    alloc;
    scratch_for_test scratch(std::max<std::size_t>(scratch_size, 1), alloc);

    const auto a_view = std::span<const uint_t>{a};
    const auto b_view = std::span<const uint_t>{b};
    const auto r_view = std::span<uint_t>{result};

    // Warm caches / branch predictors with one untimed run.
    std::ranges::fill(result, uint_t{0});
    algo(r_view, a_view, b_view, scratch);

    const stopwatch sw{};
    for (unsigned i = 0; i < trials; ++i) {
        std::ranges::fill(result, uint_t{0});
        algo(r_view, a_view, b_view, scratch);
    }
    const double seconds = stopwatch::elapsed_time<double>(sw);
    return (seconds * 1.0e9) / static_cast<double>(trials);
}

// Per-algorithm runners. Each invokes the algorithm directly so we measure
// it in isolation (no dispatcher overhead, no cutoff fallback above the
// algorithm's own internal threshold).

double run_long_at(const std::size_t limbs, const unsigned trials) {
    return measure_algorithm(
        limbs,
        trials,
        /*scratch_size=*/0,
        [](const std::span<uint_t>       r,
           const std::span<const uint_t> a,
           const std::span<const uint_t> b,
           scratch_for_test&) { ::beman::big_int::detail::multiply_long(r.first(a.size() + b.size()), a, b); });
}

double run_karatsuba_at(const std::size_t limbs, const unsigned trials) {
    return measure_algorithm(limbs,
                             trials,
                             ::beman::big_int::detail::karatsuba_storage_size(limbs),
                             [](const std::span<uint_t>       r,
                                const std::span<const uint_t> a,
                                const std::span<const uint_t> b,
                                scratch_for_test&             s) {
                                 ::beman::big_int::detail::multiply_karatsuba(r.first(a.size() + b.size()), a, b, s);
                             });
}

double run_toom_cook_3_at(const std::size_t limbs, const unsigned trials) {
    return measure_algorithm(limbs,
                             trials,
                             ::beman::big_int::detail::toom_cook_3_storage_size(limbs),
                             [](const std::span<uint_t>       r,
                                const std::span<const uint_t> a,
                                const std::span<const uint_t> b,
                                scratch_for_test&             s) {
                                 ::beman::big_int::detail::multiply_toom_cook_3(r.first(a.size() + b.size()), a, b, s);
                             });
}

double run_toom_cook_4_at(const std::size_t limbs, const unsigned trials) {
    // cutoff_override=1 forces Toom-4 to run at any input size that clears the
    // 3*k correctness gate (small inputs still fall through). Recursive sub-products
    // use the default cutoff so they fall back to Toom-3 / Karatsuba / schoolbook
    // as they would in production.
    return measure_algorithm(limbs,
                             trials,
                             ::beman::big_int::detail::toom_cook_4_storage_size(limbs),
                             [](const std::span<uint_t>       r,
                                const std::span<const uint_t> a,
                                const std::span<const uint_t> b,
                                scratch_for_test&             s) {
                                 ::beman::big_int::detail::multiply_toom_cook_4(
                                     r.first(a.size() + b.size()), a, b, s, std::size_t{1});
                             });
}

double run_toom_cook_6_5_at(const std::size_t limbs, const unsigned trials) {
    // cutoff_override=1 forces Toom-6.5 to run at any input size that clears the
    // 5*k / 7*k correctness gates. Recursive sub-products use the default cutoff so
    // they fall back to Toom-4 / Toom-3 / Karatsuba / schoolbook as in production.
    return measure_algorithm(limbs,
                             trials,
                             ::beman::big_int::detail::toom_cook_6_5_storage_size(limbs),
                             [](const std::span<uint_t>       r,
                                const std::span<const uint_t> a,
                                const std::span<const uint_t> b,
                                scratch_for_test&             s) {
                                 ::beman::big_int::detail::multiply_toom_cook_6_5(
                                     r.first(a.size() + b.size()), a, b, s, std::size_t{1});
                             });
}

// Registry of algorithms to sweep. Each entry constrains the sweep to a
// reasonable range: too small and the algorithm's own internal cutoff just
// falls back to the previous tier; too large and a single multiplication
// dominates the whole run.
struct algorithm_runner {
    std::string_view name;
    std::size_t      min_limbs;
    std::size_t      max_limbs;
    double (*run)(std::size_t limbs, unsigned trials);
};

constexpr algorithm_runner algorithms[] = {
    {"schoolbook", 2, 200, run_long_at},
    {"karatsuba", 4, 2000, run_karatsuba_at},
    {"toom-cook-3", 300, 10000, run_toom_cook_3_at},
    {"toom-cook-4", 300, 30000, run_toom_cook_4_at},
    {"toom-cook-6.5", 1000, 80000, run_toom_cook_6_5_at},
};

// Limb counts to sample. Dense coverage at the low end for the
// schoolbook -> Karatsuba crossover; extra-dense in the 1200-2400 zone for
// the Toom-3 -> Toom-4 crossover and in the 2500-4500 zone for the
// Toom-4 -> Toom-6.5 crossover, where measurement noise can produce false
// first-crossings; then a coarser tail to verify asymptotic behaviour.
constexpr std::size_t sweep_limbs[] = {
    // Schoolbook / Karatsuba region (dense, fast).
    2,    4,    6,    8,    10,   12,   14,   16,   20,   24,   28,
    32,   36,   40,   48,   56,   64,   72,   80,   96,   112,  128,
    144,  160,  192,  224,  256,
    // Karatsuba ramp.
    300,  400,  500,  550,  600,  650,  700,  750,
    // Toom-3 region.
    800,  900,  1000, 1100, 1200, 1300, 1400,
    // Toom-3 -> Toom-4 transition (cutoff 1400; densify to resolve the dip).
    1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2400,
    // Toom-4 region.
    2500, 2700, 2900,
    // Toom-4 -> Toom-6.5 transition (cutoff 3000; densify likewise).
    3000, 3100, 3300, 3500, 3700, 4000, 4500,
    // Asymptotic tail.
    5000, 6000, 7500, 10000, 15000, 20000, 30000, 50000, 80000,
};

// Aim for ~0.2-1 second per data point. Trial counts taper as the cost per
// multiplication grows. Higher counts in the 1000-5000 range smooth out the
// noise at the Toom-3/Toom-4 and Toom-4/Toom-6.5 crossover zones so the
// first crossing on the speedup curve is genuine, not a measurement artifact.
// Adjust if a machine is much faster/slower than the development baseline.
[[nodiscard]] constexpr unsigned choose_trials(const std::size_t limbs) noexcept {
    if (limbs <= 8) {
        return 300'000U;
    }
    if (limbs <= 16) {
        return 150'000U;
    }
    if (limbs <= 32) {
        return 50'000U;
    }
    if (limbs <= 64) {
        return 15'000U;
    }
    if (limbs <= 128) {
        return 4'000U;
    }
    if (limbs <= 256) {
        return 1'500U;
    }
    if (limbs <= 500) {
        return 700U;
    }
    if (limbs <= 1'000) {
        return 400U;
    }
    if (limbs <= 2'000) {
        return 250U;
    }
    if (limbs <= 3'500) {
        return 150U;
    }
    if (limbs <= 5'000) {
        return 80U;
    }
    if (limbs <= 10'000) {
        return 40U;
    }
    if (limbs <= 20'000) {
        return 20U;
    }
    if (limbs <= 50'000) {
        return 8U;
    }
    return 4U;
}

void run_sweep() {
    std::cout << "algorithm,limbs,trials,ns_per_mul\n";
    for (const auto& algo : algorithms) {
        for (const std::size_t limbs : sweep_limbs) {
            if (limbs < algo.min_limbs || limbs > algo.max_limbs) {
                continue;
            }
            const unsigned trials = choose_trials(limbs);
            const double   ns     = algo.run(limbs, trials);
            std::cout << algo.name << ',' << limbs << ',' << trials << ',' << std::fixed << std::setprecision(1) << ns
                      << '\n';
        }
        std::cout.flush();
    }
}

} // namespace local

TEST(Multiplication, MultiplicationStressBench) {
#ifdef BEMAN_BIG_INT_RUN_BENCHMARKS
    local::run_sweep();
    SUCCEED();
#else
    GTEST_SKIP() << "Stress benchmarks not run (define BEMAN_BIG_INT_RUN_BENCHMARKS to enable)";
#endif
}
