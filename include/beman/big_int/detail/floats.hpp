// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_FLOATS_HPP
#define BEMAN_BIG_INT_FLOATS_HPP

#include <bit>
#include <type_traits>
#include <cmath>
#include <limits>
#include <cstdint>

#include <beman/big_int/detail/config.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

#if __has_include(<stdfloat>)
    #include <stdfloat>
#endif

namespace beman::big_int::detail {

// Provides information about floating-point types.
// Each specialization has the following members:
// `bits_type`        - A trivially copyable type assisting in decomposing a value.
// `mantissa_type`    - An unsigned integer type sufficiently wide to represent all mantissa bits,
//                      including the leading implicit `1`.
// `mantissa_bits`    - The number of fractional bits in the mantissa.
//                      That is, not counting the explicit integer bit in x87 extended precision.
// `exponent_bits`    - The number of exponent bits.
// `bias`             - The exponent bias, which is an amount subtracted from the raw exponent
//                      to obtain the mathematical exponent.
// `explicit_int_bit` - If `true`, there exists an explicit integer bit.
//                      This is the case for x87 extended precision floats.
template <class F>
struct ieee_traits;

template <>
struct ieee_traits<float> {
    static_assert(std::numeric_limits<float>::is_iec559, "Sorry, we don't support exotic float.");
    using bits_type                        = std::uint32_t;
    using mantissa_type                    = std::uint32_t;
    static constexpr int  width            = 32;
    static constexpr int  mantissa_bits    = 23;
    static constexpr int  exponent_bits    = 8;
    static constexpr int  bias             = 127;
    static constexpr bool explicit_int_bit = false;
};

template <>
struct ieee_traits<double> {
    static_assert(std::numeric_limits<double>::is_iec559, "Sorry, we don't support exotic double.");
    using bits_type                        = std::uint64_t;
    using mantissa_type                    = std::uint64_t;
    static constexpr int  width            = 64;
    static constexpr int  mantissa_bits    = 52;
    static constexpr int  exponent_bits    = 11;
    static constexpr int  bias             = 1023;
    static constexpr bool explicit_int_bit = false;
};

#ifdef __STDCPP_FLOAT16_T__
template <>
struct ieee_traits<std::float16_t> {
    using bits_type                        = std::uint16_t;
    using mantissa_type                    = std::uint16_t;
    static constexpr int  width            = 16;
    static constexpr int  mantissa_bits    = 10;
    static constexpr int  exponent_bits    = 5;
    static constexpr int  bias             = 15;
    static constexpr bool explicit_int_bit = false;
};
#endif

#ifdef __STDCPP_BFLOAT16_T__
template <>
struct ieee_traits<std::bfloat16_t> {
    using bits_type                        = std::uint16_t;
    using mantissa_type                    = std::uint16_t;
    static constexpr int  width            = 16;
    static constexpr int  mantissa_bits    = 7;
    static constexpr int  exponent_bits    = 8;
    static constexpr int  bias             = 127;
    static constexpr bool explicit_int_bit = false;
};
#endif

#ifdef __STDCPP_FLOAT128_T__
template <>
struct ieee_traits<std::float128_t> {
    using bits_type                        = uint128_t;
    using mantissa_type                    = uint128_t;
    static constexpr int  width            = 128;
    static constexpr int  mantissa_bits    = 112;
    static constexpr int  exponent_bits    = 15;
    static constexpr int  bias             = 16383;
    static constexpr bool explicit_int_bit = false;
};
#endif

struct x87_extended_float_bits {
    std::uint64_t mantissa : 64;
    std::uint16_t exponent : 15;
    std::uint16_t sign : 1;
};

#if __LDBL_MANT_DIG__ == 64 && __LDBL_MAX_EXP__ == 16384

template <>
struct ieee_traits<long double> {
    using bits_type                        = x87_extended_float_bits;
    using mantissa_type                    = std::uint64_t;
    static constexpr int  width            = 80;
    static constexpr int  mantissa_bits    = 63;
    static constexpr int  exponent_bits    = 15;
    static constexpr int  bias             = 16383;
    static constexpr bool explicit_int_bit = true;
};
#elif __LDBL_MANT_DIG__ == 113 && __LDBL_MAX_EXP__ == 16384
template <>
struct ieee_traits<long double> {
    using bits_type                        = uint128_t;
    using mantissa_type                    = uint128_t;
    static constexpr int  width            = 128;
    static constexpr int  mantissa_bits    = 112;
    static constexpr int  exponent_bits    = 15;
    static constexpr int  bias             = 16383;
    static constexpr bool explicit_int_bit = false;
};
#elif __LDBL_MANT_DIG__ == 53 && __LDBL_MAX_EXP__ == 1024
template <>
struct ieee_traits<long double> : ieee_traits<double> {};
#else
    #define BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
#endif

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr bool constexpr_signbit(const F x) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::signbit(x);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_signbit)
    return static_cast<bool>(__builtin_signbit(x));
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        // Sorry, not implemented yet.
        BEMAN_BIG_INT_ASSERT(false);
    } else {
        return std::signbit(x);
    }
#endif
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr bool constexpr_isfinite(const F x) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::isfinite(x);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_isfinite)
    return static_cast<bool>(__builtin_isfinite(x));
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
        BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfloat-equal")
        return x == x && x != std::numeric_limits<F>::infinity() && x != -std::numeric_limits<F>::infinity();
        BEMAN_BIG_INT_DIAGNOSTIC_POP()
    } else {
        return std::isfinite(x);
    }
#endif
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr F constexpr_fabs(const F x) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::fabs(x);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_fabs)
    if constexpr (std::is_same_v<decltype(__builtin_fabs(x)), F>) {
        // The builtin is either generic or F is double.
        return __builtin_fabs(x);
    } else {
        if BEMAN_BIG_INT_IS_CONSTEVAL {
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_fabsf)
            if constexpr (std::is_same_v<F, float>) {
                return __builtin_fabsf(x);
            }
    #endif
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN(__builtin_fabsl)
            if constexpr (std::is_same_v<F, long double>) {
                return __builtin_fabsl(x);
            }
    #endif
            return constexpr_signbit(x) ? -x : x;
        } else {
            return std::fabs(x);
        }
    }
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        return constexpr_signbit(x) ? -x : x;
    } else {
        return std::fabs(x);
    }
#endif
}

// A triple that represents a finite floating-point number.
// The represented value is `(sign ? -1 : 1) * mantissa * pow(b, exponent)`,
// where `b` is the radix (usually two) of the floating-point number.
template <class T>
struct float_representation {
    // The sign bit, where `true` indicates a negative number.
    bool sign;
    // The unbiased exponent.
    int exponent;
    // The significand or "mantissa" of the floating-point number.
    T mantissa;

    friend bool operator==(const float_representation&, const float_representation&) = default;
};

// Decomposes a finite floating-point value into a `float_representation` object.
template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr float_representation<typename ieee_traits<F>::mantissa_type> decompose_float(const F value) {
    static_assert(std::numeric_limits<F>::radix == 2, "Only binary floating-point is supported.");
    using traits     = detail::ieee_traits<F>;
    using bits_t     = typename traits::bits_type;
    using mantissa_t = typename traits::mantissa_type;

    BEMAN_BIG_INT_DEBUG_ASSERT(constexpr_isfinite(value));

    constexpr int mb   = traits::mantissa_bits;
    constexpr int bias = traits::bias;

    bits_t        bits;
    bool          sign;
    std::uint32_t ieee_exp;
    mantissa_t    ieee_mantissa;

    if constexpr (std::is_same_v<bits_t, detail::x87_extended_float_bits>) {
        // UB on x86 due to 6 padding bytes.
        if BEMAN_BIG_INT_IS_CONSTEVAL {
            BEMAN_BIG_INT_ASSERT(false);
        }
        __builtin_memcpy(&bits, &value, sizeof(bits));
        sign          = bits.sign;
        ieee_exp      = static_cast<std::uint32_t>(bits.exponent);
        ieee_mantissa = bits.mantissa;
    } else {
        constexpr mantissa_t mantissa_mask = (mantissa_t{1} << mb) - 1;

        bits          = std::bit_cast<bits_t>(value);
        sign          = static_cast<bool>((bits >> (mb + traits::exponent_bits)) & 1);
        ieee_exp      = static_cast<std::uint32_t>((bits >> mb) & ((bits_t{1} << traits::exponent_bits) - 1));
        ieee_mantissa = static_cast<mantissa_t>(bits & mantissa_mask);
    }

    if (ieee_exp == 0) {
        return {
            .sign     = sign,
            .exponent = 1 - bias - mb,
            .mantissa = ieee_mantissa,
        };
    }

    if constexpr (!traits::explicit_int_bit) {
        ieee_mantissa |= typename traits::mantissa_type{1} << mb;
    }
    return {
        .sign     = sign,
        .exponent = static_cast<int>(ieee_exp) - bias - mb,
        .mantissa = ieee_mantissa,
    };
}

} // namespace beman::big_int::detail

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_FLOATS_HPP
