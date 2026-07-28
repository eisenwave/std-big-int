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

// [numeric.sat.cast]
// Casts `x` to `R`, clamping to the range of `R`. If the integer value of `x`
// is representable as `R`, that value is returned; otherwise the largest or
// smallest representable value of `R`, whichever is closer to `x`.
template <class R, std::size_t b, class A, class LimbType>
    requires detail::signed_or_unsigned<R>
constexpr R saturating_cast(const basic_big_int<b, A, LimbType>& x) noexcept {
    using U = detail::make_unsigned_t<R>;

    constexpr std::size_t width = detail::width_v<R>;
    if constexpr (detail::unsigned_integer<R>) {
        constexpr R hi = static_cast<R>(~U{0});

        if (x < R{0}) {
            return R{0};
        }
        if (x > hi) {
            return hi;
        }
    } else {
        constexpr R lo = static_cast<R>(U{1} << (width - 1));
        constexpr R hi = static_cast<R>((U{1} << (width - 1)) - U{1});

        if (x < lo) {
            return lo;
        }
        if (x > hi) {
            return hi;
        }
    }

    return static_cast<R>(x);
}

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_NUMERIC_HPP
