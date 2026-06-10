// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_DIV_IMPL_HPP
#define BEMAN_BIG_INT_DIV_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/mul_impl.hpp>

#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <span>

namespace beman::big_int::detail {

// Selects whether a division routine should yield the quotient or the remainder.
enum class division_op : unsigned char {
    // Compute only the quotient.
    div,
    // Compute only the remainder.
    rem,
    // Compute quotient and remainder simultaneously.
    div_rem,
};

// ---------------------------------------------------------------------------
// Multi-limb long division (Boost hybrid).
//
// Writes the quotient to `quotient` and the remainder to `remainder`.
// Both buffers are left unnormalized
// ---------------------------------------------------------------------------
constexpr void divide_unsigned(const std::span<uint_multiprecision_t>       quotient,
                               const std::span<uint_multiprecision_t>       remainder,
                               const std::span<const uint_multiprecision_t> dividend,
                               const std::span<const uint_multiprecision_t> divisor,
                               scratch_allocator_base&                      scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.size() >= dividend.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != dividend.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != divisor.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.data() != dividend.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.data() != divisor.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.data() != remainder.data());

    constexpr uint_multiprecision_t max_limb = static_cast<uint_multiprecision_t>(0) - 1;

    const std::size_t y_order = divisor.size() - 1;
    std::size_t       r_order = dividend.size() - 1;

    // quotient = 0
    std::ranges::fill(quotient, uint_multiprecision_t{0});

    // remainder = dividend, zero-padded up to remainder.size().
    std::ranges::copy(dividend, remainder.begin());
    if (remainder.size() > dividend.size()) {
        std::ranges::fill(remainder.subspan(dividend.size()), uint_multiprecision_t{0});
    }

    // Fast path: 2 limb / 2 limb.
    if (r_order == 1) {
        // The precondition divisor.back() != 0 ensures divisor[1] != 0,
        // so the quotient fits in a single limb.
        const wide<uint_multiprecision_t> a{.low_bits = remainder[0], .high_bits = remainder[1]};
        const wide<uint_multiprecision_t> b{.low_bits = divisor[0], .high_bits = divisor[1]};
        const auto [q, r] = divide_wide_by_wide(a, b);
        quotient[0]       = q;
        if (quotient.size() > 1) {
            quotient[1] = 0;
        }
        remainder[0] = r.low_bits;
        remainder[1] = r.high_bits;
        for (std::size_t i = 2; i < remainder.size(); ++i) {
            remainder[i] = 0;
        }
        return;
    }

    // Scratch buffer for the fused multiply-shift result `t`.
    // Needs at most dividend.size() + 1 limbs.
    const std::size_t                      t_cap  = dividend.size() + 1;
    const std::span<uint_multiprecision_t> t_full = scratch.allocate(t_cap);

    bool        r_neg      = false;
    bool        first_pass = true;
    std::size_t quot_size  = dividend.size() - divisor.size() + 1;

    do {
        // Retain the original top for future sizing comparison
        const std::size_t rem_top_orig = r_order;

        // ------------------------------------------------------------------
        // Estimate q̂.
        // ------------------------------------------------------------------
        uint_multiprecision_t guess = 1;
        if ((remainder[r_order] <= divisor[y_order]) && (r_order > 0)) {
            // Top remainder limb <= top divisor limb: safe single-limb divide
            // (remainder_top, remainder_top-1) / divisor_top.
            const wide<uint_multiprecision_t> num{.low_bits = remainder[r_order - 1], .high_bits = remainder[r_order]};
            const uint_multiprecision_t       den = divisor[y_order];
            if (num.high_bits < den) {
                const auto [v, _] = narrowing_div(num, den);
                guess             = v;
                --r_order;
            }
            // else: q̂ stays at 1; r_order stays.
        } else if (r_order == 0) {
            // Only possible when y_order == 0, but our precondition says
            // divisor.size() >= 2 so y_order >= 1. This branch is defensive.
            guess = remainder[0] / divisor[y_order];
        } else {
            // remainder[r_order] > divisor[y_order]. Use top-two-limbs of each
            // to compute a tighter q̂.
            const wide<uint_multiprecision_t> num_wide{.low_bits  = remainder[r_order - 1],
                                                       .high_bits = remainder[r_order]};
            const wide<uint_multiprecision_t> den_wide{.low_bits  = (y_order > 0) ? divisor[y_order - 1]
                                                                                  : uint_multiprecision_t{0},
                                                       .high_bits = divisor[y_order]};
            BEMAN_BIG_INT_DEBUG_ASSERT(den_wide.high_bits != 0);
            guess = divide_wide_by_wide(num_wide, den_wide).quotient;
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(guess != 0);

        // ------------------------------------------------------------------
        // Fold guess into quotient[shift..].
        // ------------------------------------------------------------------
        const std::size_t shift = r_order - y_order;
        if (r_neg) {
            if (quotient[shift] > guess) {
                quotient[shift] -= guess;
            } else {
                uint_multiprecision_t sub    = guess;
                bool                  borrow = false;
                for (std::size_t i = shift; i < quotient.size(); ++i) {
                    const auto [v, bout] = borrowing_sub(quotient[i], sub, borrow);
                    quotient[i]          = v;
                    borrow               = bout;
                    sub                  = 0;
                    if (!borrow) {
                        break;
                    }
                }
                BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
            }
        } else {
            if (max_limb - quotient[shift] > guess) {
                quotient[shift] += guess;
            } else {
                uint_multiprecision_t add   = guess;
                bool                  carry = false;
                for (std::size_t i = shift; i < quotient.size(); ++i) {
                    const auto [v, cout] = carrying_add(quotient[i], add, carry);
                    quotient[i]          = v;
                    carry                = cout;
                    add                  = 0;
                    if (!carry) {
                        break;
                    }
                }
                BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
            }
        }

        // ------------------------------------------------------------------
        // t := (divisor * guess) << (shift * limb_bits). O(n) fused.
        // ------------------------------------------------------------------
        for (std::size_t i = 0; i < shift; ++i) {
            t_full[i] = 0;
        }

        uint_multiprecision_t mul_carry = 0;
        for (std::size_t i = 0; i < divisor.size(); ++i) {
            const auto [lo, hi]     = widening_mul(divisor[i], guess);
            const auto [sum, carry] = carrying_add(lo, mul_carry);
            t_full[i + shift]       = sum;
            mul_carry               = hi + static_cast<uint_multiprecision_t>(carry);
        }
        std::size_t t_size = divisor.size() + shift;
        if (mul_carry != 0) {
            BEMAN_BIG_INT_DEBUG_ASSERT(t_size < t_cap);
            t_full[t_size] = mul_carry;
            ++t_size;
        }
        // Trim zero high limbs of t.
        while (t_size > 1 && t_full[t_size - 1] == 0) {
            --t_size;
        }
        const std::span<const uint_multiprecision_t> t_view{t_full.data(), t_size};

        // Compare remainder against t_view and update remainder accordingly.
        const std::size_t rem_logical_size = std::max(rem_top_orig + 1, t_size);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem_logical_size <= remainder.size());
        const std::span<const uint_multiprecision_t> rem_view{remainder.data(), rem_logical_size};
        const std::strong_ordering                   cmp = compare_unsigned_spans(rem_view, t_view);
        if (cmp == std::strong_ordering::greater) {
            static_cast<void>(subtract_unsigned_spans(remainder.first(rem_logical_size), rem_view, t_view));
        } else {
            // rem <= t implies rem has no nonzero limbs past t_size-1, so
            // rem_logical_size == t_size (else rem would be strictly larger).
            BEMAN_BIG_INT_DEBUG_ASSERT(rem_logical_size == t_size);
            bool borrow = false;
            for (std::size_t i = 0; i < t_size; ++i) {
                const auto ri     = remainder[i];
                const auto [v, b] = borrowing_sub(t_view[i], ri, borrow);
                remainder[i]      = v;
                borrow            = b;
            }
            BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
            // Zero any remainder limbs past the new size.
            for (std::size_t i = t_size; i < remainder.size(); ++i) {
                remainder[i] = 0;
            }
            r_neg = !r_neg;
        }

        // First iteration: strip conservatively-sized zero top of quotient.
        if (first_pass) {
            first_pass = false;
            while (quot_size > 1 && quotient[quot_size - 1] == 0) {
                --quot_size;
            }
        }

        // Recompute r_order from the full remainder span (the t - rem swap
        // can grow r_order by one limb).
        std::size_t new_r_order = remainder.size() - 1;
        while (new_r_order > 0 && remainder[new_r_order] == 0) {
            --new_r_order;
        }
        r_order = new_r_order;

        if (r_order < y_order) {
            break;
        }
    } while ((r_order > y_order) ||
             (compare_unsigned_spans(std::span<const uint_multiprecision_t>{remainder.data(), r_order + 1}, divisor) !=
              std::strong_ordering::less));

    scratch.deallocate(t_cap);

    // ------------------------------------------------------------------
    // Final adjustment: if r_neg and remainder != 0, we overshot by one.
    // quotient -= 1; remainder := divisor - remainder.
    // ------------------------------------------------------------------
    const bool remainder_is_zero =
        std::ranges::all_of(remainder, [](const uint_multiprecision_t x) { return x == 0; });
    if (r_neg && !remainder_is_zero) {
        static_cast<void>(decrement_span(quotient));
        bool borrow = false;
        for (std::size_t i = 0; i < remainder.size(); ++i) {
            const auto di     = i < divisor.size() ? divisor[i] : uint_multiprecision_t{0};
            const auto [v, b] = borrowing_sub(di, remainder[i], borrow);
            remainder[i]      = v;
            borrow            = b;
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    }
}

// ---------------------------------------------------------------------------
// Burnikel-Ziegler divide-and-conquer division.
// Reference: C. Burnikel, J. Ziegler, "Fast Recursive Division",
// MPI-I-98-1-022 (1998); structure mirrors OpenJDK's MutableBigInteger.
//
// Two mutually recursive routines (D_2n/n and D_3n/2n) reduce division to
// half-sized multiplications, so D(n) ~= 2*M(n) and the products ride the
// Karatsuba/Toom/FFT tiers through multiply_dispatch.
// ---------------------------------------------------------------------------

// Minimum divisor limbs for the divide-and-conquer path, and the minimum
// quotient length (dividend limbs - divisor limbs) for entering it; below
// either bound the schoolbook kernel wins. Java's BigInteger draws the same
// divisor line at 80 32-bit words. Tuned via division_stress_bench medians
// (M4 Max, 2026-06-10): the two paths sit at parity right at the gates, with
// divide-and-conquer ahead monotonically beyond them (~1.6x at a 2x-cutoff
// balanced division, ~4.5x at 512 limbs, ~12x at 4096); the quotient-gate
// crossover measures 6-10 limbs depending on divisor size. Below the divisor
// gate the wins exist only for near-balanced shapes and flip to losses at
// short quotients, so the gate stays at Java's line until the Phase B
// preinv basecase retune.
#if BEMAN_BIG_INT_LIMB_WIDTH == 64
inline constexpr std::size_t burnikel_ziegler_cutoff = 40;
inline constexpr std::size_t burnikel_ziegler_offset = 10;
#else
inline constexpr std::size_t burnikel_ziegler_cutoff = 80;
inline constexpr std::size_t burnikel_ziegler_offset = 20;
#endif

static_assert(burnikel_ziegler_cutoff >= 2,
              "burnikel_ziegler_cutoff < 2 lets the recursion reach leaves that break the "
              "divide_unsigned preconditions");

// Scratch upper bound for divide_burnikel_ziegler with padded divisor length
// `block_limbs` (n), `blocks` (t) dividend blocks, and recursion threshold
// `threshold`:
//   - normalized divisor:        n limbs
//   - shifted working dividend:  t*n limbs
//   - quotient blocks:           (t-1)*n limbs
//   - recursion high-water:      max(n, 5*threshold + 3) limbs
// The recursion term is exact by construction: a D_3n/2n frame allocates its
// single q*b2 product (2h <= n limbs) only after its recursive call has fully
// returned and rewound (LIFO), so product buffers never stack; a leaf needs
// q(j+1) + r(2j+1) + divide_unsigned's internal 2j+2 with j <= threshold.
// Unlike the multiplication heuristics this bound is deterministic (the
// allocation sizes depend only on operand sizes, never values); instrumented
// peaks in division_scratch_peak.test.cpp match the model exactly, topping
// out at peak/budget ~0.999 (measured 2026-06-10, shapes up to 4000x512),
// with the +8 slack as the only headroom.
constexpr std::size_t burnikel_ziegler_storage_size(const std::size_t block_limbs,
                                                    const std::size_t blocks,
                                                    const std::size_t threshold) noexcept {
    return 2 * blocks * block_limbs + std::max(block_limbs, 5 * threshold + 3) + 8;
}

// ---------------------------------------------------------------------------
// Leaf of the Burnikel-Ziegler recursion: divide the 2n-limb window `a` by
// the n-limb divisor `b` (b.back() != 0) with the schoolbook kernel.
// Precondition: value(a) < beta^n * value(b), so the quotient fits n limbs.
// Postconditions: a.first(n) holds the remainder, a.subspan(n) is zero, and
// all n limbs of `q` are written.
// ---------------------------------------------------------------------------
inline void divide_dc_basecase(const std::span<uint_multiprecision_t>       a,
                               const std::span<const uint_multiprecision_t> b,
                               const std::span<uint_multiprecision_t>       q,
                               scratch_allocator_base&                      scratch) noexcept {
    const std::size_t n = b.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == 2 * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(q.size() == n);
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(b.back() != 0);

    const std::size_t a_size = trimmed_size_span(a);
    const auto        a_view = std::span<const uint_multiprecision_t>{a.data(), a_size};

    // value(a) < value(b): quotient 0, remainder already in place (a's limbs
    // above its trimmed size are zero by definition).
    if (compare_unsigned_spans(a_view, b) == std::strong_ordering::less) {
        std::ranges::fill(q, uint_multiprecision_t{0});
        return;
    }

    // Single-limb divisor, reachable only under small threshold overrides.
    if (n == 1) {
        const std::span<uint_multiprecision_t> q_tmp = scratch.allocate(a_size);
        const uint_multiprecision_t            rem   = divide_unsigned_short(q_tmp, a_view, b[0]);
        BEMAN_BIG_INT_DEBUG_ASSERT(a_size < 2 || q_tmp[1] == 0);
        q[0] = q_tmp[0];
        a[0] = rem;
        a[1] = 0;
        scratch.deallocate(a_size);
        return;
    }

    // value(a) >= value(b) and b.back() != 0 force a_size >= n, satisfying
    // the divide_unsigned preconditions.
    BEMAN_BIG_INT_DEBUG_ASSERT(a_size >= n);
    const std::size_t                      q_size = a_size - n + 1;
    const std::span<uint_multiprecision_t> q_tmp  = scratch.allocate(q_size);
    const std::span<uint_multiprecision_t> r_tmp  = scratch.allocate(a_size + 1);

    divide_unsigned(q_tmp, r_tmp, a_view, b, scratch);

    // The 2n/n precondition keeps quotient and remainder below beta^n; copy
    // them back into the window layout.
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= n + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= n || q_tmp[n] == 0);
    const std::size_t q_copy = std::min(q_size, n);
    std::ranges::copy(q_tmp.first(q_copy), q.begin());
    std::ranges::fill(q.subspan(q_copy), uint_multiprecision_t{0});

    BEMAN_BIG_INT_DEBUG_ASSERT(
        is_span_zero(std::span<const uint_multiprecision_t>{r_tmp.data() + n, a_size + 1 - n}));
    std::ranges::copy(r_tmp.first(n), a.begin());
    std::ranges::fill(a.subspan(n), uint_multiprecision_t{0});

    scratch.deallocate(a_size + 1);
    scratch.deallocate(q_size);
}

template <class Allocator>
void divide_dc_3n2n(std::span<uint_multiprecision_t>       v,
                    std::span<const uint_multiprecision_t> b,
                    std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                scratch,
                    Allocator&                             alloc,
                    std::size_t                            threshold);

// ---------------------------------------------------------------------------
// Burnikel-Ziegler D_2n/n: divide the 2n-limb window `a` by the n-limb
// normalized divisor `b` (top bit set) via two D_3n/2n calls on half blocks.
// Precondition: value(a) < beta^n * value(b), so the quotient fits n limbs.
// Postconditions: a.first(n) holds the remainder, a.subspan(n) is zero, and
// all n limbs of `q` are written.
// `threshold` propagates through the recursion (unlike the multiplication
// kernels' top-only cutoff_override) so tests can force deep recursion on
// small operands; production callers pass burnikel_ziegler_cutoff.
// ---------------------------------------------------------------------------
template <class Allocator>
void divide_dc_2n1n(const std::span<uint_multiprecision_t>       a,
                    const std::span<const uint_multiprecision_t> b,
                    const std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                      scratch,
                    Allocator&                                   alloc,
                    const std::size_t                            threshold) {
    const std::size_t n = b.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == 2 * n);
    BEMAN_BIG_INT_DEBUG_ASSERT(q.size() == n);
    // value(a) < beta^n * value(b) is exactly "high half of a < b".
    BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(a.subspan(n), b) == std::strong_ordering::less);

    if ((n % 2) != 0 || n < threshold) {
        divide_dc_basecase(a, b, q, scratch);
        return;
    }

    const std::size_t h = n / 2;

    // High quotient half from the top three quarter-blocks; the remainder
    // lands in a[h..3h) and a[3h..4h) is zeroed.
    divide_dc_3n2n(a.subspan(h, 3 * h), b, q.subspan(h, h), scratch, alloc, threshold);

    // Low quotient half: [low quarter-block | remainder] is contiguous.
    divide_dc_3n2n(a.first(3 * h), b, q.first(h), scratch, alloc, threshold);
}

// ---------------------------------------------------------------------------
// Burnikel-Ziegler D_3n/2n: divide the 3h-limb window `v` by the 2h-limb
// normalized divisor `b` (top bit set): estimate the quotient from the high
// 2h limbs of `v` against b's high half, fix up with one h x h product and
// at most two add-back corrections.
// Precondition: value(v) < beta^h * value(b), so the quotient fits h limbs.
// Postconditions: v.first(2h) holds the remainder, v.subspan(2h) is zero,
// and all h limbs of `q` are written.
// ---------------------------------------------------------------------------
template <class Allocator>
void divide_dc_3n2n(const std::span<uint_multiprecision_t>       v,
                    const std::span<const uint_multiprecision_t> b,
                    const std::span<uint_multiprecision_t>       q,
                    scratch_allocator_base&                      scratch,
                    Allocator&                                   alloc,
                    const std::size_t                            threshold) {
    constexpr uint_multiprecision_t max_limb = static_cast<uint_multiprecision_t>(0) - 1;

    const std::size_t h = q.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(v.size() == 3 * h);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() == 2 * h);
    BEMAN_BIG_INT_DEBUG_ASSERT((b.back() >> (width_v<uint_multiprecision_t> - 1)) == 1);

    const auto b1 = b.subspan(h, h); // high half of the divisor
    const auto b2 = b.first(h);      // low half of the divisor

    const bool top_below_b1 = compare_unsigned_spans(v.subspan(2 * h, h), b1) == std::strong_ordering::less;
    bool       r1_carry     = false;
    if (top_below_b1) {
        // Recursive estimate q = floor(v_high2h / b1); R1 is left in v[h..2h)
        // and v[2h..3h) is zeroed.
        divide_dc_2n1n(v.subspan(h, 2 * h), b1, q, scratch, alloc, threshold);
    } else {
        // The precondition forces v's top h limbs to equal b1 exactly and the
        // true quotient to within 2 of beta^h - 1:
        //   q = beta^h - 1,  R1 = v_mid + b1  (fits h limbs plus a carry bit).
        BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(v.subspan(2 * h, h), b1) == std::strong_ordering::equal);
        std::ranges::fill(q, max_limb);
        r1_carry = add_unsigned_spans(v.subspan(h, h), v.subspan(h, h), b1);
        std::ranges::fill(v.subspan(2 * h), uint_multiprecision_t{0});
    }

    // d := q * b2 (2h limbs). The all-ones branch needs no multiply:
    // (beta^h - 1) * b2 == (b2 << h limbs) - b2.
    const std::span<uint_multiprecision_t> d = scratch.allocate(2 * h);
    std::ranges::fill(d, uint_multiprecision_t{0});
    if (top_below_b1) {
        multiply_dispatch(d, q, b2, alloc);
    } else {
        std::ranges::copy(b2, d.begin() + static_cast<std::ptrdiff_t>(h));
        subtract_unsigned_spans(d, d, b2);
    }

    // Combine: r_hat = R1 * beta^h + v_low - d over the 2h-limb window; the
    // carry/borrow pair tracks the limb at position 2h. net is in {-1, 0}:
    // d < beta^2h bounds r_hat > -beta^2h, and r_hat < value(b) < beta^2h.
    const bool borrow = subtract_unsigned_spans_borrow_out(v.first(2 * h), v.first(2 * h), d);
    int        net    = static_cast<int>(r1_carry) - static_cast<int>(borrow);
    BEMAN_BIG_INT_DEBUG_ASSERT(net <= 0);

    // While r_hat < 0: q -= 1, r_hat += b. Normalization (2 * value(b) >=
    // beta^2h) bounds the loop at two iterations.
    [[maybe_unused]] int corrections = 0;
    while (net < 0) {
        const bool q_borrow = decrement_span(q);
        BEMAN_BIG_INT_DEBUG_ASSERT(!q_borrow);
        net += static_cast<int>(add_unsigned_spans(v.first(2 * h), v.first(2 * h), b));
        ++corrections;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(corrections <= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(compare_unsigned_spans(v.first(2 * h), b) == std::strong_ordering::less);

    scratch.deallocate(2 * h);
}

// Block-decomposition parameters of the divide_burnikel_ziegler driver,
// exposed so the scratch instrumentation test can size and probe the same
// workspace the production entry point uses.
struct burnikel_ziegler_params {
    std::size_t block_limbs; // n: divisor length padded to j * 2^k
    std::size_t blocks;      // t: dividend blocks marched by the driver
    std::size_t threshold;   // recursion leaf threshold in effect
};

// Computes (n, t, thr) for a trimmed dividend/divisor pair:
//   - n pads the divisor length to j * m_pow2 with m_pow2 the smallest power
//     of two for which j = ceil(s / m_pow2) is at or below the threshold, so
//     halving bottoms out at j-limb leaves;
//   - t is the dividend block count; its +1 keeps the shifted dividend's top
//     block strictly below beta^n / 2 <= b_hat, establishing the D_2n/n
//     precondition for the first window (remainder < b_hat maintains it for
//     every later window).
[[nodiscard]] constexpr burnikel_ziegler_params
burnikel_ziegler_plan(const std::span<const uint_multiprecision_t> dividend,
                      const std::span<const uint_multiprecision_t> divisor,
                      const std::size_t                            threshold_override = 0) noexcept {
    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t s   = divisor.size();
    const std::size_t m   = dividend.size();
    const std::size_t thr = threshold_override != 0 ? threshold_override : burnikel_ziegler_cutoff;
    BEMAN_BIG_INT_DEBUG_ASSERT(thr >= 2);

    const std::size_t m_pow2 = std::size_t{1} << std::bit_width(s / thr);
    const std::size_t j      = (s + m_pow2 - 1) / m_pow2;
    const std::size_t n      = j * m_pow2;

    const std::size_t sigma = n * limb_bits - ((s - 1) * limb_bits + static_cast<std::size_t>(std::bit_width(divisor.back())));
    const std::size_t dividend_bits = (m - 1) * limb_bits + static_cast<std::size_t>(std::bit_width(dividend.back()));
    const std::size_t t             = std::max<std::size_t>(2, (dividend_bits + sigma) / (n * limb_bits) + 1);

    return {.block_limbs = n, .blocks = t, .threshold = thr};
}

// ---------------------------------------------------------------------------
// Burnikel-Ziegler division driver (the paper's Algorithm 3): pad the divisor
// length to n = j * 2^k so halving bottoms out at j-limb leaves, normalize
// both operands by sigma = n * limb_bits - bitlen(divisor), then march
// fixed-size dividend blocks through divide_dc_2n1n from the top down, each
// window picking up the previous remainder as its high half.
// Same outer contract as divide_unsigned: trimmed inputs, divisor.size() >= 2,
// dividend.size() >= divisor.size(), quotient.size() >= dividend.size() -
// divisor.size() + 1, remainder.size() >= dividend.size() + 1, no aliasing;
// both output spans are fully written (zero above the significant limbs).
// This overload works in caller-provided scratch, which must hold at least
// burnikel_ziegler_storage_size(plan...) limbs for `plan` as computed by
// burnikel_ziegler_plan on the same operands.
// ---------------------------------------------------------------------------
template <class Allocator>
void divide_burnikel_ziegler(const std::span<uint_multiprecision_t>       quotient,
                             const std::span<uint_multiprecision_t>       remainder,
                             const std::span<const uint_multiprecision_t> dividend,
                             const std::span<const uint_multiprecision_t> divisor,
                             scratch_allocator_base&                      scratch,
                             Allocator&                                   alloc,
                             const burnikel_ziegler_params                plan) {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(dividend.size() >= divisor.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size() - divisor.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(remainder.size() >= dividend.size() + 1);

    constexpr std::size_t limb_bits = width_v<uint_multiprecision_t>;

    const std::size_t s   = divisor.size();
    const std::size_t m   = dividend.size();
    const std::size_t thr = plan.threshold;
    const std::size_t n   = plan.block_limbs;
    const std::size_t t   = plan.blocks;

    // Normalization shift: whole limbs from the padding plus the bits that
    // bring the divisor's top bit to the top. limb_off == n - s exactly
    // because the trimmed divisor has (s-1)*limb_bits < bitlen <= s*limb_bits.
    const std::size_t limb_off = n - s;
    const unsigned    bit_off =
        static_cast<unsigned>(limb_bits) - static_cast<unsigned>(std::bit_width(divisor.back()));

    // b_hat = divisor << sigma: n limbs, top bit set.
    const std::span<uint_multiprecision_t> b_hat = scratch.allocate(n);
    std::ranges::fill(b_hat, uint_multiprecision_t{0});
    std::ranges::copy(divisor, b_hat.begin() + static_cast<std::ptrdiff_t>(limb_off));
    if (bit_off != 0) {
        const std::size_t b_hat_size = shift_left_n(b_hat, n, bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(b_hat_size == n);
    }
    BEMAN_BIG_INT_DEBUG_ASSERT((b_hat.back() >> (limb_bits - 1)) == 1);

    // w = dividend << sigma, zero-extended to t blocks of n limbs.
    const std::span<uint_multiprecision_t> w = scratch.allocate(t * n);
    std::ranges::fill(w, uint_multiprecision_t{0});
    std::ranges::copy(dividend, w.begin() + static_cast<std::ptrdiff_t>(limb_off));
    if (bit_off != 0) {
        const std::size_t w_size = shift_left_n(w, limb_off + m, bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(w_size <= t * n);
    }

    // March the windows from the top down; window i leaves its remainder in
    // w[i*n..(i+1)*n), the high half of window i-1.
    const std::span<uint_multiprecision_t> q_work = scratch.allocate((t - 1) * n);
    for (std::size_t i = t - 1; i-- > 0;) {
        divide_dc_2n1n(w.subspan(i * n, 2 * n), b_hat, q_work.subspan(i * n, n), scratch, alloc, thr);
    }

    // Quotient blocks concatenate exactly (each is below beta^n); trim and
    // copy out.
    const std::size_t q_size = trimmed_size_span(q_work);
    BEMAN_BIG_INT_DEBUG_ASSERT(q_size <= quotient.size());
    std::ranges::copy(q_work.first(q_size), quotient.begin());
    std::ranges::fill(quotient.subspan(q_size), uint_multiprecision_t{0});

    // Remainder: undo the normalization shift on the final window's low half.
    if (bit_off != 0) {
        const uint_multiprecision_t dropped = shift_right_n(w.first(n), bit_off);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(limb_off == 0 ||
                               is_span_zero(std::span<const uint_multiprecision_t>{w.data(), limb_off}));
    std::ranges::copy(w.subspan(limb_off, n - limb_off), remainder.begin());
    std::ranges::fill(remainder.subspan(n - limb_off), uint_multiprecision_t{0});
}

// Convenience overload: sizes and owns the workspace, then forwards to the
// scratch-based driver above.
// `threshold_override` is a benchmark/test-only escape hatch; unlike the
// multiplication kernels' cutoff_override it propagates through the
// recursion. Production callers omit it.
template <class Allocator>
void divide_burnikel_ziegler(const std::span<uint_multiprecision_t>       quotient,
                             const std::span<uint_multiprecision_t>       remainder,
                             const std::span<const uint_multiprecision_t> dividend,
                             const std::span<const uint_multiprecision_t> divisor,
                             Allocator&                                   alloc,
                             const std::size_t                            threshold_override = 0) {
    const burnikel_ziegler_params plan = burnikel_ziegler_plan(dividend, divisor, threshold_override);
    scratch_allocator<Allocator>  scratch(
        burnikel_ziegler_storage_size(plan.block_limbs, plan.blocks, plan.threshold), alloc);
    divide_burnikel_ziegler(quotient, remainder, dividend, divisor, scratch, alloc, plan);
}

// ---------------------------------------------------------------------------
// Top-level division dispatcher (counterpart of multiply_dispatch): the
// divide-and-conquer path needs both a large divisor and a long quotient to
// pay off; everything else takes the schoolbook kernel. Constant evaluation
// always takes the schoolbook kernel for the same reason multiply_dispatch
// avoids its recursive tiers there (consteval step limits).
// Same contract as divide_unsigned. `scratch` must provide at least
// dividend.size() + 1 limbs for the schoolbook path; the divide-and-conquer
// path sizes and owns its own workspace through `alloc`.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void divide_dispatch(const std::span<uint_multiprecision_t>       quotient,
                               const std::span<uint_multiprecision_t>       remainder,
                               const std::span<const uint_multiprecision_t> dividend,
                               const std::span<const uint_multiprecision_t> divisor,
                               scratch_allocator_base&                      scratch,
                               Allocator&                                   alloc) {
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (divisor.size() >= burnikel_ziegler_cutoff &&
            dividend.size() - divisor.size() >= burnikel_ziegler_offset) {
            divide_burnikel_ziegler(quotient, remainder, dividend, divisor, alloc);
            return;
        }
    }

    divide_unsigned(quotient, remainder, dividend, divisor, scratch);
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_DIV_IMPL_HPP
