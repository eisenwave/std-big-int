// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// Exercises divide_unsigned_approx against the exact schoolbook kernel:
// the approximate quotient must satisfy q <= q' <= q + 1 on every shape,
// including the patterns that stress the truncated-divisor slack (zero low
// divisor halves), the saturated 3-by-2 fast path (top window pairs equal
// to the divisor's), exact multiples, and near-misses on both sides.

#include <beman/big_int/detail/div_impl.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <vector>

namespace {

namespace detail = beman::big_int::detail;
using uint_t     = beman::big_int::uint_multiprecision_t;

constexpr uint_t limb_max = std::numeric_limits<uint_t>::max();

using scratch_for_test = detail::scratch_allocator<std::allocator<uint_t>>;

std::vector<uint_t> random_limbs(const std::size_t size, std::mt19937_64& rng) {
    std::vector<uint_t> v(size);
    for (auto& limb : v) {
        limb = static_cast<uint_t>(rng());
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

// q' - q must be 0 or 1; returns the difference so sweeps can confirm the
// +1 case actually occurs.
std::size_t check_divappr(const std::vector<uint_t>& dividend, const std::vector<uint_t>& divisor) {
    const std::size_t m = dividend.size();
    const std::size_t s = divisor.size();
    EXPECT_GE(s, 3u);
    EXPECT_GT(m, s);

    std::allocator<uint_t> alloc;

    std::vector<uint_t> q_ref(m - s + 1, 0);
    std::vector<uint_t> r_ref(m + 1, 0);
    {
        scratch_for_test scratch(detail::divide_unsigned_storage_size(m, s), alloc);
        detail::divide_unsigned(std::span<uint_t>{q_ref}, std::span<uint_t>{r_ref}, std::span<const uint_t>{dividend},
                                std::span<const uint_t>{divisor}, scratch);
    }

    std::vector<uint_t> q_apx(m - s + 1, 0);
    {
        scratch_for_test scratch(detail::divide_unsigned_storage_size(m, s), alloc);
        detail::divide_unsigned_approx(std::span<uint_t>{q_apx}, std::span<const uint_t>{dividend},
                                       std::span<const uint_t>{divisor}, scratch);
    }

    // diff = q' - q, which must not borrow and must be at most 1.
    std::vector<uint_t> diff = q_apx;
    const bool borrow = detail::subtract_unsigned_spans_borrow_out(std::span<uint_t>{diff},
                                                                   std::span<const uint_t>{diff},
                                                                   std::span<const uint_t>{q_ref});
    EXPECT_FALSE(borrow) << "approximate quotient below the true quotient at m=" << m << " s=" << s;
    if (detail::is_span_zero(std::span<const uint_t>{diff})) {
        return 0;
    }
    const std::size_t diff_size = detail::trimmed_size_span(std::span<const uint_t>{diff});
    EXPECT_EQ(diff_size, 1u) << "m=" << m << " s=" << s;
    EXPECT_EQ(diff[0], 1u) << "m=" << m << " s=" << s;
    return static_cast<std::size_t>(diff[0]);
}

TEST(DivisionDivappr, RandomShapeSweep) {
    // For random operands the +1 slack fires with probability ~1/B per
    // division, so this sweep checks only the bound; ConstructedPlusOne
    // below pins the slack deterministically.
    std::mt19937_64 rng{0x5a1u};
    for (std::size_t s = 3; s <= 24; ++s) {
        for (std::size_t extra = 1; extra <= 40; extra += (extra < 8 ? 1 : 5)) {
            for (int trial = 0; trial < 20; ++trial) {
                check_divappr(random_limbs(s + extra, rng), random_limbs(s, rng));
            }
        }
    }
}

TEST(DivisionDivappr, ConstructedPlusOne) {
    // d = (d2, d1, d0) = (2^63, 0, B - 1), N = 3d - 1: the true quotient is
    // 2, but the chain that drops d0 and the bottom dividend limb computes
    // floor(floor((3d - 1)/B) / (d2 B + d1)) = floor((3 d_eff + 2)/d_eff)
    // = 3. This is the +1 slack made deterministic.
    constexpr uint_t    top_bit = uint_t{1} << (detail::width_v<uint_t> - 1);
    std::vector<uint_t> d{limb_max, 0, top_bit};

    std::vector<uint_t> n3d(4, 0); // 3 * d
    std::allocator<uint_t> alloc;
    detail::multiply_dispatch(std::span<uint_t>{n3d}, std::span<const uint_t>{std::vector<uint_t>{3}},
                              std::span<const uint_t>{d}, alloc);
    detail::subtract_unsigned_spans(std::span<uint_t>{n3d}, std::span<const uint_t>{n3d},
                                    std::span<const uint_t>{std::vector<uint_t>{1}});
    ASSERT_EQ(detail::trimmed_size_span(std::span<const uint_t>{n3d}), 4u);

    EXPECT_EQ(check_divappr(n3d, d), 1u);
}

TEST(DivisionDivappr, LargerShapes) {
    std::mt19937_64 rng{0x5a2u};
    for (const auto& [s, m] : {std::pair<std::size_t, std::size_t>{50, 100},
                               {100, 173},
                               {128, 1024},
                               {300, 421},
                               {500, 1000},
                               {37, 2000}}) {
        for (int trial = 0; trial < 4; ++trial) {
            check_divappr(random_limbs(m, rng), random_limbs(s, rng));
        }
    }
}

TEST(DivisionDivappr, AdversarialPatterns) {
    std::mt19937_64 rng{0x5a3u};
    for (const std::size_t s : {std::size_t{3}, std::size_t{5}, std::size_t{9}, std::size_t{16}}) {
        for (const std::size_t m : {s + 1, s + 2, 2 * s, 3 * s + 1}) {
            // All-ones everything: every digit saturates.
            check_divappr(std::vector<uint_t>(m, limb_max), std::vector<uint_t>(s, limb_max));
            // Divisor with a zero low half: the truncated limbs are the ones
            // that matter most.
            {
                std::vector<uint_t> d(s, 0);
                for (std::size_t i = s / 2; i < s; ++i) {
                    d[i] = static_cast<uint_t>(rng());
                }
                if (d.back() == 0) {
                    d.back() = 1;
                }
                check_divappr(random_limbs(m, rng), d);
                check_divappr(std::vector<uint_t>(m, limb_max), d);
            }
            // Divisor just above a power of two and dividend saturated: long
            // runs of (d1, d0) pairs in the windows.
            {
                std::vector<uint_t> d(s, 0);
                d.back() = uint_t{1} << (beman::big_int::detail::width_v<uint_t> - 1);
                d[0]     = 1;
                check_divappr(std::vector<uint_t>(m, limb_max), d);
            }
        }
    }
}

TEST(DivisionDivappr, SparseDivisorTruncationStress) {
    // Regression class for a tuning bug: with the divisor truncated (the
    // quotient much shorter than the divisor) and zero limbs scattered
    // through the divisor, keeping only quotient-digits + 1 limbs lets the
    // accumulated tail slack reach +2. The kernel must keep one more.
    std::mt19937_64 rng{0x5a5u};
    for (std::size_t s = 8; s <= 32; s += 4) {
        for (std::size_t extra = 2; extra <= 8; extra += 3) {
            for (int trial = 0; trial < 400; ++trial) {
                std::vector<uint_t> d(s);
                for (auto& limb : d) {
                    limb = (rng() & 1u) ? 0 : static_cast<uint_t>(rng());
                }
                if (d.back() == 0) {
                    d.back() = 1 + static_cast<uint_t>(rng() >> 1);
                }
                check_divappr(random_limbs(s + extra, rng), d);
            }
        }
    }
}

TEST(DivisionDivappr, ExactMultiplesAndNeighbors) {
    std::mt19937_64        rng{0x5a4u};
    std::allocator<uint_t> alloc;
    for (const std::size_t s : {std::size_t{3}, std::size_t{7}, std::size_t{20}, std::size_t{64}}) {
        for (const std::size_t qn : {std::size_t{2}, std::size_t{5}, std::size_t{3 * s}}) {
            for (int trial = 0; trial < 6; ++trial) {
                const auto          d = random_limbs(s, rng);
                const auto          q = random_limbs(qn, rng);
                std::vector<uint_t> product(s + qn, 0);
                detail::multiply_dispatch(std::span<uint_t>{product}, std::span<const uint_t>{q},
                                          std::span<const uint_t>{d}, alloc);
                product.resize(detail::trimmed_size_span(std::span<const uint_t>{product}));
                if (product.size() <= s) {
                    continue;
                }
                // N = q * d exactly, then one below (q - 1 remainder d - 1)
                // and one above (remainder 1).
                check_divappr(product, d);
                std::vector<uint_t> above = product;
                if (!detail::add_unsigned_spans(std::span<uint_t>{above}, std::span<const uint_t>{above},
                                                std::span<const uint_t>{std::vector<uint_t>{1}})) {
                    check_divappr(above, d);
                }
                std::vector<uint_t> below = product;
                detail::subtract_unsigned_spans(std::span<uint_t>{below}, std::span<const uint_t>{below},
                                                std::span<const uint_t>{std::vector<uint_t>{1}});
                below.resize(detail::trimmed_size_span(std::span<const uint_t>{below}));
                if (below.size() > s) {
                    check_divappr(below, d);
                }
            }
        }
    }
}

} // namespace
