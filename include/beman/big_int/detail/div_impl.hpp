// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_DIV_IMPL_HPP
#define BEMAN_BIG_INT_DIV_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#include <algorithm>
#include <span>

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// Three-way compare of two little-endian unsigned spans.
// Returns -1 / 0 / +1. Operands need not be trimmed: trailing zero limbs on
// either side are treated as insignificant.
// ---------------------------------------------------------------------------
constexpr int compare_unsigned_spans(const std::span<const uint_multiprecision_t> a,
                                     const std::span<const uint_multiprecision_t> b) noexcept {
    const std::size_t na = a.size();
    const std::size_t nb = b.size();
    const std::size_t n  = std::max(na, nb);
    for (std::size_t i = n; i-- > 0;) {
        const auto ai = i < na ? a[i] : uint_multiprecision_t{0};
        const auto bi = i < nb ? b[i] : uint_multiprecision_t{0};
        if (ai != bi) {
            return ai < bi ? -1 : 1;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// In-place +1 on a little-endian unsigned span. Returns true on carry out.
// ---------------------------------------------------------------------------
constexpr bool increment_span(const std::span<uint_multiprecision_t> s) noexcept {
    for (auto& limb : s) {
        if (++limb != 0) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// In-place -1 on a little-endian unsigned span. Returns true on borrow out
// (i.e. the span was all zero on entry).
// ---------------------------------------------------------------------------
constexpr bool decrement_span(const std::span<uint_multiprecision_t> s) noexcept {
    for (auto& limb : s) {
        if (limb-- != 0) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Single-limb short division.
// `quotient[i]` := floor(([remainder, dividend[i]] as two limbs) / divisor)
// scanning from the top limb down. Returns the scalar remainder.
//
// Preconditions:
//   - divisor != 0
//   - quotient.size() >= dividend.size()
//   - dividend.size() >= 1
//   - quotient may alias dividend (we read dividend[i] before writing
//     quotient[i]; subsequent iterations touch strictly lower indices).
// ---------------------------------------------------------------------------
constexpr uint_multiprecision_t divide_unsigned_short(const std::span<uint_multiprecision_t>       quotient,
                                                      const std::span<const uint_multiprecision_t> dividend,
                                                      const uint_multiprecision_t                  divisor) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());

    uint_multiprecision_t remainder = 0;
    for (std::size_t i = dividend.size(); i-- > 0;) {
        // narrowing_div requires x.high_bits < y; the previous remainder was
        // taken mod divisor, so this invariant holds (and 0 < divisor on the
        // first iteration).
        const wide<uint_multiprecision_t> num{.low_bits = dividend[i], .high_bits = remainder};
        const auto [q, r] = narrowing_div(num, divisor);
        quotient[i]       = q;
        remainder         = r;
    }
    return remainder;
}

// ---------------------------------------------------------------------------
// Multi-limb long division (Boost hybrid).
//
// Writes the quotient to `quotient` and the remainder to `remainder`.
// Both buffers are left unnormalized
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void divide_unsigned(const std::span<uint_multiprecision_t>       quotient,
                               const std::span<uint_multiprecision_t>       remainder,
                               const std::span<const uint_multiprecision_t> dividend,
                               const std::span<const uint_multiprecision_t> divisor,
                               scratch_allocator<Allocator>&                scratch) noexcept {
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

    constexpr std::size_t           limb_bits = width_v<uint_multiprecision_t>;
    constexpr uint_multiprecision_t max_limb  = static_cast<uint_multiprecision_t>(0) - 1;

    const std::size_t y_order = divisor.size() - 1;
    std::size_t       r_order = dividend.size() - 1;

    // quotient = 0
    std::ranges::fill(quotient, uint_multiprecision_t{0});

    // remainder = dividend, zero-padded up to remainder.size().
    std::ranges::copy(dividend, remainder.begin());
    if (remainder.size() > dividend.size()) {
        std::ranges::fill(remainder.subspan(dividend.size()), uint_multiprecision_t{0});
    }

    // Fast path: 2 limb / 2 limb. Do the whole thing in uint_wide_t.
    if (r_order == 1) {
        // r_order == 1 && divisor.size() >= 2 && divisor.size() <= dividend.size()
        // ⇒ divisor.size() == 2.
        const uint_wide_t a = (static_cast<uint_wide_t>(remainder[1]) << limb_bits) | remainder[0];
        const uint_wide_t b = (static_cast<uint_wide_t>(divisor[1]) << limb_bits) | divisor[0];
        const uint_wide_t q = a / b;
        const uint_wide_t r = a % b;
        quotient[0]         = static_cast<uint_multiprecision_t>(q);
        if (quotient.size() > 1) {
            quotient[1] = static_cast<uint_multiprecision_t>(q >> limb_bits);
        }
        remainder[0] = static_cast<uint_multiprecision_t>(r);
        remainder[1] = static_cast<uint_multiprecision_t>(r >> limb_bits);
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
        // ------------------------------------------------------------------
        // Estimate q̂.
        // ------------------------------------------------------------------
        uint_multiprecision_t guess = 1;
        if ((remainder[r_order] <= divisor[y_order]) && (r_order > 0)) {
            // Top remainder limb <= top divisor limb: safe single-limb divide
            // (remainder_top, remainder_top-1) / divisor_top.
            const wide<uint_multiprecision_t> num{.low_bits  = remainder[r_order - 1],
                                                  .high_bits = remainder[r_order]};
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
            // in uint_wide_t to compute a tighter q̂.
            const uint_wide_t num_wide =
                (static_cast<uint_wide_t>(remainder[r_order]) << limb_bits) | remainder[r_order - 1];
            const uint_wide_t den_wide =
                (y_order > 0)
                    ? ((static_cast<uint_wide_t>(divisor[y_order]) << limb_bits) | divisor[y_order - 1])
                    : (static_cast<uint_wide_t>(divisor[y_order]) << limb_bits);
            BEMAN_BIG_INT_DEBUG_ASSERT(den_wide != 0);
            guess = static_cast<uint_multiprecision_t>(num_wide / den_wide);
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

        uint_wide_t mul_carry = 0;
        for (std::size_t i = 0; i < divisor.size(); ++i) {
            mul_carry += static_cast<uint_wide_t>(divisor[i]) * static_cast<uint_wide_t>(guess);
            t_full[i + shift] = static_cast<uint_multiprecision_t>(mul_carry);
            mul_carry >>= limb_bits;
        }
        std::size_t t_size = divisor.size() + shift;
        if (mul_carry != 0) {
            BEMAN_BIG_INT_DEBUG_ASSERT(t_size < t_cap);
            t_full[t_size] = static_cast<uint_multiprecision_t>(mul_carry);
            ++t_size;
        }
        // Trim zero high limbs of t.
        while (t_size > 1 && t_full[t_size - 1] == 0) {
            --t_size;
        }
        const std::span<const uint_multiprecision_t> t_view{t_full.data(), t_size};

        // Compare remainder against t_view and update remainder accordingly.
        const std::size_t rem_logical_size = std::max(r_order + 1, t_size);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem_logical_size <= remainder.size());
        const std::span<const uint_multiprecision_t> rem_view{remainder.data(), rem_logical_size};
        const int                                    cmp = compare_unsigned_spans(rem_view, t_view);
        if (cmp > 0) {
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
             (compare_unsigned_spans(std::span<const uint_multiprecision_t>{remainder.data(), r_order + 1},
                                     divisor) >= 0));

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

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_DIV_IMPL_HPP
