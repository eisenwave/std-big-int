// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MUL_IMPL_LOW_LEVEL_OPT_HPP
#define BEMAN_BIG_INT_MUL_IMPL_LOW_LEVEL_OPT_HPP

#include <beman/big_int/detail/wide_ops.hpp>

namespace beman::big_int::detail {

// Forward declaration.
extern "C" constexpr void multiply_long_low_level(uint_multiprecision_t*       p_result,
                                                  const uint_multiprecision_t* p_a,
                                                  const std::size_t            len_a,
                                                  const uint_multiprecision_t* p_b,
                                                  const std::size_t            len_b) noexcept;

// ---------------------------------------------------------------------------
// TODO(ckormanyos): This is a temporary iteration toward low-level optimization
//                   of schoolbook multiplication. Low-level primitives including
//                   pointers and lengths are used for multiplication - such as
//                   can be used in C/asm. This routine is intended to be iteratively
//                   isolated runtime path only in several steps.
//                   The isolation steps might include:
//                     * The pairs as return types from primitives might need to be
//                       broken up into two lines each. Or use a slightly different
//                       approach on input/output params.
//                     * Alternatively, this subroutine might simmply be entirely
//                       rewritten in C/asm, ultimately moving to assembly.
//                     * It may for some compiler/assembler systems, however, remain
//                       as a separate header file. Modern C++ is _supposed_ _to_
//                       support constexpr inline assembly.
//                     * Place the subroutine in s aseparace C++ file with extern "C"
//                       linkage.
//                     * Isolate the runtime path (if not already done so) and call
//                       the isolated (now in source file) routine only for the
//                       runtime path.
//                     * Transform the subroutine into a naked assembly implementation.
//                     * Remove any redundant calling perhaps left over from the process
//                       of iterative isolation.
extern "C" constexpr void multiply_long_low_level(uint_multiprecision_t*       p_result,
                                                  const uint_multiprecision_t* p_a,
                                                  const std::size_t            len_a,
                                                  const uint_multiprecision_t* p_b,
                                                  const std::size_t            len_b) noexcept {

    // Low-level schoolbook multiplication intended to be iteratively worked
    // into C/asm for selected architectures in the non-constexpr, non-consteval
    // path. This could be inline assembly for some compiler/assembler systems
    // or implemented in an assembly source file for others.

    {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < len_b; ++j) {
            const auto [lo, hi] = widening_mul(*p_a, *(p_b + j));
            const auto [s, c]   = carrying_add(lo, carry);
            *(p_result + j)     = s;
            carry               = hi + static_cast<uint_multiprecision_t>(c);
        }
        *(p_result + len_b) = carry;
    }

    // Subsequent rows: accumulate onto values written by previous rows.
    for (std::size_t i = 1; i < len_a; ++i) {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < len_b; ++j) {
            const auto [lo, hi]   = widening_mul(*(p_a + i), *(p_b + j));
            const auto [s1, c1]   = carrying_add(lo, *(p_result + (i + j)));
            const auto [s2, c2]   = carrying_add(s1, carry);
            *(p_result + (i + j)) = s2;
            carry = hi + static_cast<uint_multiprecision_t>(c1) + static_cast<uint_multiprecision_t>(c2);
        }
        *(p_result + (i + len_b)) = carry;
    }
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_MUL_IMPL_LOW_LEVEL_OPT_HPP
