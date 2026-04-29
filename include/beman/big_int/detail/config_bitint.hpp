// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_BITINT_HPP
#define BEMAN_BIG_INT_CONFIG_BITINT_HPP

// _BitInt support =====================================================

__extension__ template <const unsigned N>
using bit_int = _BitInt(N);

__extension__ template <const unsigned N>
using bit_uint = unsigned _BitInt(N);

#endif // BEMAN_BIG_INT_CONFIG_BITINT_HPP
