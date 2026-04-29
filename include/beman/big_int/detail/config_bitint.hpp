// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_BITINT_HPP
#define BEMAN_BIG_INT_CONFIG_BITINT_HPP

// _BitInt support =====================================================

__extension__ using bitint32_t   = _BitInt(32);
__extension__ using ubitint32_t  = unsigned _BitInt(32);
__extension__ using bitint64_t   = _BitInt(64);
__extension__ using ubitint64_t  = unsigned _BitInt(64);
__extension__ using bitint128_t  = _BitInt(128);
__extension__ using ubitint128_t = unsigned _BitInt(128);

__extension__ template <const unsigned N>
using bitint_n_t = _BitInt(N);

__extension__ template <const unsigned N>
using ubitint_n_t = unsigned _BitInt(N);

#endif // BEMAN_BIG_INT_CONFIG_BITINT_HPP
