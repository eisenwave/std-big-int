// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MUL_IMPL_HPP
#define BEMAN_BIG_INT_MUL_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

#include <algorithm>
#include <memory>
#include <span>

namespace beman::big_int::detail {

// Minimum number of limbs for Karatsuba to be worthwhile
// Directly from Boost
inline constexpr std::size_t karatsuba_cutoff = 40;

// Heuristic estimate of scratch space needed for Karatsuba multiplication
// Directly from Boost
constexpr std::size_t karatsuba_storage_size(const std::size_t s) noexcept { return 5 * s; }

// Maximum number of scratch limbs we're willing to put on the stack.
// Directly from Boost
inline constexpr std::size_t karatsuba_stack_threshold = 300;

// Scratchpad using the provided allocator from basic_big_int
// This is a bump allocator using LIFO storage
//
// Karatsuba recursion allocates temporaries from this,
// then "deallocates" after each level returns so sibling branches reuse the same memory.
// This deallocation is simply moving the pointer back
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

template <class Allocator>
struct scratch_allocator {
    using alloc_traits = std::allocator_traits<Allocator>;
    using pointer      = alloc_traits::pointer;

    BEMAN_BIG_INT_NO_UNIQUE_ADDRESS Allocator m_alloc;
    pointer                                   m_base;
    std::size_t                               m_capacity;
    std::size_t                               m_offset = 0;
    bool                                      m_owns;

    // Wrap an existing stack buffer — no ownership.
    constexpr scratch_allocator(pointer buf, const std::size_t cap, const Allocator& alloc) noexcept
        : m_alloc(alloc), m_base(buf), m_capacity(cap), m_owns(false) {}

    // Heap-allocate at least `cap` limbs using the provided allocator.
    constexpr scratch_allocator(std::size_t cap, const Allocator& alloc)
        : m_alloc(alloc), m_base(nullptr), m_capacity(0), m_owns(true) {
#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
        auto result = alloc_traits::allocate_at_least(m_alloc, cap);
        m_base      = result.ptr;
        m_capacity  = result.count;
#else
        m_base     = alloc_traits::allocate(m_alloc, cap);
        m_capacity = cap;
#endif
    }

    constexpr ~scratch_allocator() {
        if (m_owns) {
            alloc_traits::deallocate(m_alloc, m_base, m_capacity);
        }
    }

    scratch_allocator(const scratch_allocator&)            = delete;
    scratch_allocator& operator=(const scratch_allocator&) = delete;

    // Bump-allocate `n` limbs from the workspace, returned as a mutable span.
    constexpr std::span<uint_multiprecision_t> allocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(m_offset + n <= m_capacity);
        auto* p = std::to_address(m_base) + m_offset;
        m_offset += n;
        return {p, n};
    }

    // LIFO release of the last `n` limbs so sibling branches can reuse them.
    constexpr void deallocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(n <= m_offset);
        m_offset -= n;
    }
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

// Trim leading zero limbs returning the effective size
constexpr std::size_t trimmed_size(const std::span<const uint_multiprecision_t> s) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!s.empty());
    std::size_t n = s.size();
    while (n > 1 && s[n - 1] == 0) {
        --n;
    }
    return n;
}

// Returns true if all limbs in the span are zero
constexpr bool is_span_zero(const std::span<const uint_multiprecision_t> s) noexcept {
    return std::ranges::all_of(s, [](const uint_multiprecision_t limb) { return limb == 0; });
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
    return trimmed_size(std::span<const uint_multiprecision_t>{result.data(), a.size()});
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
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size(a) + trimmed_size(b));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());

    a = a.first(trimmed_size(a));
    b = b.first(trimmed_size(b));

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

    const uint_multiprecision_t zero = 0;
    const auto                  a_h  = a.size() > n ? a.subspan(n) : std::span<const uint_multiprecision_t>(&zero, 1);

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
    const auto result_low  = result.first(2 * n);
    const auto result_high = result.size() > 2 * n ? result.subspan(2 * n) : std::span<uint_multiprecision_t>{};

    // Compute result_low = a_l * b_l
    multiply_karatsuba(result_low, a_l, b_l, scratch);
    const std::size_t result_low_size =
        trimmed_size(std::span<const uint_multiprecision_t>{result_low.data(), a_l.size() + b_l.size()});

    // Zero unused limbs in result_low region
    std::ranges::fill(result_low.subspan(result_low_size), uint_multiprecision_t{0});

    // Compute result_high = a_h * b_h
    if (!result_high.empty()) {
        multiply_karatsuba(result_high, a_h, b_h, scratch);
        const std::size_t result_high_size =
            trimmed_size(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});

        // Zero unused limbs in result_high region
        std::ranges::fill(result_high.subspan(result_high_size), uint_multiprecision_t{0});
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
    std::size_t t1_size = trimmed_size(std::span<const uint_multiprecision_t>{t1.data(), t2_size + t3_size});

    // t1 -= result_high (a_h * b_h)
    if (!result_high.empty()) {
        const std::size_t rh_size =
            trimmed_size(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});
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

    a = a.first(trimmed_size(a));
    b = b.first(trimmed_size(b));

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

    // Use Karatsuba when both operands are large enough
    // Also avoid using this function at compile time, because we can end up with high recursion depth
    // Unlikely to matter, but long multiplication works just fine in this case
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
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
            return trimmed_size(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
    }

    // Long multiplication fallback
    multiply_long(result, a, b);
    return trimmed_size(std::span<const uint_multiprecision_t>{result.data(), a.size() + b.size()});
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_MUL_IMPL_HPP
