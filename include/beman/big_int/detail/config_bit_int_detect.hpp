// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_BIT_INT_DETECT_HPP
#define BEMAN_BIG_INT_CONFIG_BIT_INT_DETECT_HPP

#if defined(BEMAN_BIG_INT_GCC) || defined(BEMAN_BIG_INT_CLANG)
    #pragma GCC system_header
#endif

// _BitInt detection ===========================================================

#include <climits> // for BITINT_MAXWIDTH

#ifdef BITINT_MAXWIDTH
    // Once _BitInt is a standard feature and available on all compilers,
    // this case should be selected for all compilers.
    #define BEMAN_BIG_INT_BITINT_MAXWIDTH BITINT_MAXWIDTH
    #define BEMAN_BIG_INT_HAS_BITINT 1
#elif defined(__BITINT_MAXWIDTH__)
    // This case is for Clang when it provides _BitInt as an extension.
    #define BEMAN_BIG_INT_BITINT_MAXWIDTH __BITINT_MAXWIDTH__
    #define BEMAN_BIG_INT_HAS_BITINT 1
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wbit-int-extension")
#else
    // Prevent warnings for use of undefined macros.
    #define BEMAN_BIG_INT_BITINT_MAXWIDTH 0
#endif // BITINT_MAXWIDTH

#endif // BEMAN_BIG_INT_CONFIG_BIT_INT_DETECT_HPP
