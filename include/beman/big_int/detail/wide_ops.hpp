// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_WIDE_OPS_HPP
#define BEMAN_BIG_INT_WIDE_OPS_HPP

#include <bit>

#include <beman/big_int/detail/config.hpp>

#ifdef BEMAN_BIG_INT_MSVC
    #include <intrin.h>
#endif

namespace beman::big_int::detail {

// Denotes the integer type with twice the width of `T`
// and with the same signedness.
// For now, only integers with the same width as `uint_multiprecision_t` are supported.
template <signed_or_unsigned T>
    requires(width_v<T> == width_v<uint_multiprecision_t>)
using wider_t = std::conditional_t<std::is_signed_v<T>, int_wide_t, uint_wide_t>;

template <signed_or_unsigned T>
struct wide {
    T low_bits;
    T high_bits;

    [[nodiscard]] friend constexpr bool operator==(const wide& x, const wide& y) noexcept = default;
};

template <signed_or_unsigned T>
    requires requires { typename wider_t<T>; }
struct wide<T> {
    T low_bits;
    T high_bits;

    [[nodiscard]] friend constexpr bool operator==(const wide& x, const wide& y) noexcept = default;

    [[nodiscard]] static constexpr wide from_int(wider_t<T> x) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return std::bit_cast<wide>(x);
        } else {
            return {
                .low_bits  = static_cast<T>(x),
                .high_bits = static_cast<T>(x >> width_v<T>),
            };
        }
    }

    [[nodiscard]] constexpr wider_t<T> to_int() const noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return std::bit_cast<wider_t<T>>(*this);
        } else {
            return (static_cast<wider_t<T>>(high_bits) << width_v<T>) | low_bits;
        }
    }
};

namespace high_mul_detail {

template <signed_or_unsigned T>
    requires(width_v<T> == 64)
constexpr T high_mul_portable(const T x, const T y) noexcept {
    if constexpr (std::is_signed_v<T>) {
        // https://stackoverflow.com/a/28904636/5740428
        const T a_lo = static_cast<unsigned>(x);
        const T a_hi = static_cast<unsigned>(x >> 32);
        const T b_lo = static_cast<unsigned>(y);
        const T b_hi = static_cast<unsigned>(y >> 32);

        const T a_x_b_hi  = a_hi * b_hi;
        const T a_x_b_mid = a_hi * b_lo;
        const T b_x_a_mid = b_hi * a_lo;
        const T a_x_b_lo  = a_lo * b_lo;

        const T carry_bit = (static_cast<T>(static_cast<unsigned>(a_x_b_mid)) + //
                             static_cast<T>(static_cast<unsigned>(b_x_a_mid)) + //
                             (a_x_b_lo >> 32))                                  //
                            >> 32;

        return a_x_b_hi + (a_x_b_mid >> 32) + (b_x_a_mid >> 32) + carry_bit;
    } else {
        const T a_lo = static_cast<std::uint32_t>(x);
        const T a_hi = x >> 32;
        const T b_lo = static_cast<std::uint32_t>(y);
        const T b_hi = y >> 32;

        const T ll = a_lo * b_lo;
        const T lh = a_lo * b_hi;
        const T hl = a_hi * b_lo;
        const T hh = a_hi * b_hi;

        const T mid = (ll >> 32) + static_cast<std::uint32_t>(lh) + static_cast<std::uint32_t>(hl);
        return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    }
}

} // namespace high_mul_detail

// Returns the high 64 bits of the multiplication `x * y`.
template <signed_or_unsigned T>
    requires(width_v<T> <= 64)
constexpr T high_mul(const T x, const T y) noexcept {
    // We don't want to use 128-bit on MSVC because intrinsics are better
    // than the `std::_Unsigned128` type you get.
#if defined(BEMAN_BIG_INT_HAS_INT128) && !defined(BEMAN_BIG_INT_MSVC)
    return (x * static_cast<wider_t<T>>(y)) >> width_v<T>;
#else
    if constexpr (width_v<T> <= 32) {
        return (x * static_cast<wider_t<T>>(y)) >> width_v<T>;
    } else {
            // MSVC intrinsics are not usable during constant evaluation, so fall through
            // to the portable path when we're in a consteval context.
    #if defined(BEMAN_BIG_INT_MSVC)
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if constexpr (std::is_signed_v<T>) {
                return __mulh(x, y);
            } else {
                return __umulh(x, y);
            }
        }
    #elif defined(_M_AMD64)
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if constexpr (std::is_signed_v<T>) {
                __int64 result;
                void(_mul128(x, y, &result));
                return result;
            } else {
                unsigned __int64 result;
                void(_umul128(x, y, &result));
                return result;
            }
        }
    #endif
        if constexpr (width_v<T> == 64) {
            return high_mul_detail::high_mul_portable(x, y);
        } else {
            static_assert(width_v<unsigned long long> == 64);
            return high_mul<unsigned long long>(x, y);
        }
    }
#endif
}

// Returns both the low and the high bits of the multiplication `x * y`.
template <signed_or_unsigned T>
    requires(width_v<T> <= 64)
[[nodiscard]] constexpr wide<T> widening_mul(const T x, const T y) noexcept {
    // Based on mul_wide from P3161R4, but with different names.
    // P4052R0 renamed "mul_sat" to "saturating_mul",
    // and the corresponding Rust-style rename for "mul_wide" is "widening_mul".
    //
    // Currently, it would be fine if we just returned an integer,
    // but that enshrines the assumption that we have a 128-bit integer type everywhere.
    // Also, we often need to break up the result into limbs anyway.

    // We don't want to use 128-bit on MSVC because intrinsics are better
    // than the `std::_Unsigned128` type you get.
#if defined(BEMAN_BIG_INT_HAS_INT128) && !defined(BEMAN_BIG_INT_MSVC)
    const auto product = static_cast<wider_t<T>>(x) * static_cast<wider_t<T>>(y);
    return wide<T>::from_int(product);
#else
    if constexpr (width_v<T> <= 32) {
        const auto product = static_cast<wider_t<T>>(x) * static_cast<wider_t<T>>(y);
        return wide<T>::from_int(product);
    } else {
    #if defined(_M_AMD64)
        // MSVC intrinsics are not usable during constant evaluation, so fall through
        // to the portable path when we're in a consteval context.
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if constexpr (std::is_signed_v<T>) {
                __int64 high;
                __int64 low = _mul128(x, y, &high);
                return {low, high};
            } else {
                unsigned __int64 high;
                unsigned __int64 low = _umul128(x, y, &high);
                return {low, high};
            }
        }
    #endif
        using U = std::make_unsigned_t<T>;
        return {
            .low_bits  = static_cast<T>(static_cast<U>(x) * static_cast<U>(y)),
            .high_bits = high_mul(x, y),
        };
    }
#endif
}

// Design for funnel shifts is similar to P4010R0.

// Returns `x.high_bits << s`,
// except that the low bits are filled with `x.low_bits` instead of zeroes.
template <signed_or_unsigned T>
[[nodiscard]] constexpr T funnel_shl(const wide<T> x, const unsigned s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s < width_v<T>);
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_elementwise_fshl)
    return __builtin_elementwise_fshl(x.high_bits, x.low_bits, static_cast<T>(s));
#else
    if (s == 0) {
        return x.high_bits;
    }
    return (x.high_bits << s) | (x.low_bits >> (width_v<T> - s));
#endif
}

// Returns `x.low_bits >> s`,
// except that the high bits are filled with `x.high_bits` instead sign bits.
template <signed_or_unsigned T>
[[nodiscard]] constexpr T funnel_shr(const wide<T> x, const unsigned s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s < width_v<T>);
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_elementwise_fshr)
    return __builtin_elementwise_fshr(x.high_bits, x.low_bits, static_cast<T>(s));
#else
    // It usually makes sense to implement this in terms of a right-shift of a wider type.
    // However, the 128-bit version optimizes poorly;
    // Clang recognizes the else case as a funnel shift and optimizes that better.
    // In general, the 128-bit shift provides little benefit here.
    if constexpr (width_v<T> < 64 && requires { x.to_int(); }) {
        return static_cast<T>(x.to_int() >> s);
    } else {
        if (s == 0) {
            return x.low_bits;
        }
        return (x.low_bits >> s) | (x.high_bits << (width_v<T> - s));
    }
#endif
}

// These are going to be the standardized forms.
// Padding is expected and acceptable.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

template <class T>
struct overflow_result {
    T    value;
    bool overflow;
};

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_add(const T x, const T y) noexcept {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_add_overflow)
    T    value;
    bool overflow = __builtin_add_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    const auto wide  = static_cast<wider_t<T>>(x) + static_cast<wider_t<T>>(y);
    const auto value = static_cast<T>(wide);
    return {.value = value, .overflow = wide != value};
#endif // __builtin_add_overflow
}

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_sub(const T x, const T y) noexcept {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_sub_overflow)
    T    value;
    bool overflow = __builtin_sub_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    const auto wide  = static_cast<wider_t<T>>(x) - static_cast<wider_t<T>>(y);
    const auto value = static_cast<T>(wide);
    return {.value = value, .overflow = wide != value};
#endif // __builtin_sub_overflow
}

template <unsigned_integer T>
[[nodiscard]] constexpr overflow_result<T> overflowing_mul(const T x, const T y) noexcept {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_mul_overflow)
    T    value;
    bool overflow = __builtin_mul_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    const T value = x * y;
    return {.value = value, .overflow = x != 0 && value / x != y};
#endif // __builtin_mul_overflow
}

template <class T>
struct carry_result {
    T    value;
    bool carry;
};

template <unsigned_integer T>
[[nodiscard]] constexpr carry_result<T> carrying_add(const T x, const T y, const bool carry = false) noexcept {
    // None of the intrinsics below are portably constexpr,
    // so we put everything into a giant if !consteval block.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {

#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_addc)
        if constexpr (width_v<T> == 32) {
            unsigned carry_out;
            unsigned value = __builtin_addc(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        }
#endif

#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_addcll)
        if constexpr (width_v<T> == 64) {
            unsigned long long carry_out;
            unsigned long long value = __builtin_addcll(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        }
#endif

#if defined(BEMAN_BIG_INT_MSVC) && (defined(_M_AMD64) || defined(_M_IX86))
        // Theoretically we could use the ADX intrinsic, but then the user always has to compile for it
        // This is more portable.
        // Each branch returns directly, so we don't share uninitialized state
        // across the `if constexpr` chain.
        if constexpr (width_v<T> == 8) {
            std::uint8_t        value;
            const unsigned char carry_out = _addcarry_u8(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == 16) {
            std::uint16_t       value;
            const unsigned char carry_out = _addcarry_u16(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == 32) {
            std::uint32_t       value;
            const unsigned char carry_out = _addcarry_u32(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        }
    #ifdef _M_AMD64
        else if constexpr (width_v<T> == 64) {
            std::uint64_t       value;
            const unsigned char carry_out = _addcarry_u64(static_cast<unsigned char>(carry), x, y, &value);
            return {.value = value, .carry = carry_out != 0};
        }
    #endif
#endif // BEMAN_BIG_INT_GNUC
    }

    const auto result = static_cast<wider_t<T>>(x) + static_cast<wider_t<T>>(y) + carry;
    return {.value = static_cast<T>(result), .carry = (result >> width_v<T>) != 0};
}

template <class T>
struct borrow_result {
    T    value;
    bool borrow;
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

template <unsigned_integer T>
[[nodiscard]] constexpr borrow_result<T> borrowing_sub(const T x, const T y, const bool borrow = false) noexcept {
    // None of the intrinsics below are portably constexpr,
    // so we put everything into a giant if !consteval block.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {

#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_subc)
        if constexpr (width_v<T> == 32) {
            unsigned borrow_out;
            unsigned value = __builtin_subc(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        }
#endif

#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_subcll)
        if constexpr (width_v<T> == 64) {
            unsigned long long borrow_out;
            unsigned long long value = __builtin_subcll(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        }
#endif

#if defined(BEMAN_BIG_INT_MSVC) && (defined(_M_AMD64) || defined(_M_IX86))
        // Mirror the `carrying_add` MSVC path using the matching `_subborrow_*` intrinsics.
        // Each branch returns directly, so we don't share uninitialized state across the
        // `if constexpr` chain.
        if constexpr (width_v<T> == 8) {
            std::uint8_t        value;
            const unsigned char borrow_out = _subborrow_u8(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == 16) {
            std::uint16_t       value;
            const unsigned char borrow_out = _subborrow_u16(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == 32) {
            std::uint32_t       value;
            const unsigned char borrow_out = _subborrow_u32(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        }
    #ifdef _M_AMD64
        else if constexpr (width_v<T> == 64) {
            std::uint64_t       value;
            const unsigned char borrow_out = _subborrow_u64(static_cast<unsigned char>(borrow), x, y, &value);
            return {.value = value, .borrow = borrow_out != 0};
        }
    #endif
#endif
    }

    auto result = static_cast<wider_t<T>>(x) - static_cast<wider_t<T>>(y) - borrow;
    return {.value = static_cast<T>(result), .borrow = (result >> width_v<T>) != 0};
}

template <signed_or_unsigned T>
struct div_result {
    T quotient;
    T remainder;
};

// Returns the quotient and remainder of the division `x / y`.
// The behavior is undefined if the quotient is not representable as `T`,
// which is the case if and only if `x.high_bits < y`.
template <unsigned_integer T>
[[nodiscard]] constexpr div_result<T> narrowing_div(const wide<T> x, const T y) noexcept {
    const bool quotient_fits = x.high_bits < y;
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        BEMAN_BIG_INT_ASSERT(quotient_fits);
    }

#if BEMAN_BIG_INT_LIMB_WIDTH == 64
    // For 64-bit, there potentially exists the optimization opportunity of using a `div` instruction.
    // This is only available on x86_64, and traps if the quotient does not fit into 64 bits.
    if constexpr (width_v<T> == 64) {
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            BEMAN_BIG_INT_DEBUG_ASSERT(quotient_fits);
    #if defined(BEMAN_BIG_INT_GNUC) && (defined(__x86_64__) || defined(__i386__))
            T q, r;
            __asm__("div %[d]" : "=a"(q), "=d"(r) : "a"(x.low_bits), "d"(x.high_bits), [d] "r"(y) : "cc");
            return {.quotient = q, .remainder = r};
    #elif defined(_WIN32)
            T r;
            T q = _udiv128(static_cast<T>(x.high_bits), static_cast<T>(x.low_bits), static_cast<T>(y), &r);
            return {.quotient = static_cast<T>(q), .remainder = static_cast<T>(r)};
    #endif
        }
    }
#endif // BEMAN_BIG_INT_LIMB_WIDTH == 64

    // In the general case, we rely on `wider_t<T>` and `to_int()` existing.
    // There is no software fallback, so this might fail due to lack of 128-bit support
    // if the function is instantiated with a 64-bit type.
    const auto x_int = x.to_int();
    return {
        .quotient  = static_cast<T>(x_int / y),
        .remainder = static_cast<T>(x_int % y),
    };
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_WIDE_OPS_HPP
