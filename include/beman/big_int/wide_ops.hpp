// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_WIDE_OPS_HPP
#define BEMAN_BIG_INT_WIDE_OPS_HPP

#include <limits>
#include <bit>

#include <beman/big_int/config.hpp>

namespace beman::big_int::detail {

template <std::integral T>
inline constexpr std::size_t width_v = std::numeric_limits<std::make_unsigned_t<T>>::digits;

struct wide {
    uint_multiprecision_t low_bits;
    uint_multiprecision_t high_bits;
};

[[nodiscard]] constexpr wide widening_mul(uint_multiprecision_t x, uint_multiprecision_t y) noexcept {
    // Based on mul_wide from P3161R4, but with different names.
    // P4052R0 renamed "mul_sat" to "saturating_mul",
    // and the corresponding Rust-style rename for "mul_wide" is "widening_mul".
    //
    // Currently, it would be fine if we just returning uint_wide_t,
    // but enshrines the assumption that we have a 128-bit integer type everywhere.
    // Also, we often need to break up the result into limbs anyway.
    auto product = static_cast<uint_wide_t>(x) * static_cast<uint_wide_t>(y);
    if constexpr (std::endian::native == std::endian::little) {
        return std::bit_cast<wide>(product);
    } else {
        return {
            .low_bits  = static_cast<uint_multiprecision_t>(product),
            .high_bits = static_cast<uint_multiprecision_t>(product >> 64),
        };
    }
}

template <class T>
struct overflow_result {
    T    value;
    bool overflow;
};

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_add(T x, T y) noexcept {
#ifdef BEMAN_BIG_INT_GNUC
    T    value;
    bool overflow = __builtin_add_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    static_assert(width_v<T> <= 64, "Don't need more than 64-bit checked operations for now.");
    using Wide = std::conditional_t<std::is_signed_v<T>, int128_t, uint128_t>;
    Wide wide  = static_cast<Wide>(x) + static_cast<Wide>(y);
    auto value = static_cast<T>(wide);
    return {.value = value, .overflow = wide != value};
#endif // BEMAN_BIG_INT_GNUC
}

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_sub(T x, T y) noexcept {
#ifdef BEMAN_BIG_INT_GNUC
    T    value;
    bool overflow = __builtin_sub_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    static_assert(width_v<T> <= 64, "Don't need more than 64-bit checked operations for now.");
    using Wide = std::conditional_t<std::is_signed_v<T>, int128_t, uint128_t>;
    Wide wide  = static_cast<Wide>(x) - static_cast<Wide>(y);
    auto value = static_cast<T>(wide);
    return {.value = value, .overflow = wide != value};
#endif // BEMAN_BIG_INT_GNUC
}

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_mul(T x, T y) noexcept {
#ifdef BEMAN_BIG_INT_GNUC
    T    value;
    bool overflow = __builtin_mul_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    static_assert(width_v<T> <= 64, "Don't need more than 64-bit checked operations for now.");
    using Wide = std::conditional_t<std::is_signed_v<T>, int128_t, uint128_t>;
    Wide wide  = static_cast<Wide>(x) * static_cast<Wide>(y);
    auto value = static_cast<T>(wide);
    return {.value = value, .overflow = wide != value};
#endif // BEMAN_BIG_INT_GNUC
}

template <class T>
struct carry_result {
    T    value;
    bool carry;
};

template <unsigned_integer T>
[[nodiscard]] constexpr carry_result<T> carrying_add(T x, T y, bool carry) noexcept {
    static_assert(width_v<T> == 64, "Don't need anything but 64-bit for now.");
#ifdef BEMAN_BIG_INT_GNUC
    bool               carry_out;
    unsigned long long value = __builtin_addcll(x, y, carry, &carry_out);
    return {.value = value, .carry = carry_out};
#else
    auto result    = static_cast<uint128_t>(x) + static_cast<uint128_t>(y) + carry;
    bool carry_out = (result >> 64) != 0;
    return {.value = static_cast<T>(result), .carry = carry_out};
#endif // BEMAN_BIG_INT_GNUC
}

template <class T>
struct borrow_result {
    T    value;
    bool borrow;
};

template <unsigned_integer T>
[[nodiscard]] constexpr borrow_result<T> borrowing_sub(T x, T y, bool borrow) noexcept {
    static_assert(width_v<T> == 64, "Don't need anything but 64-bit for now.");
#ifdef BEMAN_BIG_INT_GNUC
    bool               borrow_out;
    unsigned long long value = __builtin_subcll(x, y, borrow, &borrow_out);
    return {.value = value, .borrow = borrow_out};
#else
    auto result     = static_cast<uint128_t>(x) - static_cast<uint128_t>(y) - borrow;
    bool borrow_out = (result >> 64) != 0;
    return {.value = static_cast<T>(result), .borrow = borrow_out};
#endif // BEMAN_BIG_INT_GNUC
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_WIDE_OPS_HPP
