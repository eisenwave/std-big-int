// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG128_HPP
#define BEMAN_BIG_INT_CONFIG128_HPP

#if defined(BEMAN_BIG_INT_GCC) || defined(BEMAN_BIG_INT_CLANG)
    #pragma GCC system_header
#endif

// 128-bit integer support =====================================================

#ifdef BEMAN_BIG_INT_MSVC
    #include <__msvc_int128.hpp>
#endif // BEMAN_BIG_INT_MSVC

namespace beman::big_int::detail {

#if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 128
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL 1
using int128_t  = _BitInt(128);
using uint128_t = unsigned _BitInt(128);
#elif defined(__SIZEOF_INT128__)
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL 1
using int128_t  = __int128;
using uint128_t = unsigned __int128;
#elif defined(BEMAN_BIG_INT_MSVC)
    #define BEMAN_BIG_INT_HAS_INT128 1
    #define BEMAN_BIG_INT_HAS_INT128_CLASS 1
using int128_t  = std::_Signed128;
using uint128_t = std::_Unsigned128;
#endif

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_CONFIG128_HPP
