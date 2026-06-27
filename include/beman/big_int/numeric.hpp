// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_NUMERIC_HPP
#define BEMAN_BIG_INT_NUMERIC_HPP

#include <type_traits>
#include <utility>

#include <beman/big_int/big_int.hpp>

namespace beman::big_int {

// Returns the absolute value (magnitude) of `x`.
// This cannot overflow since `basic_big_int` is unbounded
template <class T>
    requires detail::is_basic_big_int_v<T>
constexpr std::remove_cvref_t<T> abs(T&& x) {
    std::remove_cvref_t<T> result(std::forward<T>(x));
    result.unchecked_set_sign(false);
    return result;
}

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_NUMERIC_HPP
