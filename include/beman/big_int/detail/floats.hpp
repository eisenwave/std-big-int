// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_FLOATS_HPP
#define BEMAN_BIG_INT_FLOATS_HPP

#include <bit>
#include <type_traits>
#include <cmath>
#include <cfloat>
#include <limits>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

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
    std::uint64_t mantissa;
    std::uint16_t sign_and_exponent;
};

#if !defined(LDBL_MANT_DIG) || !defined(LDBL_MAX_EXP)
    #error Cannot define ieee_traits<long double> without LDBL_MANT_DIG and LDBL_MAX_EXP.
#endif

#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
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
#elif LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384
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
#elif LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
template <>
struct ieee_traits<long double> : ieee_traits<double> {};
#else
    #define BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
#endif

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr bool constexpr_signbit(const F x) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::signbit(x);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_signbit)
    return static_cast<bool>(__builtin_signbit(x));
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        using bits_t      = typename ieee_traits<F>::bits_type;
        const bits_t bits = std::bit_cast<bits_t>(x);
        if constexpr (std::is_same_v<bits_t, detail::x87_extended_float_bits>) {
            return ((bits.sign_and_exponent >> 15) & 1) != 0;
        } else {
            // This implementation assumes that the sign bit is always the most significant bit.
            return ((bits >> (ieee_traits<F>::width - 1)) & 1) != 0;
        }
    } else {
        return std::signbit(x);
    }
#endif
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr bool constexpr_isfinite(const F x) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::isfinite(x);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_isfinite)
    return static_cast<bool>(__builtin_isfinite(x));
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
        BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfloat-equal")
        BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wfloat-equal")
        return x == x && x != std::numeric_limits<F>::infinity() && x != -std::numeric_limits<F>::infinity();
        BEMAN_BIG_INT_DIAGNOSTIC_POP()
    } else {
        return std::isfinite(x);
    }
#endif
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr F constexpr_copysign(const F x, const F s) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::copysign(x, s);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_copysign)
    if constexpr (std::is_same_v<decltype(__builtin_copysign(x, s)), F>) {
        // The builtin is either generic or F is double.
        return __builtin_copysign(x, s);
    } else {
        if BEMAN_BIG_INT_IS_CONSTEVAL {
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_copysignf)
            if constexpr (std::is_same_v<F, float>) {
                return __builtin_copysignf(x, s);
            }
    #endif
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_copysignl)
            if constexpr (std::is_same_v<F, long double>) {
                return __builtin_copysignl(x, s);
            }
    #endif
            return constexpr_signbit(x) == constexpr_signbit(s) ? x : -x;
        } else {
            return std::copysign(x, s);
        }
    }
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        return constexpr_signbit(x) == constexpr_signbit(s) ? x : -x;
    } else {
        return std::copysign(x, s);
    }
#endif
}

// Not actually marked consteval because this is problematic if `if consteval`
// is not supported yet and calls from an immediate context are needed.
template <cv_unqualified_floating_point F>
[[nodiscard, maybe_unused]] constexpr F consteval_ldexp(F x, int exp) noexcept {
    while (exp > 0) {
        const int part = exp > 63 ? 63 : exp;
        x *= static_cast<F>(1ull << part);
        exp -= part;
    }
    while (exp < 0) {
        const int part = exp < -63 ? -63 : exp;
        x /= static_cast<F>(1ull << part);
        exp += part;
    }
    return x;
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr F constexpr_ldexp(const F x, const int exp) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::ldexp(x, exp);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_ldexp)
    if constexpr (std::is_same_v<decltype(__builtin_ldexp(x, exp)), F>) {
        // The builtin is either generic or F is double.
        return __builtin_ldexp(x, exp);
    } else {
        if BEMAN_BIG_INT_IS_CONSTEVAL {
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_ldexpf)
            if constexpr (std::is_same_v<F, float>) {
                return __builtin_ldexpf(x, exp);
            }
    #endif
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_ldexpl)
            if constexpr (std::is_same_v<F, long double>) {
                return __builtin_ldexpl(x, exp);
            }
    #endif
            return consteval_ldexp(x, exp);
        } else {
            return std::ldexp(x, exp);
        }
    }
#else
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        return consteval_ldexp(x, exp);
    } else {
        return std::ldexp(x, exp);
    }
#endif
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr F constexpr_fabs(const F x) noexcept {
#if defined(__cpp_lib_constexpr_cmath) && __cpp_lib_constexpr_cmath >= 202202L
    return std::fabs(x);
#elif BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_fabs)
    if constexpr (std::is_same_v<decltype(__builtin_fabs(x)), F>) {
        // The builtin is either generic or F is double.
        return __builtin_fabs(x);
    } else {
        if BEMAN_BIG_INT_IS_CONSTEVAL {
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_fabsf)
            if constexpr (std::is_same_v<F, float>) {
                return __builtin_fabsf(x);
            }
    #endif
    #if BEMAN_BIG_INT_HAS_CONSTEXPR_BUILTIN_OR_BUILTIN(__builtin_fabsl)
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
template <class F>
struct float_representation {
    using exponent_type = int;
    using mantissa_type = typename ieee_traits<F>::mantissa_type;

    // The sign bit, where `true` indicates a negative number.
    bool sign;
    // The unbiased exponent.
    exponent_type exponent;
    // The significand or "mantissa" of the floating-point number.
    mantissa_type mantissa;

    friend bool operator==(const float_representation&, const float_representation&) = default;
};

// Decomposes a finite floating-point value into a `float_representation` object.
template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr float_representation<F> decompose_float(const F value) {
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
        constexpr std::uint16_t exponent_mask = ((std::uint16_t{1} << traits::exponent_bits) - 1);
        bits                                  = std::bit_cast<bits_t>(value);
        sign          = static_cast<bool>((bits.sign_and_exponent >> traits::exponent_bits) & 1);
        ieee_exp      = static_cast<std::uint32_t>(bits.sign_and_exponent & exponent_mask);
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

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr F compose_float(const float_representation<F> representation) {
    static_assert(std::numeric_limits<F>::radix == 2, "Only binary floating-point is supported.");

    const F magnitude = constexpr_ldexp(static_cast<F>(representation.mantissa), representation.exponent);
    return constexpr_copysign(magnitude, representation.sign ? F{-1} : F{1});
}

template <cv_unqualified_floating_point F>
[[nodiscard]] constexpr F compose_float(const std::span<const uint_multiprecision_t> limbs, const bool sign) {
    static_assert(std::numeric_limits<F>::radix == 2, "Only binary floating-point is supported.");
    static_assert(std::numeric_limits<F>::round_style == std::float_round_style::round_to_nearest,
                  "Only round-to-nearest is currently supported.");

    using traits     = ieee_traits<F>;
    using mantissa_t = typename traits::mantissa_type;

    constexpr std::size_t limb_width     = width_v<uint_multiprecision_t>;
    constexpr std::size_t precision_bits = traits::mantissa_bits + 1;
    constexpr std::size_t mantissa_width = width_v<mantissa_t>;

    const F sign_value = sign ? F{-1} : F{1};

    std::size_t limb_count = limbs.size();
    while (limb_count > 0 && limbs[limb_count - 1] == 0) {
        --limb_count;
    }
    if (limb_count == 0) {
        return constexpr_copysign(F{0}, sign_value);
    }

    const std::size_t top_limb_bits = limb_width - static_cast<std::size_t>(std::countl_zero(limbs[limb_count - 1]));
    const std::size_t total_bits    = (limb_count - 1) * limb_width + top_limb_bits;

    // Up-front overflow check: any integer with more bits than `max_exponent` has
    // magnitude `>= pow(2, max_exponent)` and therefore exceeds every finite value of `F`.
    if (total_bits > static_cast<std::size_t>(std::numeric_limits<F>::max_exponent)) {
        return constexpr_copysign(std::numeric_limits<F>::infinity(), sign_value);
    }

    const auto limb_at = [&](const std::size_t i) -> uint_multiprecision_t { //
        return i < limb_count ? limbs[i] : 0;
    };

    mantissa_t mantissa         = 0;
    int        exponent         = 0;
    bool       round_bit        = false;
    bool       found_sticky_bit = false;

    if (total_bits <= precision_bits) {
        // The whole integer fits inside the mantissa; concat limbs into `mantissa`.
        for (std::size_t i = 0; i < limb_count; ++i) {
            mantissa |= static_cast<mantissa_t>(limbs[i]) << (i * limb_width);
        }
    } else {
        // We need to extract bits `[shift, shift + precision_bits)` of the integer as the
        // mantissa, with bit `shift - 1` as the round bit and a sticky-OR over all lower bits.
        const auto shift = total_bits - precision_bits;
        {
            const auto limb_shift = shift / limb_width;
            const auto bit_shift  = static_cast<unsigned>(shift % limb_width);

            // Extract `precision_bits` bits via funnel-shifts across pairs of limbs.
            constexpr int mantissa_limbs = (precision_bits + limb_width - 1) / limb_width;
            for (std::size_t j = 0; j < mantissa_limbs; ++j) {
                const std::size_t           li = limb_shift + static_cast<std::size_t>(j);
                const uint_multiprecision_t piece =
                    funnel_shr(wide{.low_bits = limb_at(li), .high_bits = limb_at(li + 1)}, bit_shift);
                mantissa |= static_cast<mantissa_t>(piece) << (j * limb_width);
            }
        }
        // Trim any bits we pulled in beyond the mantissa width (only needed when
        // `precision_bits` is not a multiple of `limb_bits`).
        if constexpr (precision_bits < mantissa_width) {
            mantissa &= (mantissa_t{1} << precision_bits) - 1;
        }

        // Round bit lives at bit index `shift - 1`.
        const std::size_t rb_index       = shift - 1;
        const auto        rb_limb_index  = rb_index / limb_width;
        const unsigned    rb_limb_offset = static_cast<unsigned>(rb_index % limb_width);
        round_bit                        = ((limbs[rb_limb_index] >> rb_limb_offset) & uint_multiprecision_t{1}) != 0;

        // Sticky bit: OR over everything strictly below the round bit.
        for (std::size_t i = 0; i < rb_limb_index && !found_sticky_bit; ++i) {
            found_sticky_bit = limbs[i] != 0;
        }
        if (!found_sticky_bit && rb_limb_offset > 0) {
            const uint_multiprecision_t low_mask = (uint_multiprecision_t{1} << rb_limb_offset) - 1;
            found_sticky_bit                     = (limbs[rb_limb_index] & low_mask) != 0;
        }

        exponent = static_cast<int>(shift);
    }

    if (round_bit && (found_sticky_bit || ((mantissa & mantissa_t{1}) != 0))) {
        // The pre-rounding mantissa has its high bit at position `precision_bits - 1`.
        // A carry-out past that position requires shifting back by one and bumping the
        // exponent. When `mantissa_t` has headroom above the mantissa, that carry shows
        // up as a bit at position `precision_bits`; otherwise, it shows up as the
        // type-level carry from `carrying_add`.
        if constexpr (precision_bits < mantissa_width) {
            mantissa += 1;
            if ((mantissa >> precision_bits) != 0) {
                mantissa >>= 1;
                ++exponent;
            }
        } else {
            const auto [sum, carry] = overflowing_add(mantissa, mantissa_t{1});
            mantissa                = sum;
            if (carry) {
                mantissa = mantissa_t{1} << (precision_bits - 1);
                ++exponent;
            }
        }
    }

    // Post-rounding overflow check: rounding can bump `exponent` by one, which may push
    // a value that was just below `pow(2, max_exponent)` over the edge to infinity.
    if (exponent + static_cast<int>(precision_bits) > std::numeric_limits<F>::max_exponent) {
        return constexpr_copysign(std::numeric_limits<F>::infinity(), sign_value);
    }

    return compose_float(float_representation<F>{
        .sign     = sign,
        .exponent = exponent,
        .mantissa = mantissa,
    });
}

} // namespace beman::big_int::detail

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_FLOATS_HPP
