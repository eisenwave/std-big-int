// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SPAN_OPS_HPP
#define BEMAN_BIG_INT_SPAN_OPS_HPP

#include <beman/big_int/detail/config.hpp>
#include <algorithm>
#include <initializer_list>
#include <ranges>
#include <utility>
#include <span>

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// In-place +1 on a little-endian unsigned span. Returns true on carry out.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr bool increment_span(const std::span<uint_multiprecision_t> s) noexcept {
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
[[nodiscard]] constexpr bool decrement_span(const std::span<uint_multiprecision_t> s) noexcept {
    for (auto& limb : s) {
        if (limb-- != 0) {
            return false;
        }
    }
    return true;
}

// Returns true if all limbs in the span are zero
constexpr bool is_span_zero(const std::span<const uint_multiprecision_t> s) noexcept {
    return std::ranges::all_of(s, [](const uint_multiprecision_t limb) { return limb == 0; });
}

// ---------------------------------------------------------------------------
// Three-way compare of two little-endian unsigned spans.
// Operands need not be trimmed: trailing zero limbs on either side are
// treated as insignificant.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::strong_ordering
compare_unsigned_spans(const std::span<const uint_multiprecision_t> a,
                       const std::span<const uint_multiprecision_t> b) noexcept {
    const std::size_t na = a.size();
    const std::size_t nb = b.size();
    const std::size_t n  = std::max(na, nb);
    for (std::size_t i = n; i-- > 0;) {
        const auto ai = i < na ? a[i] : uint_multiprecision_t{0};
        const auto bi = i < nb ? b[i] : uint_multiprecision_t{0};
        if (ai != bi) {
            return ai < bi ? std::strong_ordering::less : std::strong_ordering::greater;
        }
    }
    return std::strong_ordering::equal;
}

// ---------------------------------------------------------------------------
// Unsigned span addition: result = a + b
// `result.size()` must be >= max(a.size(), b.size()).
// `result` may alias `a`. Returns true if there is a carry out.
// ---------------------------------------------------------------------------
constexpr bool add_unsigned_spans(const std::span<uint_multiprecision_t>       result,
                                  const std::span<const uint_multiprecision_t> a,
                                  const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= std::max(a.size(), b.size()));
    bool carry = false;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto ai            = i < a.size() ? a[i] : uint_multiprecision_t{0};
        const auto bi            = i < b.size() ? b[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(ai, bi, carry);
        result[i]                = r_value;
        carry                    = c1;
    }
    return carry;
}

// Trim leading zero limbs returning the effective size
constexpr std::size_t trimmed_size_span(const std::span<const uint_multiprecision_t> s) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!s.empty());
    std::size_t n = s.size();
    while (n > 1 && s[n - 1] == 0) {
        --n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Unsigned span subtraction: result = a - b
// Requires |a| >= |b|. `result` may alias `a`.
// Returns the trimmed number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t subtract_unsigned_spans(const std::span<uint_multiprecision_t>       result,
                                              const std::span<const uint_multiprecision_t> a,
                                              const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());

    bool borrow = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ai                  = a[i];
        const auto bi                  = i < b.size() ? b[i] : uint_multiprecision_t{0};
        const auto [r_value, r_borrow] = borrowing_sub(ai, bi, borrow);
        result[i]                      = r_value;
        borrow                         = r_borrow;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size()});
}

// Void wrappers that discard the existing functions' returns and assert their
// expected conditions. Their matching `void` return lets callers express the
// sign-aware "result = a + b_signed" dispatch as a single ternary:
//   sign_b ? add_unsigned_spans_no_carry(r, a, b)
//          : subtract_unsigned_spans_no_borrow(r, a, b);
constexpr void add_unsigned_spans_no_carry(const std::span<uint_multiprecision_t>       result,
                                           const std::span<const uint_multiprecision_t> a,
                                           const std::span<const uint_multiprecision_t> b) noexcept {
    const bool carry = add_unsigned_spans(result, a, b);
    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
}

constexpr void subtract_unsigned_spans_no_borrow(const std::span<uint_multiprecision_t>       result,
                                                 const std::span<const uint_multiprecision_t> a,
                                                 const std::span<const uint_multiprecision_t> b) noexcept {
    [[maybe_unused]] const auto unused = subtract_unsigned_spans(result, a, b);
}

// ---------------------------------------------------------------------------
// Fused (a + b) >> n into result in a single pass. Returns the dropped low n
// bits (caller asserts == 0 for exact division by 2^n). Asserts no carry-out
// from the sum. `result` may alias `a` or `b`.
// 0 <= n < limb_width.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
add_unsigned_spans_and_shift_right_n(const std::span<uint_multiprecision_t>       result,
                                     const std::span<const uint_multiprecision_t> a,
                                     const std::span<const uint_multiprecision_t> b,
                                     const unsigned                               n) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= std::max(a.size(), b.size()));

    if (n == 0) {
        const bool carry = add_unsigned_spans(result, a, b);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    if (result.empty()) {
        return 0;
    }

    // Compute sum[0] separately so we can capture the dropped low bits.
    const auto a0     = !a.empty() ? a[0] : uint_multiprecision_t{0};
    const auto b0     = !b.empty() ? b[0] : uint_multiprecision_t{0};
    auto [s_prev, c0] = carrying_add(a0, b0);
    bool carry        = c0;

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = s_prev & mask;

    // Each iteration computes sum[i] then writes the shifted result[i-1] by
    // funnel-shifting (sum[i-1], sum[i]) right by n. Reads at index i happen
    // before the write at i-1, so result may safely alias a or b.
    for (std::size_t i = 1; i < result.size(); ++i) {
        const auto ai           = i < a.size() ? a[i] : uint_multiprecision_t{0};
        const auto bi           = i < b.size() ? b[i] : uint_multiprecision_t{0};
        const auto [s_curr, ci] = carrying_add(ai, bi, carry);
        carry                   = ci;
        result[i - 1]           = funnel_shr(wide<uint_multiprecision_t>{.low_bits = s_prev, .high_bits = s_curr}, n);
        s_prev                  = s_curr;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    result[result.size() - 1] = s_prev >> n;
    return rem;
}

[[nodiscard]] constexpr uint_multiprecision_t
add_unsigned_spans_and_shift_right_one(const std::span<uint_multiprecision_t>       result,
                                       const std::span<const uint_multiprecision_t> a,
                                       const std::span<const uint_multiprecision_t> b) noexcept {
    return add_unsigned_spans_and_shift_right_n(result, a, b, 1u);
}

// ---------------------------------------------------------------------------
// Fused (a - b) >> n into result in a single pass. Returns the dropped low n
// bits. Requires |a| >= |b|. `result` may alias `a` or `b`.
// 0 <= n < limb_width.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
subtract_unsigned_spans_and_shift_right_n(const std::span<uint_multiprecision_t>       result,
                                          const std::span<const uint_multiprecision_t> a,
                                          const std::span<const uint_multiprecision_t> b,
                                          const unsigned                               n) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= b.size());

    if (n == 0) {
        subtract_unsigned_spans(result, a, b);
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    if (result.empty()) {
        return 0;
    }

    const auto a0      = !a.empty() ? a[0] : uint_multiprecision_t{0};
    const auto b0      = !b.empty() ? b[0] : uint_multiprecision_t{0};
    auto [d_prev, bo0] = borrowing_sub(a0, b0);
    bool borrow        = bo0;

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = d_prev & mask;

    for (std::size_t i = 1; i < result.size(); ++i) {
        const auto ai            = i < a.size() ? a[i] : uint_multiprecision_t{0};
        const auto bi            = i < b.size() ? b[i] : uint_multiprecision_t{0};
        const auto [d_curr, boi] = borrowing_sub(ai, bi, borrow);
        borrow                   = boi;
        result[i - 1]            = funnel_shr(wide<uint_multiprecision_t>{.low_bits = d_prev, .high_bits = d_curr}, n);
        d_prev                   = d_curr;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    result[result.size() - 1] = d_prev >> n;
    return rem;
}

[[nodiscard]] constexpr uint_multiprecision_t
subtract_unsigned_spans_and_shift_right_one(const std::span<uint_multiprecision_t>       result,
                                            const std::span<const uint_multiprecision_t> a,
                                            const std::span<const uint_multiprecision_t> b) noexcept {
    return subtract_unsigned_spans_and_shift_right_n(result, a, b, 1u);
}

// ---------------------------------------------------------------------------
// Signed span subtraction: writes |a - b| into result, returns its size and
// whether the result is negative (i.e. b > a, so result holds b - a).
// `result` may alias `a` or `b`. Used by Toom-Cook 3 to evaluate at x = -1.
// ---------------------------------------------------------------------------
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

struct signed_sub_result {
    std::size_t size;
    bool        negative;
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

constexpr signed_sub_result subtract_unsigned_spans_signed(const std::span<uint_multiprecision_t>       result,
                                                           const std::span<const uint_multiprecision_t> a,
                                                           const std::span<const uint_multiprecision_t> b) noexcept {
    // Trim before comparing so the size relationship matches the value relationship,
    // satisfying subtract_unsigned_spans's a.size() >= b.size() invariant.
    const std::size_t a_trim = a.empty() ? 0 : trimmed_size_span(a);
    const std::size_t b_trim = b.empty() ? 0 : trimmed_size_span(b);
    const auto        a_view = a.first(a_trim);
    const auto        b_view = b.first(b_trim);

    if (compare_unsigned_spans(a_view, b_view) != std::strong_ordering::less) {
        BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_trim);
        if (a_trim == 0) {
            return {0, false};
        }
        return {subtract_unsigned_spans(result.first(a_trim), a_view, b_view), false};
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= b_trim);
    return {subtract_unsigned_spans(result.first(b_trim), b_view, a_view), true};
}

// ---------------------------------------------------------------------------
// Single-limb short division.
// `quotient[i]` := floor(([remainder, dividend[i]] as two limbs) / divisor)
// scanning from the top limb down.
// Returns the scalar remainder.
// `quotient` and `dividend` may be the same range (i.e. alias each other),
// but `quotient` may not be a strict subrange of `dividend`.
//
// Preconditions:
//   - divisor != 0
//   - quotient.size() >= dividend.size()
//   - dividend.size() >= 1
//   - quotient may alias dividend (we read dividend[i] before writing
//     quotient[i]; subsequent iterations touch strictly lower indices).
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
divide_unsigned_short(const std::span<uint_multiprecision_t>       quotient,
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
// Multiply a multi-limb number `a` by a single limb `val`.
// `result` must have space for `a.size() + 1` limbs and must NOT alias `a`.
// Returns the number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t multiply_single_limb(const std::span<uint_multiprecision_t>       result,
                                           const std::span<const uint_multiprecision_t> a,
                                           const uint_multiprecision_t                  val) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    uint_multiprecision_t carry = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto [lo, hi] = widening_mul(a[i], val);
        const auto [s1, c1] = carrying_add(lo, carry);
        result[i]           = s1;
        carry               = hi + static_cast<uint_multiprecision_t>(c1);
    }

    if (carry != 0) {
        result[a.size()] = carry;
        return a.size() + 1;
    }

    return a.size();
}

// ---------------------------------------------------------------------------
// Helpers shared by the Toom-Cook variants. All operate on little-endian
// multi-precision spans of uint_multiprecision_t limbs.
// ---------------------------------------------------------------------------

// In-place tmp[0..size) += addend; returns new size (may grow by 1).
[[nodiscard]] constexpr std::size_t add_into_tmp(const std::span<uint_multiprecision_t>       tmp,
                                                 const std::size_t                            size,
                                                 const std::span<const uint_multiprecision_t> addend) noexcept {
    if (addend.empty()) {
        return size;
    }

    const std::size_t new_size = std::max(size, addend.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > new_size);

    const bool carry =
        add_unsigned_spans(tmp.first(new_size), std::span<const uint_multiprecision_t>{tmp.data(), size}, addend);

    if (carry) {
        tmp[new_size] = 1;
        return new_size + 1;
    }

    return new_size;
}

// In-place tmp[0..size) <<= n (0 < n < limb_width); returns new size (may grow by 1).
// Pass size == tmp.size() to shift the full span; the carry-branch assertion
// then enforces that the shift did not overflow.
// Single-pass equivalent of n chained shift_left_one calls.
[[nodiscard]] constexpr std::size_t
shift_left_n(const std::span<uint_multiprecision_t> tmp, std::size_t size, const unsigned n) noexcept {
    if (size == 0 || n == 0) {
        return size;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(size <= tmp.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    uint_multiprecision_t prev = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, n);
        prev            = limb;
    }

    if (const auto carry = prev >> (local_limb_bits - n)) {
        BEMAN_BIG_INT_DEBUG_ASSERT(size < tmp.size());
        tmp[size++] = carry;
    }

    return size;
}

// In-place tmp[0..size) <<= 1; returns new size (may grow by 1). Thin wrapper
// around shift_left_n for the common single-bit Horner-style shift.
[[nodiscard]] constexpr std::size_t shift_left_one(const std::span<uint_multiprecision_t> tmp,
                                                   const std::size_t                      size) noexcept {
    return shift_left_n(tmp, size, 1u);
}

// In-place tmp <- addends[0] + addends[1] + ... + addends[N-1]; returns new size.
// Single fused pass over tmp: at each position i, sums addend[j][i] for every j
// in one shot with multi-input carry propagation. Replaces N-1 separate passes
// (one per chained add_into_tmp), so the number of writes to tmp drops from
// O(N*max_size) to O(max_size). Empty addends are tolerated (treated as zero).
[[nodiscard]] constexpr std::size_t
add_many_into_tmp(const std::span<uint_multiprecision_t>                              tmp,
                  const std::initializer_list<std::span<const uint_multiprecision_t>> addends) noexcept {
    if (addends.size() == 0) {
        return 0;
    }

    std::size_t max_size = 0;
    for (const auto& addend : addends) {
        if (addend.size() > max_size) {
            max_size = addend.size();
        }
    }
    if (max_size == 0) {
        return 0;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > max_size);

    // Per-position carry is bounded by addends.size() (one increment per overflow),
    // so it always fits in a single limb for any plausible N.
    uint_multiprecision_t carry = 0;
    for (std::size_t i = 0; i < max_size; ++i) {
        uint_multiprecision_t sum       = carry;
        uint_multiprecision_t new_carry = 0;
        for (const auto& addend : addends) {
            if (i < addend.size()) {
                const auto [s, c] = carrying_add(sum, addend[i]);
                sum               = s;
                new_carry += static_cast<uint_multiprecision_t>(c);
            }
        }
        tmp[i] = sum;
        carry  = new_carry;
    }

    std::size_t size = max_size;
    if (carry != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(size < tmp.size());
        tmp[size++] = carry;
    }
    return size;
}

// In-place tmp <- (tmp[0..size) << shift) + addend; returns new size (may grow
// by up to 2: one from the shift carry-out, one from the add carry-out). Fused
// single-pass equivalent of (shift_left_n, add_into_tmp). 0 <= shift < limb_width.
// Used as the inner step of horner_eval_into_tmp, but also useful as a standalone
// primitive.
[[nodiscard]] constexpr std::size_t
shift_left_n_and_add_into_tmp(const std::span<uint_multiprecision_t>       tmp,
                              const std::size_t                            size,
                              const unsigned                               shift,
                              const std::span<const uint_multiprecision_t> addend) noexcept {
    if (shift == 0) {
        return add_into_tmp(tmp, size, addend);
    }
    if (size == 0) {
        if (addend.empty()) {
            return 0;
        }
        std::ranges::copy(addend, tmp.begin());
        return addend.size();
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(shift < local_limb_bits);

    // The shifted region is `size + 1` limbs if the top limb's high `shift` bits
    // are non-zero, else `size` limbs. After adding `addend` the new size is
    // max(shifted_size, addend.size()) plus a possible carry-out.
    const uint_multiprecision_t shift_out    = tmp[size - 1] >> (local_limb_bits - shift);
    const std::size_t           shifted_size = size + (shift_out != 0 ? 1u : 0u);
    const std::size_t           output_size  = std::max(shifted_size, addend.size());

    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > output_size);

    bool                  add_carry = false;
    uint_multiprecision_t prev      = 0;
    for (std::size_t i = 0; i < output_size; ++i) {
        uint_multiprecision_t shifted;
        if (i < size) {
            const auto limb = tmp[i];
            shifted         = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, shift);
            prev            = limb;
        } else if (i == size) {
            shifted = shift_out;
        } else {
            shifted = 0;
        }

        const auto ai            = i < addend.size() ? addend[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(shifted, ai, add_carry);
        tmp[i]                   = r_value;
        add_carry                = c1;
    }

    if (add_carry) {
        BEMAN_BIG_INT_DEBUG_ASSERT(output_size < tmp.size());
        tmp[output_size] = 1;
        return output_size + 1;
    }
    return output_size;
}

// In-place tmp <- ((...((coeffs[0] << shift) + coeffs[1]) << shift) + ... ) + coeffs[N-1];
// returns new size. Evaluates a polynomial with MSB-first coefficients at x = 2^shift
// via Horner's method. Each Horner step is a single fused pass over tmp (shift+add),
// halving the per-step memory traffic versus calling shift_left_n then add_into_tmp.
[[nodiscard]] constexpr std::size_t
horner_eval_into_tmp(const std::span<uint_multiprecision_t>                              tmp,
                     const std::initializer_list<std::span<const uint_multiprecision_t>> coeffs,
                     const unsigned                                                      shift) noexcept {
    if (coeffs.size() == 0) {
        return 0;
    }
    auto it = coeffs.begin();
    std::ranges::copy(*it, tmp.begin());
    std::size_t size = it->size();
    for (++it; it != coeffs.end(); ++it) {
        size = shift_left_n_and_add_into_tmp(tmp, size, shift, *it);
    }
    return size;
}

// In-place tmp >>= n (0 < n < limb_width); returns the dropped low n bits packed
// as a single value (caller asserts == 0 for exact division by 2^n).
// Single-pass equivalent of n chained shift_right_one calls.
[[nodiscard]] constexpr uint_multiprecision_t shift_right_n(const std::span<uint_multiprecision_t> tmp,
                                                            const unsigned                         n) noexcept {
    if (tmp.empty() || n == 0) {
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = tmp[0] & mask;
    uint_multiprecision_t       high = 0;
    for (std::size_t i = tmp.size(); i-- > 0;) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shr(wide<uint_multiprecision_t>{.low_bits = limb, .high_bits = high}, n);
        high            = limb;
    }

    return rem;
}

// In-place tmp >>= 1; returns the dropped low bit (caller asserts == 0 for exact div).
// Thin wrapper around shift_right_n for the common single-bit halving case.
[[nodiscard]] constexpr uint_multiprecision_t shift_right_one(const std::span<uint_multiprecision_t> tmp) noexcept {
    return shift_right_n(tmp, 1u);
}

// In-place result[shift..) += src; asserts no carry out of the result span.
constexpr void add_shifted(const std::span<uint_multiprecision_t>       result,
                           const std::size_t                            shift,
                           const std::span<const uint_multiprecision_t> src) noexcept {
    const auto dest  = result.subspan(shift);
    bool       carry = false;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        const auto si            = i < src.size() ? src[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(dest[i], si, carry);
        dest[i]                  = r_value;
        carry                    = c1;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
}

// Fused multi-source variant of add_shifted with regular k-stride shifts:
//   result[(j+1)*k .. (j+1)*k + sources[j].size()) += sources[j]  for j = 0..N-1
// performed as a single pass over result with a multi-input carry chain.
// Equivalent to N chained add_shifted calls but writes each result limb exactly
// once instead of N times, which is the dominant cost when N gets large (e.g.
// the 10-coefficient recomposition at the end of Toom-Cook 6.5). Asserts no
// carry escapes result.
constexpr void recompose(const std::span<uint_multiprecision_t>                              result,
                         const std::size_t                                                   k,
                         const std::initializer_list<std::span<const uint_multiprecision_t>> sources) noexcept {
    if (sources.size() == 0 || k >= result.size()) {
        return;
    }

    const auto* const src_begin = sources.begin();
    const std::size_t n_sources = sources.size();

    // [active_lo, active_hi) is the slice of `sources` whose ranges currently
    // cover position p. As p advances, active_hi grows when sources enter and
    // active_lo grows when sources leave; each index increments at most N times
    // across the whole outer loop, so the inner work is O(1) amortized.
    std::size_t active_lo = 0;
    std::size_t active_hi = 0;

    uint_multiprecision_t carry = 0;
    for (std::size_t p = k; p < result.size(); ++p) {
        while (active_hi < n_sources && (active_hi + 1) * k <= p) {
            ++active_hi;
        }
        while (active_lo < active_hi && (active_lo + 1) * k + src_begin[active_lo].size() <= p) {
            ++active_lo;
        }

        uint_multiprecision_t sum       = result[p];
        uint_multiprecision_t new_carry = 0;

        if (carry != 0) {
            const auto [s, c] = carrying_add(sum, carry);
            sum               = s;
            new_carry         = static_cast<uint_multiprecision_t>(c);
        }

        for (std::size_t j = active_lo; j < active_hi; ++j) {
            const std::size_t shift = (j + 1) * k;
            const auto [s, c]       = carrying_add(sum, src_begin[j][p - shift]);
            sum                     = s;
            new_carry += static_cast<uint_multiprecision_t>(c);
        }

        result[p] = sum;
        carry     = new_carry;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(carry == 0);
}

// Given s = a + b and (|a - b|, sign_d) for non-negative a and b, recover the
// pair (a, b) and place them in (lower_dst, higher_dst):
//   sign_d == false  =>  a >= b: lower_dst = a = (s + |d|)/2; higher_dst = b = (s - |d|)/2
//   sign_d == true   =>  a <  b: lower_dst = a = (s - |d|)/2; higher_dst = b = (s + |d|)/2
// Used by Toom-Cook palindromic interpolation to split sum/diff back into
// individual coefficients. `tmp_scratch` is a workspace of at least
// max(s_view.size(), d_view.size()) limbs; it does not need to be pre-zeroed.
// The destinations may alias s_view and d_view (recover_pair reads s and d
// before writing the destinations); destinations must not alias each other
// nor tmp_scratch. Both s_view + d_view and s_view - d_view must be even
// (caller's responsibility, asserted via shift_right_one's rem == 0 check).
constexpr void recover_pair(const std::span<uint_multiprecision_t>       lower_dst,
                            const std::span<uint_multiprecision_t>       higher_dst,
                            const std::span<const uint_multiprecision_t> s_view,
                            const std::span<const uint_multiprecision_t> d_view,
                            const bool                                   sign_d,
                            const std::span<uint_multiprecision_t>       tmp_scratch) noexcept {
    // Stage (s + |d|) in tmp_scratch first; the subtract-into-destination step
    // below would otherwise clobber s_view / d_view if either aliases a destination.
    std::ranges::fill(tmp_scratch, uint_multiprecision_t{0});
    const bool sum_carry = add_unsigned_spans(tmp_scratch, s_view, d_view);
    BEMAN_BIG_INT_DEBUG_ASSERT(!sum_carry);
    const std::size_t plus_size = std::max(s_view.size(), d_view.size());
    const auto        plus_span = std::span<const uint_multiprecision_t>{tmp_scratch.data(), plus_size};

    // (s - |d|)/2 into the destination chosen by sign.
    const std::span<uint_multiprecision_t> minus_dst = sign_d ? lower_dst : higher_dst;
    subtract_unsigned_spans(minus_dst, s_view, d_view);
    {
        const auto rem = shift_right_one(minus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // (s + |d|)/2 into the other destination.
    const std::span<uint_multiprecision_t> plus_dst = sign_d ? higher_dst : lower_dst;
    std::ranges::fill(plus_dst, uint_multiprecision_t{0});
    std::ranges::copy(plus_span, plus_dst.begin());
    {
        const auto rem = shift_right_one(plus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
}

// Used by the solve_subsystem lambda in toom_cook_6_5
struct subsystem_signs {
    bool outer;
    bool inner;
};

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SPAN_OPS_HPP
