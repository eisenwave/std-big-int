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
constexpr void multiply_karatsuba(const std::span<uint_multiprecision_t> result,
                                  std::span<const uint_multiprecision_t> a,
                                  std::span<const uint_multiprecision_t> b,
                                  scratch_allocator<Allocator>&          scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a) + trimmed_size_span(b));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());

    a = a.first(trimmed_size_span(a));
    b = b.first(trimmed_size_span(b));

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

// Minimum number of limbs for Toom-Cook 3 to be worthwhile.
// Toom-Cook 3 first beats Karatsuba around 800 limbs on Apple Silicon.
inline constexpr std::size_t toom_cook_3_cutoff = 800;

// Heuristic estimate of scratch space needed for Toom-Cook 3 multiplication.
// Includes space for karatsuba fallback plan
constexpr std::size_t toom_cook_3_storage_size(const std::size_t s) noexcept { return 8 * s; }

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
constexpr void multiply_toom_cook_3(const std::span<uint_multiprecision_t> result,
                                    std::span<const uint_multiprecision_t> a,
                                    std::span<const uint_multiprecision_t> b,
                                    scratch_allocator<Allocator>&          scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a) + trimmed_size_span(b));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());

    a = a.first(trimmed_size_span(a));
    b = b.first(trimmed_size_span(b));

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

    // ---- Helper: in-place tmp[0..size) += addend; returns new size (may grow by 1) ----
    constexpr auto add_into_tmp = [](const std::span<uint_multiprecision_t>       tmp,
                                     const std::size_t                            size,
                                     const std::span<const uint_multiprecision_t> addend) -> std::size_t {
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
    };

    // ---- Helper: in-place tmp[0..size) <<= 1; returns new size (may grow by 1) ----
    constexpr auto shift_left_one = [](const std::span<uint_multiprecision_t> tmp, std::size_t size) -> std::size_t {
        if (size == 0) {
            return 0;
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > size);
        constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
        uint_multiprecision_t prev            = 0;
        for (std::size_t i = 0; i < size; ++i) {
            const auto limb = tmp[i];
            tmp[i]          = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, 1u);
            prev            = limb;
        }
        if (const auto carry = prev >> (local_limb_bits - 1)) {
            tmp[size++] = carry;
        }
        return size;
    };

    // ---- Helper: in-place tmp >>= 1; returns the dropped low bit (caller asserts == 0 for exact div) ----
    constexpr auto shift_right_one = [](const std::span<uint_multiprecision_t> tmp) -> uint_multiprecision_t {
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
    };

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
    const auto add_shifted = [&](const std::size_t shift, const std::span<const uint_multiprecision_t> src) {
        const auto dest  = result.subspan(shift);
        bool       carry = false;
        for (std::size_t i = 0; i < dest.size(); ++i) {
            const auto si            = i < src.size() ? src[i] : uint_multiprecision_t{0};
            const auto [r_value, c1] = carrying_add(dest[i], si, carry);
            dest[i]                  = r_value;
            carry                    = c1;
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    };

    add_shifted(k, vm1_view);
    add_shifted(2 * k, v1_view);
    add_shifted(3 * k, v2_view);

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    scratch.deallocate(total_scratch);
}

// ---------------------------------------------------------------------------
// Top-level multiplication dispatcher.
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// Returns the number of significant result limbs (trimmed).
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t multiply_dispatch(const std::span<uint_multiprecision_t> result,
                                        std::span<const uint_multiprecision_t> a,
                                        std::span<const uint_multiprecision_t> b,
                                        Allocator&                             alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());

    a = a.first(trimmed_size_span(a));
    b = b.first(trimmed_size_span(b));

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
