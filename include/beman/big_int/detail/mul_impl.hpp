// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MUL_IMPL_HPP
#define BEMAN_BIG_INT_MUL_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>

#include <algorithm>
#include <compare>
#include <memory>
#include <span>

namespace beman::big_int::detail {

// Minimum number of limbs for Karatsuba to be worthwhile
// Directly from Boost, and reconfirmed as correct
inline constexpr std::size_t karatsuba_cutoff = 40;

// Heuristic estimate of scratch space needed for Karatsuba multiplication
// Directly from Boost
constexpr std::size_t karatsuba_storage_size(const std::size_t s) noexcept { return 5 * s; }

// Maximum number of scratch limbs we're willing to put on the stack.
// Directly from Boost
inline constexpr std::size_t karatsuba_stack_threshold = 300;

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
// Long (classical) O(n*m) multiplication.
// `result` must be pre-zeroed and have space for `a.size() + b.size()` limbs.
// `result` must NOT alias `a` or `b`.
// ---------------------------------------------------------------------------
constexpr void multiply_long(const std::span<uint_multiprecision_t>       result,
                             const std::span<const uint_multiprecision_t> a,
                             const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());

    // The key invariant from Boost is:
    //   double_limb_max - 2 * limb_max >= limb_max * limb_max
    // This means that: widening_mul(a[i], b[j]).high + carry + bool_carry
    // can never overflow a single limb, so we only need a single-limb carry.
    for (std::size_t i = 0; i < a.size(); ++i) {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto [lo, hi] = widening_mul(a[i], b[j]);
            const auto [s1, c1] = carrying_add(lo, result[i + j]);
            const auto [s2, c2] = carrying_add(s1, carry);
            result[i + j]       = s2;
            carry               = hi + static_cast<uint_multiprecision_t>(c1) + static_cast<uint_multiprecision_t>(c2);
        }
        result[i + b.size()] = carry;
    }
}

// ---------------------------------------------------------------------------
// Recursive Karatsuba multiplication.
// Port of Boost.Multiprecision multiply_karatsuba (lines 98-215).
//
// `result` must have space for a.size() + b.size() limbs or more.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void multiply_karatsuba(const std::span<uint_multiprecision_t>       result,
                                  const std::span<const uint_multiprecision_t> a_untrimmed,
                                  const std::span<const uint_multiprecision_t> b_untrimmed,
                                  scratch_allocator<Allocator>&                scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // First, check if we have enough limbs to justify karatsuba
    if (a.size() < karatsuba_cutoff || b.size() < karatsuba_cutoff) {
        std::ranges::fill(result, uint_multiprecision_t{0});
        multiply_long(result.first(a.size() + b.size()), a, b);
        return;
    }

    // Partition at n = max(a.size(), b.size()) / 2 + 1
    const std::size_t n = std::max(a.size(), b.size()) / 2 + 1;

    // Split: a = a_h * B^n + a_l,  b = b_h * B^n + b_l
    // where B = 2^bits_per_limb
    const auto a_l = a.first(std::min(a.size(), n));

    const uint_multiprecision_t zero{0U};
    const auto                  a_h = a.size() > n ? a.subspan(n) : std::span<const uint_multiprecision_t>(&zero, 1);

    const auto b_l = b.first(std::min(b.size(), n));
    const auto b_h = b.size() > n ? b.subspan(n) : std::span<const uint_multiprecision_t>(&zero, 1);

    // Allocate all temporaries in a single bump from scratch, then carve
    // sub-spans for each:
    //   t1: holds (a_h + a_l) * (b_h + b_l), needs up to 2*n + 2 limbs
    //   t2: holds a_h + a_l, needs up to n + 1 limbs
    //   t3: holds b_h + b_l, needs up to n + 1 limbs
    const std::size_t t1_cap        = 2 * n + 2;
    const std::size_t t2_cap        = n + 1;
    const std::size_t t3_cap        = n + 1;
    const std::size_t total_scratch = t1_cap + t2_cap + t3_cap;

    auto scratch_block = scratch.allocate(total_scratch);
    auto t1            = scratch_block.first(t1_cap);
    auto t2            = scratch_block.subspan(t1_cap, t2_cap);
    auto t3            = scratch_block.subspan(t1_cap + t2_cap, t3_cap);

    // result layout:
    //
    // result[0, 2*n) = result_low (will hold a_l * b_l)
    // result[2*n, result.size()) = result_high (will hold a_h * b_h)
    auto result_low  = result.first(2 * n);
    auto result_high = result.size() > 2 * n ? result.subspan(2 * n) : std::span<uint_multiprecision_t>{};

    // Compute result_low = a_l * b_l
    multiply_karatsuba(result_low, a_l, b_l, scratch);
    const std::size_t result_low_size =
        trimmed_size_span(std::span<const uint_multiprecision_t>{result_low.data(), a_l.size() + b_l.size()});

    // Zero unused limbs in result_low region
    std::ranges::fill(result_low.subspan(result_low_size), uint_multiprecision_t{0});

    // Compute result_high = a_h * b_h
    if (!result_high.empty()) {
        if ((a.size() > n) && (b.size() > n)) {
            multiply_karatsuba(result_high, a_h, b_h, scratch);

            const std::size_t result_high_size =
                trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});

            // Zero unused limbs in result_high region
            std::ranges::fill(result_high.subspan(result_high_size), uint_multiprecision_t{0});
        } else {
            result_high = std::span<uint_multiprecision_t>{};
        }
    }

    // Compute t2 = a_h + a_l
    std::size_t t2_size = std::max(a_h.size(), a_l.size());
    if (add_unsigned_spans(t2.first(t2_size), a_l, a_h)) {
        t2[t2_size] = 1;
        ++t2_size;
    }

    // Compute t3 = b_h + b_l
    std::size_t t3_size = std::max(b_h.size(), b_l.size());
    if (add_unsigned_spans(t3.first(t3_size), b_l, b_h)) {
        t3[t3_size] = 1;
        ++t3_size;
    }

    // Compute t1 = t2 * t3 = (a_h + a_l) * (b_h + b_l)
    std::ranges::fill(t1, uint_multiprecision_t{0});
    const auto t2_span = std::span<const uint_multiprecision_t>{t2.data(), t2_size};
    const auto t3_span = std::span<const uint_multiprecision_t>{t3.data(), t3_size};
    multiply_karatsuba(t1, t2_span, t3_span, scratch);
    std::size_t t1_size = trimmed_size_span(std::span<const uint_multiprecision_t>{t1.data(), t2_size + t3_size});

    // t1 -= result_high (a_h * b_h)
    if (!result_high.empty()) {
        const std::size_t rh_size =
            trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});
        t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                          std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                          std::span<const uint_multiprecision_t>{result_high.data(), rh_size});
    }

    // t1 -= result_low (a_l * b_l)
    t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                      std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                      std::span<const uint_multiprecision_t>{result_low.data(), result_low_size});

    // Add t1 shifted left by n limbs into result: result[n...] += t1
    const auto result_mid = result.subspan(n);
    bool       carry      = false;
    for (std::size_t i = 0; i < result_mid.size(); ++i) {
        const auto ti            = i < t1_size ? t1[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(result_mid[i], ti, carry);
        result_mid[i]            = r_value;
        carry                    = c1;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    // No actual deallocation happens,
    // this is pointer arithmetic within a single pre-allocated buffer.
    scratch.deallocate(total_scratch);
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

// In-place tmp[0..size) <<= 1; returns new size (may grow by 1).
// Pass size == tmp.size() to shift the full span; the carry-branch assertion
// then enforces that the shift did not overflow.
[[nodiscard]] constexpr std::size_t shift_left_one(const std::span<uint_multiprecision_t> tmp,
                                                   std::size_t                            size) noexcept {
    if (size == 0) {
        return 0;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(size <= tmp.size());
    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    uint_multiprecision_t prev            = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, 1u);
        prev            = limb;
    }

    if (const auto carry = prev >> (local_limb_bits - 1)) {
        BEMAN_BIG_INT_DEBUG_ASSERT(size < tmp.size());
        tmp[size++] = carry;
    }

    return size;
}

// In-place tmp >>= 1; returns the dropped low bit (caller asserts == 0 for exact div).
[[nodiscard]] constexpr uint_multiprecision_t shift_right_one(const std::span<uint_multiprecision_t> tmp) noexcept {
    if (tmp.empty()) {
        return 0;
    }

    const uint_multiprecision_t rem  = tmp[0] & uint_multiprecision_t{1};
    uint_multiprecision_t       high = 0;
    for (std::size_t i = tmp.size(); i-- > 0;) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shr(wide<uint_multiprecision_t>{.low_bits = limb, .high_bits = high}, 1u);
        high            = limb;
    }

    return rem;
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

// Minimum number of limbs for Toom-Cook 3 to be worthwhile.
// See multiplication_stress_bench for tuning
inline constexpr std::size_t toom_cook_3_cutoff = 550;

// Heuristic estimate of scratch space needed for Toom-Cook 3 multiplication.
// Includes space for karatsuba fallback plan
constexpr std::size_t toom_cook_3_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 3-Way multiplication (Bodrato variant).
// Reference: Knuth TAOCP section 4.3.3
// Reference: Bodrato, "Towards Optimal Toom-Cook Multiplication" (2006)
//
// Splits each operand into three pieces of size k = ceil(max(an,bn)/3):
//   a = a2*B^(2k) + a1*B^k + a0,   b = b2*B^(2k) + b1*B^k + b0  (B = 2^limb_bits)
//
// Evaluates the product polynomial r(x) = p(x)*q(x) at five points
// {0, 1, -1, 2, infinity}, then interpolates the five coefficients c0-c4 of
// r(x) via Bodrato's seven-step in-place sequence (one /3, two /2).
// Result = c0 + c1*B^k + c2*B^(2k) + c3*B^(3k) + c4*B^(4k).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void multiply_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                                    const std::span<const uint_multiprecision_t> a_untrimmed,
                                    const std::span<const uint_multiprecision_t> b_untrimmed,
                                    scratch_allocator<Allocator>&                scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Partition at k = ceil(max(an, bn) / 3).
    const std::size_t min_size = std::min(a.size(), b.size());
    const std::size_t k        = (std::max(a.size(), b.size()) + 2) / 3;

    // Fall through to Karatsuba (and on through to schoolbook) when the smaller
    // operand is below the performance cutoff or below the algorithm's 2*k
    // invariant (Toom-Cook 3 needs both a2 and b2 non-empty).
    if (min_size < toom_cook_3_cutoff || min_size <= 2 * k) {
        multiply_karatsuba(result, a, b, scratch);
        return;
    }

    // Split each operand into three pieces. Empty pieces represent zero.
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 = a.size() > 2 * k ? a.subspan(2 * k) : std::span<const uint_multiprecision_t>{};

    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 = b.size() > 2 * k ? b.subspan(2 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each
    //   v1, vm1, v2: 2k+2 limbs each, hold three product values that survive through interpolation
    //   v0 = a0*b0 and vinf = a2*b2 live directly in result.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 3 * prod_cap;

    auto block = scratch.allocate(total_scratch);
    auto tmpa  = block.first(tmp_cap);
    auto tmpb  = block.subspan(tmp_cap, tmp_cap);
    auto v1    = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1   = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2    = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);

    // ---- v0 = a0*b0, written into result[0..2k) ----
    // result is pre-zeroed by the caller convention, so result.first(2k) is too.
    multiply_toom_cook_3(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- vinf = a2*b2, written into result[4k..) ----
    if (!a2.empty() && !b2.empty()) {
        multiply_toom_cook_3(result.subspan(4 * k, a2.size() + b2.size()), a2, b2, scratch);
    }

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2; tmpb = b0 + b1 + b2 ----
    std::ranges::copy(a0, tmpa.begin());
    std::size_t tmpa_size = a0.size();
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a2);

    std::ranges::copy(b0, tmpb.begin());
    std::size_t tmpb_size = b0.size();
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b2);

    // v1 = tmpa * tmpb
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_3(v1,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -1: tmpa = (a0 + a2) - a1 (signed); tmpb similarly ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    const auto sub_a =
        subtract_unsigned_spans_signed(tmpa, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, a1);
    tmpa_size         = sub_a.size;
    const bool sign_a = sub_a.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    const auto sub_b =
        subtract_unsigned_spans_signed(tmpb, std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size}, b1);
    tmpb_size         = sub_b.size;
    const bool sign_b = sub_b.negative;

    // Sign of vm1 = sign of (p(-1)*q(-1)). Magnitude = |p(-1)| * |q(-1)|.
    const bool sign_vm1 = sign_a ^ sign_b;

    // vm1 = |p(-1)| * |q(-1)|
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size == 0 || tmpb_size == 0) {
        // One of the evaluations is zero, so vm1 = 0.
    } else {
        multiply_toom_cook_3(vm1,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 4*a2 + 2*a1 + a0 (Horner: ((a2<<1)+a1)<<1+a0); tmpb similarly ----
    std::ranges::copy(a2, tmpa.begin());
    tmpa_size = a2.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);   // 2*a2
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1); // 2*a2 + a1
    tmpa_size = shift_left_one(tmpa, tmpa_size);   // 4*a2 + 2*a1
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0); // 4*a2 + 2*a1 + a0

    std::ranges::copy(b2, tmpb.begin());
    tmpb_size = b2.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    // v2 = p(2) * q(2)
    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_3(v2,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Bodrato interpolation (in-place; full 2k+2 buffers throughout) ----
    // After this sequence: v0=c0 (in result), vm1=c1, v1=c2, v2=c3, vinf=c4 (in result).
    //
    // Clip vinf_view to its semantic size of 2k limbs: c4 = a2*b2 with a2.size() <= k
    // and b2.size() <= k yields a product of at most 2k limbs. The result span may
    // extend past 4k+2k when called recursively from a parent whose prod_cap buffer
    // is larger than the actual product (slack = caller-pre-zeroed). Subtracting
    // those slack zeros is correct, but the size assertion in subtract_unsigned_spans
    // would fail without clipping.
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 4 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 4 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};

    // Step 1: v2 <- (v2 - vm1) / 3 (sign-aware: add if vm1 was negative).
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v2, v2_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(v2, v2_view, vm1_view);
    }
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 2: vm1 <- (v1 - vm1) / 2 (sign-aware). After this, vm1 is non-negative.
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm1, v1_view, vm1_view);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(vm1);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 3: v1 <- v1 - v0 (sizes: v1 is 2k+2, v0_view is 2k; v1 >= v0 numerically).
    subtract_unsigned_spans(v1, v1_view, v0_view);

    // Step 4: v2 <- (v2 - v1) / 2.
    subtract_unsigned_spans(v2, v2_view, v1_view);
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 5: v1 <- v1 - vm1 - vinf.
    subtract_unsigned_spans(v1, v1_view, vm1_view);
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 6: v2 <- v2 - 2*vinf (subtract twice, no doubling helper needed).
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    subtract_unsigned_spans(v2, v2_view, vinf_view);

    // Step 7: vm1 <- vm1 - v2.
    subtract_unsigned_spans(vm1, vm1_view, v2_view);

    // ---- Recompose: result += vm1*B^k + v1*B^(2k) + v2*B^(3k) ----
    // c0 already at result[0..2k); c4 already at result[4k..). Add the three middle
    // coefficients with carry chains spanning to result.size() so carries can
    // propagate into the c4 region.
    add_shifted(result, k, vm1_view);
    add_shifted(result, 2 * k, v1_view);
    add_shifted(result, 3 * k, v2_view);

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    scratch.deallocate(total_scratch);
}

// Minimum number of limbs for Toom-Cook 4 to be worthwhile.
// Empirically tuned on Apple Silicon via multiplication_stress_bench
inline constexpr std::size_t toom_cook_4_cutoff = 1400;

// Heuristic estimate of scratch space needed for Toom-Cook 4 multiplication.
// One Toom-4 level uses 14k+16 limbs (~3.5*s where k = ceil(s/4)). The geometric
// sum over self-recursion converges to 14/3*s ~= 4.67*s as the asymptotic worst
// case; empirically (probed via scratch_allocator high-water marks on sizes
// 4500-80000 limbs) the actual peak ranges 4.50-4.66*s. 6*s leaves ~25-30%
// safety margin and matches the same generous-but-not-wasteful ratio the older
// algorithms use.
constexpr std::size_t toom_cook_4_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 4-Way multiplication (Bodrato variant).
// Reference: Bodrato, "Towards Optimal Toom-Cook Multiplication" (2006).
//
// Splits each operand into four pieces of size k = ceil(max(an,bn)/4):
//   a = a3*B^(3k) + a2*B^(2k) + a1*B^k + a0,  b = b3*B^(3k) + ... (B = 2^limb_bits)
//
// Evaluates the product polynomial r(x) = p(x)*q(x) at seven points
// {0, 1, -1, 2, -2, 1/2 (scaled by 8), infinity}, then interpolates the seven
// coefficients c0..c6 of r(x) using a Bodrato-style in-place sequence with
// exact divisions by 2, 3, and 5.
// Result = c0 + c1*B^k + c2*B^(2k) + c3*B^(3k) + c4*B^(4k) + c5*B^(5k) + c6*B^(6k).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
//
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_4_cutoff` for this call only. Production callers should omit it so
// the default cutoff applies. Recursive sub-product calls always use the default.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void multiply_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                                    const std::span<const uint_multiprecision_t> a_untrimmed,
                                    const std::span<const uint_multiprecision_t> b_untrimmed,
                                    scratch_allocator<Allocator>&                scratch,
                                    const std::size_t                            cutoff_override = 0) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Partition at k = ceil(max(an, bn) / 4).
    const std::size_t min_size         = std::min(a.size(), b.size());
    const std::size_t k                = (std::max(a.size(), b.size()) + 3) / 4;
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_4_cutoff : cutoff_override;

    // Fall through to Toom-Cook 3 (and on through Karatsuba and schoolbook) when the
    // smaller operand is below the performance cutoff or below the algorithm's 3*k
    // invariant (Toom-Cook 4 needs both a3 and b3 non-empty).
    if (min_size < effective_cutoff || min_size <= 3 * k) {
        multiply_toom_cook_3(result, a, b, scratch);
        return;
    }

    // Split each operand into four pieces.
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 =
        a.size() > 2 * k ? a.subspan(2 * k, std::min(k, a.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a3 = a.size() > 3 * k ? a.subspan(3 * k) : std::span<const uint_multiprecision_t>{};

    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 =
        b.size() > 2 * k ? b.subspan(2 * k, std::min(k, b.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b3 = b.size() > 3 * k ? b.subspan(3 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each (evaluation buffers, hold p(x) and q(x) at each point)
    //   v1, vm1, v2, vm2, vh: 2k+2 limbs each (five interpolation buffers)
    //   tmp_double: 2k+2 limbs (used for in-place bit shifts to replace doubling with mul)
    // c0 = a0*b0 lives in result[0..2k); c6 = a3*b3 lives in result[6k..).
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 6 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto tmpb       = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);

    // ---- c0 = a0*b0, written into result[0..2k) (caller pre-zeroed). ----
    multiply_toom_cook_4(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- c6 = a3*b3, written into result[6k..). ----
    // The min > 3*k gate above guarantees both a3 and b3 are non-empty.
    multiply_toom_cook_4(result.subspan(6 * k, a3.size() + b3.size()), a3, b3, scratch);

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2 + a3; tmpb similarly. ----
    std::ranges::copy(a0, tmpa.begin());
    std::size_t tmpa_size = a0.size();
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a3);

    std::ranges::copy(b0, tmpb.begin());
    std::size_t tmpb_size = b0.size();
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b3);

    // v1 = tmpa * tmpb
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_4(v1,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -1: tmpa = (a0 + a2) - (a1 + a3) signed; tmpb similarly. ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    // tmpb temporarily used to hold a1 + a3 before subtraction.
    std::ranges::copy(a1, tmpb.begin());
    std::size_t aux_size = a1.size();
    aux_size             = add_into_tmp(tmpb, aux_size, a3);
    const auto sub_a_m1 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{tmpb.data(), aux_size});
    tmpa_size            = sub_a_m1.size;
    const bool sign_a_m1 = sub_a_m1.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    // tmpa now holds (a0 + a2) - (a1 + a3); reuse tail of vm1 as a scratch slot for (b1 + b3).
    std::ranges::copy(b1, vm1.begin());
    aux_size = b1.size();
    aux_size = add_into_tmp(vm1, aux_size, b3);
    const auto sub_b_m1 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpb_size            = sub_b_m1.size;
    const bool sign_b_m1 = sub_b_m1.negative;
    const bool sign_vm1  = sign_a_m1 ^ sign_b_m1;

    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_4(vm1,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 2: tmpa = ((a3*2 + a2)*2 + a1)*2 + a0 (Horner); tmpb similarly. ----
    std::ranges::copy(a3, tmpa.begin());
    tmpa_size = a3.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);

    std::ranges::copy(b3, tmpb.begin());
    tmpb_size = b3.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_4(v2,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -2: tmpa = (a0 + 4*a2) - (2*a1 + 8*a3) signed; tmpb similarly.
    // Build positive = a0 + 4*a2 in tmpa, negative = 2*a1 + 8*a3 in tmpb, then signed-sub.
    std::ranges::copy(a2, tmpa.begin());
    tmpa_size = a2.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size); // 2*a2
    tmpa_size = shift_left_one(tmpa, tmpa_size); // 4*a2
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);

    std::ranges::copy(a3, tmpb.begin());
    aux_size = a3.size();
    aux_size = shift_left_one(tmpb, aux_size); // 2*a3
    aux_size = shift_left_one(tmpb, aux_size); // 4*a3
    aux_size = shift_left_one(tmpb, aux_size); // 8*a3
    aux_size = add_into_tmp(tmpb, aux_size, a1);
    // tmpb holds 8*a3 + a1; we still need 2*a1, which is (8*a3 + a1) + a1 = 8*a3 + 2*a1.
    aux_size = add_into_tmp(tmpb, aux_size, a1);

    const auto sub_a_m2 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{tmpb.data(), aux_size});
    tmpa_size            = sub_a_m2.size;
    const bool sign_a_m2 = sub_a_m2.negative;

    std::ranges::copy(b2, tmpb.begin());
    tmpb_size = b2.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    // Use vm2 as a scratch slot for 8*b3 + 2*b1.
    std::ranges::copy(b3, vm2.begin());
    aux_size = b3.size();
    aux_size = shift_left_one(vm2, aux_size);
    aux_size = shift_left_one(vm2, aux_size);
    aux_size = shift_left_one(vm2, aux_size);
    aux_size = add_into_tmp(vm2, aux_size, b1);
    aux_size = add_into_tmp(vm2, aux_size, b1);

    const auto sub_b_m2 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpb_size            = sub_b_m2.size;
    const bool sign_b_m2 = sub_b_m2.negative;
    const bool sign_vm2  = sign_a_m2 ^ sign_b_m2;

    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_4(vm2,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled by 8): tmpa = ((a0*2 + a1)*2 + a2)*2 + a3 (Horner);
    // this equals 8*a(1/2), so the resulting product is 64*c(1/2). ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);

    std::ranges::fill(vh, uint_multiprecision_t{0});
    multiply_toom_cook_4(vh,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Bodrato-style interpolation (in-place; full 2k+2 buffers throughout) ----
    // After this sequence the c-coefficients live as:
    //   c0 in result[0..2k), c1 in vh, c2 in v1, c3 in vm1, c4 in v2, c5 in vm2, c6 in result[6k..).
    //
    // v0_view and vinf_view bound the c0 and c6 regions in result. Clip vinf_view because
    // the result span may be larger than 8k when the caller's prod_cap exceeds the actual
    // product (same trick as Toom-3 at line 642).
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 6 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 6 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view  = std::span<const uint_multiprecision_t>{vm2};
    const auto vh_view   = std::span<const uint_multiprecision_t>{vh};
    const auto td_view   = std::span<const uint_multiprecision_t>{tmp_double};

    // Phase 1: Symmetrize {v1, vm1} into {E1, D1} and {v2, vm2} into {E2, D2}.
    //   E1 = (v1 + vm1)/2 = c0 + c2 + c4 + c6
    //   D1 = (v1 - vm1)/2 = c1 + c3 + c5
    //   E2 = (v2 + vm2)/2 = c0 + 4c2 + 16c4 + 64c6
    //   D2 = (v2 - vm2)/4 = c1 + 4c3 + 16c5

    // Step 1a: v1 <- (v1 + vm1) algebraic (sign-aware on sign_vm1).
    if (sign_vm1) {
        subtract_unsigned_spans(v1, v1_view, vm1_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    // Step 1b: v1 /= 2.
    {
        [[maybe_unused]] const auto rem = shift_right_one(v1);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 1: v1 = E1 = c0 + c2 + c4 + c6.

    // Step 2: vm1 <- v1 - vm1 algebraic = (v1_orig - vm1_orig)/2 = D1.
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm1, v1_view, vm1_view);
    }
    // After Step 2: vm1 = D1 = c1 + c3 + c5. sign_vm1 is no longer needed.

    // Step 3a: v2 <- (v2 + vm2) algebraic.
    if (sign_vm2) {
        subtract_unsigned_spans(v2, v2_view, vm2_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    // Step 3b: v2 /= 2.
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 3: v2 = E2 = c0 + 4c2 + 16c4 + 64c6.

    // Step 4a: vm2 <- v2 - vm2 algebraic = (v2_orig - vm2_orig)/2 = 2c1 + 8c3 + 32c5.
    if (sign_vm2) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm2, v2_view, vm2_view);
    }
    // Step 4b: vm2 /= 2.
    {
        [[maybe_unused]] const auto rem = shift_right_one(vm2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 4: vm2 = D2 = c1 + 4c3 + 16c5. sign_vm2 is no longer needed.

    // Phase 2: Solve even system for c2 (into v1) and c4 (into v2).

    // Step 5: v2 -= v1.  v2 = (c0+4c2+16c4+64c6) - (c0+c2+c4+c6) = 3*(c2 + 5c4 + 21c6).
    subtract_unsigned_spans(v2, v2_view, v1_view);
    // Step 6: v2 /= 3.  v2 = c2 + 5c4 + 21c6.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 7: v1 -= c0.  v1 = c2 + c4 + c6.
    subtract_unsigned_spans(v1, v1_view, v0_view);
    // Step 8: v1 -= c6.  v1 = c2 + c4.
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 9: v2 -= v1.  v2 = 4c4 + 21c6.
    subtract_unsigned_spans(v2, v2_view, v1_view);

    // Step 10: subtract 21*c6 from v2 using tmp_double for the doublings.
    //   v2 -= 1*c6, then v2 -= 4*c6, then v2 -= 16*c6.  Total subtracted = 21*c6.
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vinf_view, tmp_double.begin());
    std::size_t td_size = vinf_view.size();
    td_size             = shift_left_one(tmp_double, td_size); //  2*c6
    td_size             = shift_left_one(tmp_double, td_size); //  4*c6
    subtract_unsigned_spans(v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    td_size = shift_left_one(tmp_double, td_size); //  8*c6
    td_size = shift_left_one(tmp_double, td_size); // 16*c6
    subtract_unsigned_spans(v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    // After Step 10: v2 = 4c4.

    // Step 11: v2 /= 4 (two halvings).  v2 = c4.
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 12: v1 -= v2.  v1 = c2.
    subtract_unsigned_spans(v1, v1_view, v2_view);

    // Phase 3: Reduce vh to T_odd = (vh - 64c0 - 16c2 - 4c4 - c6) / 2 = 16c1 + 4c3 + c5.

    // Step 13: vh -= 64*c0 (subtract 64*c0 via doubled-tmp_double).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    td_size = trimmed_size_span(v0_view);
    for (int i = 0; i < 6; ++i) {
        td_size = shift_left_one(tmp_double, td_size);
    }
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // Step 14: vh -= 16*c2 (c2 lives in v1 now).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v1_view, tmp_double.begin());
    td_size = trimmed_size_span(v1_view);
    for (int i = 0; i < 4; ++i) {
        td_size = shift_left_one(tmp_double, td_size);
    }
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // Step 15: vh -= 4*c4 (c4 lives in v2 now).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v2_view, tmp_double.begin());
    td_size = trimmed_size_span(v2_view);
    td_size = shift_left_one(tmp_double, td_size);
    td_size = shift_left_one(tmp_double, td_size);
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // Step 16: vh -= c6.
    subtract_unsigned_spans(vh, vh_view, vinf_view);

    // Step 17: vh /= 2.  vh = 16c1 + 4c3 + c5 = T_odd.
    {
        [[maybe_unused]] const auto rem = shift_right_one(vh);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 4: Solve odd system using vm1 = D1, vm2 = D2, vh = T_odd.
    //   vm2 -= D1:  3c3 + 15c5
    //   vh  -= D1: 15c1 +  3c3
    //   then divide both by 3.

    // Step 18: vm2 -= vm1.  vm2 = 3*(c3 + 5c5).
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    // Step 19: vh -= vm1.  vh = 3*(5c1 + c3).
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    // Step 20: vm2 /= 3.  vm2 = alpha = c3 + 5c5.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // Step 21: vh /= 3.  vh = beta = 5c1 + c3.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Now vm1 = D1, vm2 = alpha, vh = beta. Recover c3 = (5*D1 - alpha - beta) / 3 into vm1.
    // Step 22: tmp_double = D1; vm1 *= 4; vm1 += tmp_double  (-> 5*D1).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vm1_view, tmp_double.begin());
    [[maybe_unused]] const auto sz_2D1 = shift_left_one(vm1, vm1.size()); // 2*D1
    BEMAN_BIG_INT_DEBUG_ASSERT(sz_2D1 == vm1.size());
    [[maybe_unused]] const auto sz_4D1 = shift_left_one(vm1, vm1.size()); // 4*D1
    BEMAN_BIG_INT_DEBUG_ASSERT(sz_4D1 == vm1.size());
    {
        [[maybe_unused]] const bool carry = add_unsigned_spans(vm1, vm1_view, td_view); // 5*D1
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    }
    // Step 23: vm1 -= alpha; vm1 -= beta.  vm1 = 3*c3.
    subtract_unsigned_spans(vm1, vm1_view, vm2_view);
    subtract_unsigned_spans(vm1, vm1_view, vh_view);
    // Step 24: vm1 /= 3.  vm1 = c3.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vm1, vm1_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 25: vh -= c3 -> 5*c1; vh /= 5.  vh = c1.
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 26: vm2 -= c3 -> 5*c5; vm2 /= 5.  vm2 = c5.
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 5: Recompose.
    //   result += vh * B^k        (c1)
    //   result += v1 * B^(2k)     (c2)
    //   result += vm1 * B^(3k)    (c3)
    //   result += v2 * B^(4k)     (c4)
    //   result += vm2 * B^(5k)    (c5)
    // c0 and c6 are already in result.
    // Carries propagate up to result.size().
    add_shifted(result, k, vh_view);
    add_shifted(result, 2 * k, v1_view);
    add_shifted(result, 3 * k, vm1_view);
    add_shifted(result, 4 * k, v2_view);
    add_shifted(result, 5 * k, vm2_view);

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
}

// ---------------------------------------------------------------------------
// Top-level multiplication dispatcher.
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// Returns the number of significant result limbs (trimmed).
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t multiply_dispatch(const std::span<uint_multiprecision_t>       result,
                                        const std::span<const uint_multiprecision_t> a_untrimmed,
                                        const std::span<const uint_multiprecision_t> b_untrimmed,
                                        Allocator&                                   alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_untrimmed.size() + b_untrimmed.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Trivial case, use single-limb shortcuts
    if (a.size() == 1 && b.size() == 1) {
        const auto [lo, hi] = widening_mul(a[0], b[0]);
        result[0]           = lo;
        result[1]           = hi;
        return hi != 0 ? 2 : 1;
    }
    if (a.size() == 1) {
        return multiply_single_limb(result, b, a[0]);
    }
    if (b.size() == 1) {
        return multiply_single_limb(result, a, b[0]);
    }

    // Use Toom-Cook 3 above its cutoff, Karatsuba above its cutoff, and schoolbook below.
    // Avoid these at compile time because the recursion depth could blow up consteval limits;
    // long multiplication works just fine in that case.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (a.size() >= toom_cook_4_cutoff && b.size() >= toom_cook_4_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = toom_cook_4_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            scratch_allocator<Allocator> scratch(storage_size, alloc);
            multiply_toom_cook_4(result.first(result_total), a, b, scratch);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
        if (a.size() >= toom_cook_3_cutoff && b.size() >= toom_cook_3_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = toom_cook_3_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            scratch_allocator<Allocator> scratch(storage_size, alloc);
            multiply_toom_cook_3(result.first(result_total), a, b, scratch);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
        if (a.size() >= karatsuba_cutoff && b.size() >= karatsuba_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = karatsuba_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            if (storage_size <= karatsuba_stack_threshold) {
                uint_multiprecision_t        stack_buf[karatsuba_stack_threshold];
                scratch_allocator<Allocator> scratch(stack_buf, karatsuba_stack_threshold, alloc);
                multiply_karatsuba(result.first(result_total), a, b, scratch);
            } else {
                scratch_allocator<Allocator> scratch(storage_size, alloc);
                multiply_karatsuba(result.first(result_total), a, b, scratch);
            }
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
    }

    // Long multiplication fallback
    multiply_long(result, a, b);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size() + b.size()});
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_MUL_IMPL_HPP
