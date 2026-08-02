// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
#define BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP

#include <beman/big_int/detail/wide_ops.hpp>

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// TODO(ckormanyos): This is a further iteration toward low-level optimization
//                   of schoolbook multiplication.
//                   * The runtime path has been isolated and multiply_long_runtime
//                     is no longer constexpr. It can be specialized in assembly
//                     for a selected first architecture now.
//                   * Use preprocessor switches (that have not yet been invented)
//                     to identify, for example generic, unknown-x86_64 and
//                     write the subrotutine multiply_long_runtime(...) separately
//                     for the case of unknown-x86_64.
//                   * multiply_long_runtime(...) can be either inlined or embedded
//                     in an assembly file, since its name is already de-mangled
//                     through the use of C-linkage via extern "C".

extern "C" inline void multiply_long_runtime(uint_multiprecision_t*       p_result,
                                             const uint_multiprecision_t* p_a,
                                             const std::size_t            len_a,
                                             const uint_multiprecision_t* p_b,
                                             const std::size_t            len_b) noexcept {

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

#endif // BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
