// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_CONFIG_HPP
#define BEMAN_BIG_INT_CONFIG_HPP

// Compiler identification =====================================================

#if defined(_MSC_VER)
    #define BEMAN_BIG_INT_MSVC _MSC_VER
#elif defined(__clang__)
    #define BEMAN_BIG_INT_CLANG __clang__
#elif defined(__GNUC__)
    #define BEMAN_BIG_INT_GCC __GNUC__
#else
    #error "Unknown compiler (none of MSVC, Clang, GCC)."
#endif

#ifdef __GNUC__
    // Separate case for any GNU-C-compliant compilers,
    // which is both GCC and Clang.
    #define BEMAN_BIG_INT_GNUC __GNUC__
    #define BEMAN_BIG_INT_HAS_BUILTIN(...) __has_builtin(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_HAS_BUILTIN(...) 0
#endif // __GNUC__

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
#else
    // Prevent warnings for use of undefined macros.
    #define BEMAN_BIG_INT_BITINT_MAXWIDTH 0
#endif // BITINT_MAXWIDTH

// 128-bit integer support =====================================================

#ifdef BEMAN_BIG_INT_MSVC
    #include <__msvc_int128.hpp>
#endif // BEMAN_BIG_INT_MSVC

namespace beman::big_int::detail {

#if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 128
using int128_t  = _BitInt(128);
using uint128_t = unsigned _BitInt(128);
#elif defined(BEMAN_BIG_INT_GNUC)
using int128_t  = __int128;
using uint128_t = unsigned __int128;
#elif defined(BEMAN_BIG_INT_MSVC)
using int128_t  = std::_Signed128;
using uint128_t = std::_Unsigned128;
#else
    #error "128-bit integer support is required."
#endif

} // namespace beman::big_int::detail

// Limb type selection =========================================================

namespace beman::big_int {

// We currently assume 64-bit,
// so we just hardcode using unsigned long long.

using uint_multiprecision_t = unsigned long long;

namespace detail {

// Signed counterpart to uint_multiprecision_t.
using int_multiprecision_t = long long;
// Unsigned integer type with twice the width of uint_multiprecision_t.
using uint_wide_t = uint128_t;
// Signed integer type with twice the width of int_multiprecision_t.
using int_wide_t = int128_t;

} // namespace detail

} // namespace beman::big_int

// Concepts ====================================================================

#include <cstdint>
#include <type_traits>
#include <concepts>

namespace beman::big_int::detail {

template <class T>
concept cv_unqualified = !std::is_const_v<T> && !std::is_volatile_v<T>;

template <class T>
concept character_type =                                     //
    std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || //
    std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

// Modeled if `T` is a signed or unsigned integer type.
// That is, a standard integer type, extended integer type, or bit-precise integer type.
template <class T>
concept signed_or_unsigned = std::is_integral_v<T> && cv_unqualified<T> //
                             && !std::is_same_v<T, bool> && !character_type<T>;

// Modeled if `T` is a standard unsigned, extended unsigned, or bit-precise unsigned integer type.
template <class T>
concept unsigned_integer = signed_or_unsigned<T> && std::is_unsigned_v<T>;
// Modeled if `T` is a standard signed, extended signed, or bit-precise signed integer type.
template <class T>
concept signed_integer = signed_or_unsigned<T> && std::is_signed_v<T>;

// Alias template that maps a cv-unqualified integral type onto the underlying
// signed or unsigned integer type.
// For example, this converts `char8_t` to `unsigned char`, `int` to `int`, etc.
// The goal is to reduce redundant template instantiations.
template <class T>
    requires std::integral<T> && cv_unqualified<T>
using make_signed_or_unsigned_t =
    std::conditional_t<std::is_signed_v<T>, std::make_signed_t<T>, std::make_unsigned_t<T>>;

template <class T>
concept cv_unqualified_floating_point = cv_unqualified<T> && std::floating_point<T>;

template <class T>
[[nodiscard, maybe_unused]] constexpr make_signed_or_unsigned_t<T> to_signed_or_unsigned(const T x) {
    return static_cast<make_signed_or_unsigned_t<T>>(x);
}

} // namespace beman::big_int::detail

// Traits =======================================================================

#if __has_include(<stdfloat>)
    #include <stdfloat>
#endif

namespace beman::big_int::detail {
template <class F>
struct ieee_traits;

template <>
struct ieee_traits<float> {
    using bits_type                        = std::uint32_t;
    static constexpr int  mantissa_bits    = 23;
    static constexpr int  exponent_bits    = 8;
    static constexpr int  bias             = 127;
    static constexpr bool explicit_int_bit = false;
};

template <>
struct ieee_traits<double> {
    using bits_type                        = std::uint64_t;
    static constexpr int  mantissa_bits    = 52;
    static constexpr int  exponent_bits    = 11;
    static constexpr int  bias             = 1023;
    static constexpr bool explicit_int_bit = false;
};

#ifdef __STDCPP_FLOAT16_T__
template <>
struct ieee_traits<std::float16_t> {
    using bits_type                        = std::uint16_t;
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
    static constexpr int  mantissa_bits    = 112;
    static constexpr int  exponent_bits    = 15;
    static constexpr int  bias             = 16383;
    static constexpr bool explicit_int_bit = false;
};
#endif

#if __LDBL_MANT_DIG__ == 64 && __LDBL_MAX_EXP__ == 16384

    // These are 80-bits so no real way around it being padded
    #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wpadded"
    #endif

struct long_double_bits {
    std::uint64_t mantissa : 64;
    std::uint16_t exponent : 15;
    std::uint16_t sign : 1;
};

    #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC diagnostic pop
    #endif

template <>
struct ieee_traits<long double> {
    using bits_type                        = long_double_bits;
    static constexpr int  mantissa_bits    = 63;
    static constexpr int  exponent_bits    = 15;
    static constexpr int  bias             = 16383;
    static constexpr bool explicit_int_bit = true;
};
#elif __LDBL_MANT_DIG__ == 113 && __LDBL_MAX_EXP__ == 16384
template <>
struct ieee_traits<long double> {
    using bits_type                        = uint128_t;
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
} // namespace beman::big_int::detail

// Trivial ABI =================================================================

#if defined(BEMAN_BIG_INT_CLANG)
    #define BEMAN_BIG_INT_TRIVIAL_ABI [[clang::trivial_abi]]
#else
    #define BEMAN_BIG_INT_TRIVIAL_ABI
#endif

// no_unique_address ===========================================================

#ifdef BEMAN_BIG_INT_MSVC
    #define BEMAN_BIG_INT_NO_UNIQUE_ADDRESS [[no_unique_address, msvc::no_unique_address]]
#else
    #define BEMAN_BIG_INT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

// assert ======================================================================

#include <cstdlib>
#include <cassert>

namespace beman::big_int {

[[noreturn]] inline void assert_fail() {
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_trap)
    __builtin_trap();
#else
    std::abort();
#endif
}

} // namespace beman::big_int

#define BEMAN_BIG_INT_ASSERT(...) (__VA_ARGS__ ? void() : assert_fail())

#ifndef NDEBUG
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) BEMAN_BIG_INT_ASSERT(__VA_ARGS__)
#else
  // The requires expression makes sure that we still check for expression validity,
  // even if the expression is not evaluated.
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) void(requires { __VA_ARGS__; })
#endif

// =============================================================================

#endif // BEMAN_BIG_INT_CONFIG_HPP
