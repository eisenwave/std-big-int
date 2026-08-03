// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
#define BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP

#include <beman/big_int/detail/wide_ops.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wcomment")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wcomment")

// TODO(ckormanyos): Toggle to/from using assembly by commenting or
//                   uncommenting one of the following two macros.
//                   When the upper macro is active, assembly is used.
//                   When the lower macro is active, C++ code is used.
//                   Only one macro should be activated at any time.
//                   Note that the upper macro spans multiple lines.

#define MULTIPLY_LONG_RUNTIME(PARAM_RESULT, PARAM_A, PARAM_B) \
    ::multiply_long_runtime(                                  \
        (PARAM_RESULT).data(), (PARAM_A).data(), (PARAM_A).size(), (PARAM_B).data(), (PARAM_B).size())
// #define MULTIPLY_LONG_RUNTIME(PARAM_RESULT, PARAM_A, PARAM_B) multiply_long((PARAM_RESULT), (PARAM_A), (PARAM_B))

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#if defined(BEMAN_BIG_INT_ARCH_X86_64_GCC_OR_CLANG)

extern "C" void multiply_long_runtime(beman::big_int::uint_multiprecision_t*       p_result,
                                      const beman::big_int::uint_multiprecision_t* p_a,
                                      const std::size_t                            len_a,
                                      const beman::big_int::uint_multiprecision_t* p_b,
                                      const std::size_t                            len_b) noexcept;

#else

extern "C" inline void multiply_long_runtime(beman::big_int::uint_multiprecision_t*       p_result,
                                             const beman::big_int::uint_multiprecision_t* p_a,
                                             const std::size_t                            len_a,
                                             const beman::big_int::uint_multiprecision_t* p_b,
                                             const std::size_t                            len_b) noexcept {
    {
        beman::big_int::uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < len_b; ++j) {
            const auto [lo, hi] = beman::big_int::detail::widening_mul(*p_a, *(p_b + j));
            const auto [s, c]   = beman::big_int::detail::carrying_add(lo, carry);
            *(p_result + j)     = s;
            carry               = hi + static_cast<beman::big_int::uint_multiprecision_t>(c);
        }
        *(p_result + len_b) = carry;
    }

    // Subsequent rows: accumulate onto values written by previous rows.
    for (std::size_t i = 1; i < len_a; ++i) {
        beman::big_int::uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < len_b; ++j) {
            const auto [lo, hi]   = beman::big_int::detail::widening_mul(*(p_a + i), *(p_b + j));
            const auto [s1, c1]   = beman::big_int::detail::carrying_add(lo, *(p_result + (i + j)));
            const auto [s2, c2]   = beman::big_int::detail::carrying_add(s1, carry);
            *(p_result + (i + j)) = s2;
            carry                 = hi + static_cast<beman::big_int::uint_multiprecision_t>(c1) +
                                    static_cast<beman::big_int::uint_multiprecision_t>(c2);
        }
        *(p_result + (i + len_b)) = carry;
    }
}

#endif // !defined(BEMAN_BIG_INT_ARCH_X86_64_GCC_OR_CLANG)

#endif // BEMAN_BIG_INT_MULTIPLY_LONG_RUNTIME_HPP
