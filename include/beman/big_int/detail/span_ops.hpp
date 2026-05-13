// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SPAN_OPS_HPP
#define BEMAN_BIG_INT_SPAN_OPS_HPP

#include <beman/big_int/detail/config.hpp>
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

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SPAN_OPS_HPP
