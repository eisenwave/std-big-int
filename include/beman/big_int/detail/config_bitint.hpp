// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_BITINT_HPP
#define BEMAN_BIG_INT_CONFIG_BITINT_HPP

#if defined(BEMAN_BIG_INT_GCC) || defined(BEMAN_BIG_INT_CLANG)
    #pragma GCC system_header
#endif

// _BitInt support =====================================================

template <const unsigned N>
using bit_int = _BitInt(N);
template <const unsigned N>
using bit_uint = unsigned _BitInt(N);

#endif // BEMAN_BIG_INT_CONFIG_BITINT_HPP
