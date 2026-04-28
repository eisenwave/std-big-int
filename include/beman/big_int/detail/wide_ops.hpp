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

template <class T>
struct wider;

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT
template <>
struct wider<int_multiprecision_t> {
    using type = int_wide_t;
};
template <>
struct wider<uint_multiprecision_t> {
    using type = uint_wide_t;
};
#endif

#ifdef BEMAN_BIG_INT_HAS_BITINT
template <std::size_t N>
struct wider<_BitInt(N)> {
    using type = _BitInt(2 * N);
};
template <std::size_t N>
struct wider<unsigned _BitInt(N)> {
    using type = unsigned _BitInt(2 * N);
};
#endif

template <signed_integer T>
struct wider<T> {
    [[nodiscard]] static consteval auto select() {
        if constexpr (width_v<T> == 8) {
            return std::int16_t{};
        } else if constexpr (width_v<T> == 16) {
            return std::int32_t{};
        } else if constexpr (width_v<T> == 32) {
            return std::int64_t{};
        }
#ifdef BEMAN_BIG_INT_HAS_INT128
        else if constexpr (width_v<T> == 64) {
            return int128_t{};
        }
#endif
        else {
            BEMAN_BIG_INT_STATIC_ASSERT_FALSE("No wider signed integer type available.");
        }
    }
    using type = decltype(select());
};
template <unsigned_integer T>
struct wider<T> {
    [[nodiscard]] static consteval auto select() {
        if constexpr (width_v<T> == 8) {
            return std::uint16_t{};
        } else if constexpr (width_v<T> == 16) {
            return std::uint32_t{};
        } else if constexpr (width_v<T> == 32) {
            return std::uint64_t{};
        }
#ifdef BEMAN_BIG_INT_HAS_INT128
        else if constexpr (width_v<T> == 64) {
            return uint128_t{};
        }
#endif
        else {
            BEMAN_BIG_INT_STATIC_ASSERT_FALSE("No wider unsigned integer type available.");
        }
    }
    using type = decltype(select());
};

// Denotes the integer type with twice the width of `T`
// and with the same signedness.
template <signed_or_unsigned T>
using wider_t = typename wider<T>::type;

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
#if BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
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
#endif // BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
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

#ifdef BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
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
#endif // BEMAN_BIG_INT_HAS_INT128_FUNDAMENTAL
}

// The cast is needed for less than 16-bit integers,
// since they get implicitly promoted to int which sets off -Wimplicit-int-conversion
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wuseless-cast")

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
    return static_cast<T>((x.high_bits << s) | (x.low_bits >> (width_v<T> - s)));
#endif
}

BEMAN_BIG_INT_DIAGNOSTIC_POP()

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
    if constexpr (std::is_unsigned_v<T>) {
        return {.value = x + y, .overflow = x + y < x};
    } else {
        const auto wide  = static_cast<wider_t<T>>(x) + static_cast<wider_t<T>>(y);
        const auto value = static_cast<T>(wide);
        return {.value = value, .overflow = wide != value};
    }
#endif // __builtin_add_overflow
}

template <signed_or_unsigned T>
[[nodiscard]] constexpr overflow_result<T> overflowing_sub(const T x, const T y) noexcept {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_sub_overflow)
    T    value;
    bool overflow = __builtin_sub_overflow(x, y, &value);
    return {.value = value, .overflow = overflow};
#else
    if constexpr (std::is_unsigned_v<T>) {
        return {.value = x - y, .overflow = x < y};
    } else {
        const auto wide  = static_cast<wider_t<T>>(x) - static_cast<wider_t<T>>(y);
        const auto value = static_cast<T>(wide);
        return {.value = value, .overflow = wide != value};
    }
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
[[nodiscard]] constexpr carry_result<T>
carrying_add_portable(const T x, const T y, const bool carry = false) noexcept {
    const auto [sum_x_y, carry_x_y] = overflowing_add(x, y);
    const auto [sum_out, carry_out] = overflowing_add(sum_x_y, static_cast<T>(carry));
    return {.value = sum_out, .carry = carry_x_y || carry_out};
}

template <unsigned_integer T>
[[nodiscard]] constexpr carry_result<T> carrying_add(const T x, const T y, const bool carry = false) noexcept {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_addc)
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_addc)
    if constexpr (true)
    #else
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL
    #endif
    {
        if constexpr (width_v<T> == width_v<unsigned>) {
            unsigned carry_out;
            unsigned value = __builtin_addc(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long>) {
            unsigned long carry_out;
            unsigned long value = __builtin_addcl(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long long>) {
            unsigned long long carry_out;
            unsigned long long value = __builtin_addcll(x, y, carry, &carry_out);
            return {.value = value, .carry = carry_out != 0};
        } else {
            return carrying_add_portable(x, y, carry);
        }
    } else {
        return carrying_add_portable(x, y, carry);
    }
#elif defined(BEMAN_BIG_INT_MSVC) && (defined(_M_AMD64) || defined(_M_IX86))
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
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
        else {
            return carrying_add_portable(x, y, carry);
        }
    } else {
        return carrying_add_portable(x, y, carry);
    }
#else
    return carrying_add_portable(x, y, carry);
#endif
}

template <class T>
struct borrow_result {
    T    value;
    bool borrow;
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

template <unsigned_integer T>
[[nodiscard]] constexpr borrow_result<T>
borrowing_sub_portable(const T x, const T y, const bool borrow = false) noexcept {
    const auto [diff_x_y, borrow_x_y] = overflowing_sub(x, y);
    const auto [diff_out, borrow_out] = overflowing_sub(diff_x_y, static_cast<T>(borrow));
    return {.value = diff_out, .borrow = borrow_x_y || borrow_out};
}

template <unsigned_integer T>
[[nodiscard]] constexpr borrow_result<T> borrowing_sub(const T x, const T y, const bool borrow = false) noexcept {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_subc)
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_subc)
    if constexpr (true)
    #else
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL
    #endif
    {
        if constexpr (width_v<T> == width_v<unsigned>) {
            unsigned borrow_out;
            unsigned value = __builtin_subc(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long>) {
            unsigned long borrow_out;
            unsigned long value = __builtin_subcl(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        } else if constexpr (width_v<T> == width_v<unsigned long long>) {
            unsigned long long borrow_out;
            unsigned long long value = __builtin_subcll(x, y, borrow, &borrow_out);
            return {.value = value, .borrow = borrow_out != 0};
        } else {
            return borrowing_sub_portable(x, y, borrow);
        }
    } else {
        return borrowing_sub_portable(x, y, borrow);
    }
#elif defined(BEMAN_BIG_INT_MSVC) && (defined(_M_AMD64) || defined(_M_IX86))
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
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
    #endif // _M_AMD64
        else {
            return carrying_add_portable(x, y, carry);
        }
    } else {
        return carrying_add_portable(x, y, carry);
    }
#else
    return borrowing_sub_portable(x, y, borrow);
#endif
}

template <signed_or_unsigned T>
struct wide_div_result {
    T       quotient;
    wide<T> remainder;
};

// Returns the quotient and remainder of the division `x / y`.
// The behavior is undefined if the quotient is not representable as `T`,
// which is the case if and only if `x.high_bits < y`.
template <unsigned_integer T>
[[nodiscard]] constexpr div_result<T> narrowing_div(const wide<T> x, const T y) noexcept {
    const bool quotient_fits = x.high_bits < y;
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        BEMAN_BIG_INT_ASSERT(quotient_fits);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(quotient_fits);
    }

#if BEMAN_BIG_INT_LIMB_WIDTH == 64
    // For 64-bit, there potentially exists the optimization opportunity of using a `div` instruction.
    // This is only available on x86_64, and traps if the quotient does not fit into 64 bits.
    //
    // We also cannot use this during constant evaluation,
    // and if the divisor is known to the optimizer,
    // relying on full 128-bit division is better because it can be transformed into fixed-point multiplication.
    // Inline assembly does not get optimized.
    // See https://quick-bench.com/q/o-0RD_0_bnc5tQqJ4o21wdeeuco
    if constexpr (width_v<T> == 64) {
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if (!BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(y)) {
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

// Portable bit-by-bit restoring long division of the 2-limb unsigned value `a`
// by the 2-limb unsigned value `b`.
template <unsigned_integer T>
[[nodiscard]] constexpr wide_div_result<T> divide_wide_by_wide_portable(const wide<T> a, const wide<T> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(b.high_bits != 0);

    constexpr std::size_t limb_bits = width_v<T>;

    T q    = 0;
    T r_lo = 0;
    T r_hi = 0;

    for (std::size_t i = 2 * limb_bits; i-- > 0;) {
        const T r_top = static_cast<T>(r_hi >> (limb_bits - 1));
        r_hi          = funnel_shl(wide<T>{.low_bits = r_lo, .high_bits = r_hi}, 1u);
        const T bit   = (i >= limb_bits) ? static_cast<T>((a.high_bits >> (i - limb_bits)) & T{1})
                                         : static_cast<T>((a.low_bits >> i) & T{1});
        r_lo          = static_cast<T>((r_lo << 1) | bit);
        q <<= 1;

        // If the virtual 3-limb remainder (r_top, r_hi, r_lo) is >= b, subtract.
        const bool r_ge_b = (r_top != 0) || (r_hi > b.high_bits) || (r_hi == b.high_bits && r_lo >= b.low_bits);
        if (r_ge_b) {
            const auto [diff_lo, borrow_lo] = borrowing_sub(r_lo, b.low_bits);
            r_lo                            = diff_lo;
            r_hi                            = borrowing_sub(r_hi, b.high_bits, borrow_lo).value;
            q |= T{1};
        }
    }
    return {
        .quotient  = q,
        .remainder = {.low_bits = r_lo, .high_bits = r_hi},
    };
}

// Returns the quotient and remainder of dividing the 2-limb unsigned value `a`
// by the 2-limb unsigned value `b`.
// Precondition: `b.high_bits != 0`, which guarantees the quotient fits in a single limb of `T`.
template <unsigned_integer T>
[[nodiscard]] constexpr wide_div_result<T> divide_wide_by_wide(const wide<T> a, const wide<T> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(b.high_bits != 0);

    if constexpr (requires { typename wider_t<T>; }) {
        const auto a_int = a.to_int();
        const auto b_int = b.to_int();
        return {
            .quotient  = static_cast<T>(a_int / b_int),
            .remainder = wide<T>::from_int(a_int % b_int),
        };
    } else {
        return divide_wide_by_wide_portable(a, b);
    }
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_WIDE_OPS_HPP
