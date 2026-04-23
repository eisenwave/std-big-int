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

// Undefine min()/max() for MSVC ===============================================
#ifdef min
    #error min is defined as a macro. Define NOMINMAX.
#endif
#ifdef max
    #error min is defined as a macro. Define NOMINMAX.
#endif
#ifdef BEMAN_BIG_INT_MSVC
    #define NOMINMAX
#endif

// Unsupported static_assert to nothing (for old compilers) ==============
#if (!defined(BEMAN_BIG_INT_CLANG) && (defined(BEMAN_BIG_INT_GCC) && (BEMAN_BIG_INT_GCC <= 13)))
    #define BEMAN_BIG_INT_STATIC_ASSERT_FALSE(...)
#else
    #define BEMAN_BIG_INT_STATIC_ASSERT_FALSE(...) static_assert(false, __VA_ARGS__)
#endif

// Diagnostic suppression ======================================================

// See https://stackoverflow.com/q/45762357/5740428
#define BEMAN_BIG_INT_PRAGMA_STR_IMPL(...) _Pragma(#__VA_ARGS__)
#define BEMAN_BIG_INT_PRAGMA_STR(...) BEMAN_BIG_INT_PRAGMA_STR_IMPL(__VA_ARGS__)

#if defined(BEMAN_BIG_INT_GCC)
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH() _Pragma("GCC diagnostic push")
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP() _Pragma("GCC diagnostic pop")
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...) BEMAN_BIG_INT_PRAGMA_STR(GCC diagnostic ignored __VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...) BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(__VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...)
#elif defined(BEMAN_BIG_INT_CLANG)
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH() _Pragma("clang diagnostic push")
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP() _Pragma("clang diagnostic pop")
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...) BEMAN_BIG_INT_PRAGMA_STR(clang diagnostic ignored __VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...) BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(__VA_ARGS__)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...)
#elif defined(BEMAN_BIG_INT_MSVC)
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH() _Pragma("warning(push)")
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP() _Pragma("warning(pop)")
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...) BEMAN_BIG_INT_PRAGMA_STR(warning(disable : __VA_ARGS__))
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...) BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    #define BEMAN_BIG_INT_DIAGNOSTIC_POP()
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG(...)
    #define BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(...)
#endif

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

// 32-bit/64-bit ===============================================================

#include <cstdint>

#if INTPTR_MAX == INT64_MAX || defined(__wasm__) || defined(__EMSCRIPTEN__)
    #define BEMAN_BIG_INT_WORD_BITS 64
#elif INTPTR_MAX == INT32_MAX
    #define BEMAN_BIG_INT_WORD_BITS 32
#else
    #error Unknown pointer size or missing size macros!
#endif

// 128-bit integer support =====================================================

#ifdef BEMAN_BIG_INT_MSVC
    #include <__msvc_int128.hpp>
#endif // BEMAN_BIG_INT_MSVC

namespace beman::big_int::detail {

#if BEMAN_BIG_INT_BITINT_MAXWIDTH >= 128
    #define BEMAN_BIG_INT_HAS_INT128 1
using int128_t  = _BitInt(128);
using uint128_t = unsigned _BitInt(128);
#elif defined(__SIZEOF_INT128__)
    #define BEMAN_BIG_INT_HAS_INT128 1
using int128_t  = __int128;
using uint128_t = unsigned __int128;
#elif defined(BEMAN_BIG_INT_MSVC)
    #define BEMAN_BIG_INT_HAS_INT128 1
using int128_t  = std::_Signed128;
using uint128_t = std::_Unsigned128;
#endif

} // namespace beman::big_int::detail

// Limb type selection =========================================================

namespace beman::big_int {

#ifdef BEMAN_BIG_INT_FORCED_LIMB_WIDTH
    #if BEMAN_BIG_INT_FORCED_LIMB_WIDTH != 32 && BEMAN_BIG_INT_FORCED_LIMB_WIDTH != 64
        #error BEMAN_BIG_INT_FORCED_LIMB_WIDTH must be either 32 or 64!
    #endif
    #define BEMAN_BIG_INT_LIMB_WIDTH BEMAN_BIG_INT_FORCED_LIMB_WIDTH
#else
    #define BEMAN_BIG_INT_LIMB_WIDTH BEMAN_BIG_INT_WORD_BITS
#endif // BEMAN_BIG_INT_FORCED_LIMB_WIDTH

#if BEMAN_BIG_INT_LIMB_WIDTH == 64

using uint_multiprecision_t = unsigned long long;
static_assert(sizeof(uint_multiprecision_t) == 8);
namespace detail {
// Signed counterpart to uint_multiprecision_t.
using int_multiprecision_t = long long;
    #ifdef BEMAN_BIG_INT_HAS_INT128
        // Indicates that `uint_wide_t` and `int_wide_t` exist.
        #define BEMAN_BIG_INT_HAS_WIDE_INT 1
// Unsigned integer type with twice the width of uint_multiprecision_t.
using uint_wide_t = uint128_t;
// Signed integer type with twice the width of int_multiprecision_t.
using int_wide_t = int128_t;
    #endif
} // namespace detail

#elif BEMAN_BIG_INT_LIMB_WIDTH == 32

using uint_multiprecision_t = unsigned int;
static_assert(sizeof(uint_multiprecision_t) == 4);
namespace detail {
// Signed counterpart to uint_multiprecision_t.
using int_multiprecision_t = int;
    // Indicates that `uint_wide_t` and `int_wide_t` exist.
    #define BEMAN_BIG_INT_HAS_WIDE_INT 1
// Unsigned integer type with twice the width of uint_multiprecision_t.
using uint_wide_t = unsigned long long;
// Signed integer type with twice the width of int_multiprecision_t.
using int_wide_t = long long;
} // namespace detail

#else
    #error BEMAN_BIG_INT_LIMB_WIDTH must be either 32 or 64!
#endif

#define BEMAN_BIG_INT_DOUBLE_LIMB_WIDTH (BEMAN_BIG_INT_LIMB_WIDTH * 2)

} // namespace beman::big_int

// Integer concepts and traits =================================================

#include <cstdint>
#include <type_traits>
#include <concepts>
#include <limits>

namespace beman::big_int::detail {

template <class T>
concept cv_unqualified = !std::is_const_v<T> && !std::is_volatile_v<T>;

template <class T>
concept character_type =                                     //
    std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || //
    std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

#if BEMAN_BIG_INT_HAS_BUILTIN(__is_integral)
    #ifdef BEMAN_BIG_INT_HAS_BITINT
static_assert(__is_integral(_BitInt(32)) && __is_integral(unsigned _BitInt(32)),
              "Bad compiler builtin __is_integral rejects _BitInt.");
    #endif
static_assert(__is_integral(int) && __is_integral(const volatile unsigned int));
static_assert(__is_integral(char) && __is_integral(signed char) && __is_integral(unsigned char));
template <class T>
concept integral = __is_integral(T);
#elif defined(BEMAN_BIG_INT_HAS_BITINT)
// If bit-precise integers do exist but we don't have a builtin __is_integral,
// we need to create our own concept for integral types that also includes _BitInt.
template <class T>
struct is_bit_int : std::false_type {};
template <std::size_t N>
struct is_bit_int<_BitInt(N)> : std::true_type {};
template <std::size_t N>
struct is_bit_int<unsigned _BitInt(N)> : std::true_type {};
template <class T>
inline constexpr bool is_bit_int_v = is_bit_int<T>::value;
template <class T>
concept integral = std::integral<T> || is_bit_int_v<T>;
#else
// If bit-precise integers don't exist, std::integral is correct anyway.
using std::integral;
#endif

template <class T>
concept cv_unqualified_integral = integral<T> && cv_unqualified<T>;

// Modeled if `T` is a signed or unsigned integer type.
// That is, a standard integer type, extended integer type, or bit-precise integer type.
template <class T>
concept signed_or_unsigned = cv_unqualified_integral<T> //
                             && !std::is_same_v<T, bool> && !character_type<T>;
template <class T>
concept negative_representing = static_cast<T>(-1) < static_cast<T>(0);

// Modeled if `T` is a standard unsigned, extended unsigned, or bit-precise unsigned integer type.
template <class T>
concept unsigned_integer = signed_or_unsigned<T> && !negative_representing<T>;
// Modeled if `T` is a standard signed, extended signed, or bit-precise signed integer type.
template <class T>
concept signed_integer = signed_or_unsigned<T> && negative_representing<T>;

#ifdef BEMAN_BIG_INT_HAS_BITINT
static_assert(signed_or_unsigned<_BitInt(32)>);
static_assert(unsigned_integer<unsigned _BitInt(32)>);
static_assert(signed_integer<_BitInt(32)>);
#endif

// Alias template that maps a cv-unqualified integral type onto the underlying
// signed or unsigned integer type.
// For example, this converts `char8_t` to `unsigned char`, `int` to `int`, etc.
// The goal is to reduce redundant template instantiations.
template <cv_unqualified_integral T>
using make_signed_or_unsigned_t =
    std::conditional_t<std::is_signed_v<T>, std::make_signed_t<T>, std::make_unsigned_t<T>>;

template <class T>
concept cv_unqualified_floating_point = cv_unqualified<T> && std::floating_point<T>;

// Modeled if `T` is an arithmetic type - that is, a signed or unsigned integer type
// including `_BitInt` or a floating-point type.
// This extends `std::is_arithmetic_v to cover `_BitInt` types which are not standard integral.
template <class T>
concept cv_unqualified_arithmetic = cv_unqualified<T> && (integral<T> || std::floating_point<T>);

template <class T>
[[nodiscard, maybe_unused]] constexpr make_signed_or_unsigned_t<T> to_signed_or_unsigned(const T x) {
    return static_cast<make_signed_or_unsigned_t<T>>(x);
}

template <integral T>
struct width : std::integral_constant<std::size_t,
                                      static_cast<std::size_t>(std::numeric_limits<std::make_unsigned_t<T>>::digits)> {
};

#ifdef BEMAN_BIG_INT_HAS_BITINT
template <std::size_t N>
struct width<_BitInt(N)> : std::integral_constant<std::size_t, N> {};
template <std::size_t N>
struct width<unsigned _BitInt(N)> : std::integral_constant<std::size_t, N> {};
template <class T>
struct width<const T> : width<T> {};
template <class T>
struct width<volatile T> : width<T> {};
template <class T>
struct width<const volatile T> : width<T> {};
template <integral T>
inline constexpr std::size_t width_v = width<T>::value;
#else
template <integral T>
inline constexpr std::size_t width_v = std::numeric_limits<std::make_unsigned_t<T>>::digits;
#endif // BEMAN_BIG_INT_HAS_BITINT

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
#include <cstdio>
#include <source_location>

// LCOV_EXCL_START
// GCOVR_EXCL_START
namespace beman::big_int {

[[noreturn]] inline void assert_fail(const char* const          source,
                                     const std::source_location location = std::source_location::current()) {
    std::fprintf(stderr,
                 "%s:%d:%d Assertion failed: %s\nSee: %s\n",
                 location.file_name(),
                 static_cast<int>(location.line()),
                 static_cast<int>(location.column()),
                 source,
                 location.function_name());
#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_trap)
    __builtin_trap();
#else
    std::abort();
#endif
}

} // namespace beman::big_int

#define BEMAN_BIG_INT_ASSERT(...) (__VA_ARGS__ ? void() : assert_fail(#__VA_ARGS__))
// GCOVR_EXCL_STOP
// LCOV_EXCL_STOP

#ifndef NDEBUG
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) BEMAN_BIG_INT_ASSERT(__VA_ARGS__)
#else
  // The requires expression makes sure that we still check for expression validity,
  // even if the expression is not evaluated.
    #define BEMAN_BIG_INT_DEBUG_ASSERT(...) void(requires { __VA_ARGS__; })
#endif

// if consteval ================================================================

#if defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
    #define BEMAN_BIG_INT_IS_CONSTEVAL consteval
    #ifdef BEMAN_BIG_INT_MSVC
  // In MSVC, all code following `if !consteval` is considered unreachable.
  // The warning is also impossible to suppress, so NEVER use `if !consteval` on MSVC.
  // https://developercommunity.microsoft.com/t/Code-following-if-consteval-is-unreac/11073119
        #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!__builtin_is_constant_evaluated())
    #else
        #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL !consteval
    #endif // BEMAN_BIG_INT_MSVC
#elif BEMAN_BIG_INT_HAS_BUILTIN(__builtin_is_constant_evaluated)
    #define BEMAN_BIG_INT_IS_CONSTEVAL (__builtin_is_constant_evaluated())
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!__builtin_is_constant_evaluated())
#else
    #include <type_traits>
    #define BEMAN_BIG_INT_IS_CONSTEVAL (::std::is_constant_evaluated())
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL (!::std::is_constant_evaluated())
#endif

// consteval bit_cast to _BitInt ===============================================

// At the time of writing, not even clang trunk supports bit-casting to _BitInt
// during constant evaluation.
// The intended usage of this macro is
// `if BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT`
// so as to guard against accidental constexpr use of such bit-casts.

#ifdef BEMAN_BIG_INT_HAS_CONSTEXPR_BIT_CAST_TO_BIT_INT
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT constexpr(true)
#else
    #define BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT BEMAN_BIG_INT_IS_NOT_CONSTEVAL
#endif // BEMAN_BIG_INT_HAS_CONSTEXPR_BIT_CAST_TO_BIT_INT

// Constant propagation detection ==============================================

#if BEMAN_BIG_INT_HAS_BUILTIN(__builtin_constant_p)
    #define BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(...) __builtin_constant_p(__VA_ARGS__)
#else
    #define BEMAN_BIG_INT_IS_CONSTANT_PROPAGATED(...) (void(__VA_ARGS__), false)
#endif

// Division result =============================================================

namespace beman::big_int {

template <class T>
struct div_result {
    T quotient;
    T remainder;

    friend auto operator<=>(const div_result&, const div_result&) = default;
};

} // namespace beman::big_int

// =============================================================================

#endif // BEMAN_BIG_INT_CONFIG_HPP
