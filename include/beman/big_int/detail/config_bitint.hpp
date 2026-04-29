// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_BITINT_HPP
#define BEMAN_BIG_INT_CONFIG_BITINT_HPP

#if defined(BEMAN_BIG_INT_GCC) || defined(BEMAN_BIG_INT_CLANG)
    #pragma GCC system_header
#endif

// _BitInt support =====================================================

using bitint32_t   = _BitInt(32);
using ubitint32_t  = unsigned _BitInt(32);
using bitint64_t   = _BitInt(64);
using ubitint64_t  = unsigned _BitInt(64);
using bitint128_t  = _BitInt(128);
using ubitint128_t = unsigned _BitInt(128);
template <const unsigned N>
using bitint_n_t = _BitInt(N);
template <const unsigned N>
using ubitint_n_t = unsigned _BitInt(N);

#endif // BEMAN_BIG_INT_CONFIG_BITINT_HPP
