// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
#define BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP

#include <beman/big_int/detail/wide_ops.hpp>

// TODO(ckormanyos): This is a dummy assembly routine. Remove
//                   when no longer needed as an example.

#if defined(BEMAN_BIG_INT_ARCH_X86_64_GCC_OR_CLANG)
extern "C" void multiply_long_runtime_dummy(beman::big_int::uint_multiprecision_t*       p_result,
                                            const beman::big_int::uint_multiprecision_t* p_a,
                                            const std::size_t                            len_a,
                                            const beman::big_int::uint_multiprecision_t* p_b,
                                            const std::size_t                            len_b) noexcept;
#else
extern "C" inline void multiply_long_runtime_dummy(beman::big_int::uint_multiprecision_t* /*p_result*/,
                                                   const beman::big_int::uint_multiprecision_t* /*p_a*/,
                                                   const std::size_t /*len_a*/,
                                                   const beman::big_int::uint_multiprecision_t* /*p_b*/,
                                                   const std::size_t /*len_b*/) noexcept {}
#endif

// #if !defined(BEMAN_BIG_INT_ARCH_X86_64_GCC_OR_CLANG)

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// TODO(ckormanyos): The preprocessor switch above should be set *after*
//                   implementing the assembly code. When that is done,
//                   this function will need to be activated only when
//                   #if !defined(BEMAN_BIG_INT_ARCH_X86_64_GCC_OR_CLANG)
//                   is detected via preprocessor (notice the NOT).

//                   And use only one of the functions multiply_long_runtime.
//                   The skeleton function will not be needed after the setup
//                   of the architecture becomes clear.

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

// #endif !defined(BEMAN_BIG_INT_ARCH_X86_64_GCC_OR_CLANG)

#endif // BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
