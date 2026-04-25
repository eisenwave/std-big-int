// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BIG_INT_HPP
#define BEMAN_BIG_INT_BIG_INT_HPP

#include <array>
#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>

#if __has_include(<stdfloat>)
    #include <stdfloat>
#endif
#include <type_traits>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/floats.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/div_impl.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

namespace beman::big_int {

// alias uint_multiprecision_t
using beman::big_int::uint_multiprecision_t;

// Forward decl so that we can define our concepts
template <std::size_t min_inplace_bits, class Allocator = std::allocator<uint_multiprecision_t>>
class basic_big_int;

template <size_t b, class A>
constexpr std::from_chars_result from_chars(const char*, const char*, basic_big_int<b, A>&, int = 10);

template <size_t b, class A>
constexpr std::to_chars_result to_chars(char*, char*, const basic_big_int<b, A>&, int = 10);

namespace detail {

template <class>
struct is_basic_big_int : std::false_type {};

template <std::size_t b, class A>
struct is_basic_big_int<basic_big_int<b, A>> : std::true_type {};

template <class T>
inline constexpr bool is_basic_big_int_v = is_basic_big_int<std::remove_cvref_t<T>>::value;

// [big.ing.expos]
template <class T>
concept arbitrary_integer = signed_or_unsigned<std::remove_cvref_t<T>> || detail::is_basic_big_int_v<T>;

template <class T>
concept arbitrary_arithmetic = std::is_floating_point_v<T> || arbitrary_integer<T>;

template <class LT, class RT>
auto common_big_int_type_impl() {
    if constexpr (is_basic_big_int_v<LT>) {
        if constexpr ((is_basic_big_int_v<RT> && std::is_same_v<LT, RT>)) {
            return std::type_identity<LT>{};
        } else if constexpr (signed_or_unsigned<RT>) {
            return std::type_identity<LT>{};
        }
    } else if constexpr (is_basic_big_int_v<RT> && signed_or_unsigned<LT>) {
        return std::type_identity<RT>{};
    }
}

template <class L, class R>
using common_big_int_type =
    typename decltype(common_big_int_type_impl<std::remove_cvref_t<L>, std::remove_cvref_t<R>>())::type;

template <class T, class U>
concept common_big_int_type_with = requires { typename common_big_int_type<T, U>; };

template <std::size_t inplace_bits, class T>
inline constexpr bool no_alloc_constructible_from = []() {
    if constexpr (std::integral<std::remove_cvref_t<T>>) {
        return width_v<std::remove_cvref_t<T>> <= inplace_bits;
    } else {
        return false;
    }
}();

template <std::size_t b, class A, class T>
inline constexpr bool is_implicit_constructible_from =
    detail::signed_or_unsigned<std::remove_cvref_t<T>> || std::is_same_v<std::remove_cvref_t<T>, basic_big_int<b, A>>;

#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
using std::allocation_result;
#else
template <class Pointer, class SizeType = std::size_t>
struct allocation_result {
    Pointer  ptr;
    SizeType count;
};
#endif // __cpp_lib_allocate_at_least

// Returns the quotient of the division `x / y`,
// rounded towards positive infinity.
template <unsigned_integer T>
[[nodiscard]] constexpr T div_to_pos_inf(const T x, const T y) {
    BEMAN_BIG_INT_DEBUG_ASSERT(y != 0);
    return (x / y) + T(x % y != 0);
}

// Returns the mathematically correct `abs(x)` for a given signed integer `x`,
// where the result is an unsigned integer.
// Unlike `std::abs`, this function has no undefined behavior in e.g. `uabs(INT_MIN)`.
template <signed_integer T>
[[nodiscard]] constexpr std::make_unsigned_t<T> uabs(const T x) noexcept {
    using U = std::make_unsigned_t<T>;
    BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4146) // unary minus on unsigned is intentional
    return x < 0 ? -static_cast<U>(x) : static_cast<U>(x);
    BEMAN_BIG_INT_DIAGNOSTIC_POP()
}

// This is purely for convenience, so we don't have to check all the time if an integer is signed or not
template <unsigned_integer T>
[[nodiscard]] constexpr T uabs(const T x) noexcept {
    return x;
}

// The integer version of signbit, and again with unsigned impl purely out of convenience
// Follows std::signbit convention: True = value is negative, False = value is positive
template <signed_integer T>
[[nodiscard]] constexpr bool integer_signbit(const T x) noexcept {
    return x < T{0};
}

template <unsigned_integer T>
[[nodiscard]] constexpr bool integer_signbit(const T) noexcept {
    return false;
}

// Returns `std::strong_ordering::less` if `x` is `std::strong_ordering::greater`, and vice versa.
[[nodiscard]] constexpr std::strong_ordering invert(const std::strong_ordering x) noexcept {
    return std::bit_cast<std::strong_ordering>(static_cast<signed char>(-std::bit_cast<signed char>(x)));
}

static_assert(invert(std::strong_ordering::less) == std::strong_ordering::greater,
              "Weird standard library. std::strong_ordering was expected to be a wrapper for signed char, "
              "where negation exchanges less and greater.");

enum struct bitwise_op : unsigned char { and_, or_, xor_ };

template <bitwise_op op, cv_unqualified_integral T>
[[nodiscard]] constexpr T eval_bitwise(const T x, const T y) noexcept {
    if constexpr (op == bitwise_op::and_) {
        return static_cast<T>(x & y);
    } else if constexpr (op == bitwise_op::or_) {
        return static_cast<T>(x | y);
    } else if constexpr (op == bitwise_op::xor_) {
        return static_cast<T>(x ^ y);
    } else {
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("Unsupported operation.");
    }
}

} // namespace detail

// [big.int.class], class template basic_big_int
template <std::size_t min_inplace_bits, class Allocator>
class BEMAN_BIG_INT_TRIVIAL_ABI basic_big_int {

    using limb_type        = uint_multiprecision_t;
    using signed_limb_type = detail::int_multiprecision_t;

#ifdef BEMAN_BIG_INT_HAS_WIDE_INT
    using double_limb_type        = detail::uint_wide_t;
    using signed_double_limb_type = detail::int_wide_t;
#endif

  public:
    using allocator_type = Allocator;
    using size_type      = std::size_t;
    using pointer        = typename std::allocator_traits<Allocator>::pointer;
    using const_pointer  = typename std::allocator_traits<Allocator>::const_pointer;
    static_assert(std::is_same_v<typename Allocator::value_type, uint_multiprecision_t>,
                  "Allocator::value_type must be uint_multiprecision_t.");

    template <std::size_t, class>
    friend class basic_big_int;

    template <size_t b, class A>
    friend constexpr std::from_chars_result from_chars(const char*, const char*, basic_big_int<b, A>&, int);

    template <size_t b, class A>
    friend constexpr std::to_chars_result to_chars(char*, char*, const basic_big_int<b, A>&, int);

  private:
    using alloc_traits = std::allocator_traits<Allocator>;
    using alloc_result = detail::allocation_result<pointer>;

    static constexpr size_type bits_per_limb = detail::width_v<limb_type>;

  public:
    // Never fewer limbs than would fit in the pointer footprint  of the union,
    // so the union doesn't waste space.
    static constexpr size_type inplace_capacity = std::max(detail::div_to_pos_inf(min_inplace_bits, bits_per_limb),
                                                           detail::div_to_pos_inf(sizeof(pointer), sizeof(limb_type)));
    static_assert(min_inplace_bits > 0);
    static_assert(inplace_capacity > 0);
    static constexpr size_type inplace_bits = inplace_capacity * bits_per_limb;

  private:
    union data_type {
        pointer   data;
        limb_type limbs[inplace_capacity];

        constexpr data_type() noexcept : limbs{} {}
    };

    std::uint32_t                                  m_capacity;      // 0 = static storage, >0 = heap capacity
    std::uint32_t                                  m_size_and_sign; // bit 31 = sign, bits 0-30 = limb count
    data_type                                      m_storage;
    BEMAN_BIG_INT_NO_UNIQUE_ADDRESS allocator_type m_alloc;

    // Internal accessors for the packed representation
    [[nodiscard]] constexpr bool          is_representation_inplace() const noexcept;
    [[nodiscard]] constexpr std::uint32_t limb_count() const noexcept;
    // Returns `true` if the integer value is less than zero, otherwise `false`.
    [[nodiscard]] constexpr bool is_negative() const noexcept;
    // Returns `true` if the integer value is zero, otherwise `false`.
    [[nodiscard]] constexpr bool is_zero() const noexcept;
    // Like `is_zero`, but bypasses any access to the sign bit.
    // This function is safe to call even if the object is in a corrupt negative zero state.
    [[nodiscard]] constexpr bool unchecked_is_magnitude_zero() const noexcept;
    // Sets the limb count to a nonzero number `n`.
    // This can possibly create a non-canonical representation containing trailing zeros.
    // No check or trim is performed to prevent trailing zeros.
    constexpr void unchecked_set_limb_count(std::uint32_t n) noexcept;
    // Sets the value of the sign bit to `s`,
    // meaning that the integer value becomes negative if `s` is `true`.
    // This can possibly result in a corrupt negative zero state.
    constexpr void unchecked_set_sign(bool s) noexcept;
    // Canonicalizes the magnitude by trimming any most significant zeroes.
    // This does not modify the contents of the representation,
    // only the `limb_count()` is equal to `1` after calling this function if the integer value is zero.
    // This function is safe to call on negative zero; the sign bit is not affected.
    constexpr void                           unchecked_trim_magnitude() noexcept;
    constexpr void                           negate() noexcept;
    constexpr void                           set_zero() noexcept;
    [[nodiscard]] constexpr limb_type*       limb_ptr() noexcept;
    [[nodiscard]] constexpr const limb_type* limb_ptr() const noexcept;

  public:
    // [big.int.cons], construct/copy/destroy
    constexpr basic_big_int() noexcept(noexcept(Allocator())) : m_capacity{0}, m_size_and_sign{1}, m_storage{} {}
    constexpr explicit basic_big_int(const Allocator& a) noexcept
        : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {}
    constexpr basic_big_int(const basic_big_int& x);
    constexpr basic_big_int(basic_big_int&& x) noexcept;

    // Defined inline: MSVC cannot match out-of-line definitions
    // of constructors with conditional explicit + requires.
    template <detail::arbitrary_arithmetic T>
        requires(!std::same_as<std::remove_cvref_t<T>, basic_big_int>)
    constexpr explicit(!detail::is_implicit_constructible_from<inplace_bits, Allocator, T>)
        basic_big_int(T&& value) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>)
        : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{} {
        if constexpr (std::is_floating_point_v<std::remove_cvref_t<T>>) {
            assign_from_float(value);
        } else {
            if constexpr (std::is_signed_v<std::remove_cvref_t<T>>) {
                unchecked_set_sign(value < std::remove_cvref_t<T>{0});
                assign_magnitude(detail::uabs(value));
            } else {
                assign_magnitude(value);
            }
        }
    }

    template <detail::arbitrary_arithmetic T>
    constexpr basic_big_int(const T&              value,
                            const allocator_type& a) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>);

    template <std::input_iterator I, std::sentinel_for<I> S>
        requires detail::signed_or_unsigned<std::iter_value_t<I>>
    constexpr basic_big_int(I begin, S end, const allocator_type& a = allocator_type());

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    template <std::ranges::input_range R>
        requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
    constexpr explicit basic_big_int(std::from_range_t, R&& r, const allocator_type& a = allocator_type())
        : basic_big_int(std::ranges::begin(r), std::ranges::end(r), a) {}
#endif

    constexpr ~basic_big_int();

    // [big.int.modifiers]
    constexpr basic_big_int& operator=(const basic_big_int& x);
    constexpr basic_big_int& operator=(basic_big_int&& x) noexcept;

    // Defined inline: see note above
    template <detail::arbitrary_integer T>
        requires(!std::same_as<std::remove_cvref_t<T>, basic_big_int>)
    constexpr basic_big_int& operator=(T&& x) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>) {
        if constexpr (detail::is_basic_big_int_v<T>) {
            const auto count = x.limb_count();
            grow(count);
            auto* dst = limb_ptr();
            std::copy_n(x.limb_ptr(), count, dst);
            for (size_type i = count; i < limb_count(); ++i) {
                dst[i] = 0;
            }
            unchecked_set_limb_count(count);
            unchecked_set_sign(x.is_negative());
        } else {
            using U                 = std::make_unsigned_t<std::remove_cvref_t<T>>;
            const bool          neg = std::is_signed_v<std::remove_cvref_t<T>> && x < std::remove_cvref_t<T>{0};
            const U             mag = neg ? static_cast<U>(U{0} - static_cast<U>(x)) : static_cast<U>(x);
            constexpr size_type n   = detail::div_to_pos_inf(sizeof(U), sizeof(limb_type));
            grow(n);
            const auto old_count = limb_count();
            assign_magnitude(mag);
            auto* dst = limb_ptr();
            for (size_type i = limb_count(); i < old_count; ++i) {
                dst[i] = 0;
            }
            unchecked_set_sign(neg);
        }
        return *this;
    }

    // TODO(alcxpr): compound operators

    template <detail::signed_or_unsigned S>
    constexpr basic_big_int& operator>>=(S s);
    template <detail::signed_or_unsigned S>
    constexpr basic_big_int& operator<<=(S s);

    template <class T>
    constexpr basic_big_int& operator+=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator-=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator*=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator/=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;
    template <class T>
    constexpr basic_big_int& operator%=(T&& rhs)
        requires detail::common_big_int_type_with<T, basic_big_int>;

    // [big.int.ops]
    [[nodiscard]] constexpr size_type                              width_mag() const noexcept;
    [[nodiscard]] constexpr std::span<const uint_multiprecision_t> representation() const noexcept;
    [[nodiscard]] constexpr allocator_type                         get_allocator() const noexcept;
    [[nodiscard]] constexpr size_type                              size() const noexcept;
    [[nodiscard]] static constexpr size_type                       max_size() noexcept;
    constexpr void                                                 reserve(size_type n);
    [[nodiscard]] constexpr size_type                              capacity() const noexcept;
    constexpr void                                                 shrink_to_fit();

    // [big.int.unary]
    [[nodiscard]] constexpr basic_big_int operator+() const&;
    [[nodiscard]] constexpr basic_big_int operator+() && noexcept;
    [[nodiscard]] constexpr basic_big_int operator-() const&;
    [[nodiscard]] constexpr basic_big_int operator-() && noexcept;
    [[nodiscard]] constexpr basic_big_int operator~() const&;
    [[nodiscard]] constexpr basic_big_int operator~() &&;

    constexpr basic_big_int& operator++() &;
    constexpr basic_big_int  operator++(int) &;
    constexpr basic_big_int& operator--() &;
    constexpr basic_big_int  operator--(int) &;

    // [big.int.cmp]
    template <class L, detail::common_big_int_type_with<L> R>
    friend constexpr bool operator==(const L& lhs, const R& rhs) noexcept;
    template <class L, detail::common_big_int_type_with<L> R>
    friend constexpr std::strong_ordering operator<=>(const L& lhs, const R& rhs) noexcept;

    // [big.int.binary]
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator+(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator-(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator*(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator/(L&& x, R&& y);
    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator%(L&& x, R&& y);

    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator&(L&& x, R&& y);

    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator|(L&& x, R&& y);

    template <class L, class R>
    friend constexpr detail::common_big_int_type<L, R> operator^(L&& x, R&& y);

    template <class T, detail::signed_or_unsigned S>
        requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
    friend constexpr std::remove_cvref_t<T> operator<<(T&& x, S s);
    template <class T, detail::signed_or_unsigned S>
        requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
    friend constexpr std::remove_cvref_t<T> operator>>(T&& x, S s);

    // [big.int.conv], conversions
    template <detail::cv_unqualified_arithmetic T>
    [[nodiscard]] constexpr explicit operator T() const noexcept {
        return to<T>();
    }

    // [big.int.div], division
    template <class L, class R>
    friend constexpr div_result<detail::common_big_int_type<L, R>> div_rem_to_zero(L&&, R&&);

  private:
    template <detail::unsigned_integer T>
    constexpr void assign_magnitude(T value) noexcept;
    template <detail::cv_unqualified_floating_point F>
    constexpr void assign_from_float(F value) noexcept;

    [[nodiscard]] constexpr alloc_result alloc_limbs(size_type n);
    constexpr void                       free_limbs(pointer p, size_type n);
    constexpr void                       free_storage();
    constexpr void                       grow(size_type limbs_needed);
    constexpr void                       copy_n_to_allocation(const limb_type* p, size_type n, alloc_result out);
    constexpr void                       push_back_limb(limb_type limb);

    // We are limited in our shifting to what we can encode into our control block, which is 30 (or 27) bits of limbs
    // Our max shift is then the number of bits represented in these blocks plus the theoretical 63 (or 31)
    // that are in the same limb.
    using shift_type                      = uint_multiprecision_t;
    static constexpr shift_type shift_max = static_cast<shift_type>(max_size()) * bits_per_limb;

    // Increases the magnitude by one, without affecting the sign bit.
    // Returns `true` on carry in the uppermost limb.
    constexpr bool unchecked_increment_magnitude();
    // Decreases the magnitude by one.
    // If the value was zero prior to the operation,
    // the magnitude is set to `1`.
    // Returns `true` on borrow in the uppermost limb,
    // meaning that the value was originally zero.
    constexpr bool unchecked_decrement_magnitude();

    constexpr void shift_left(shift_type s);
    constexpr void shift_right(shift_type s);

    template <detail::signed_or_unsigned Integer>
    [[nodiscard]] constexpr bool equals_integer(Integer x) const noexcept;
    [[nodiscard]] constexpr bool equals_big_int(const basic_big_int& x) const noexcept;
    template <std::size_t extent>
    [[nodiscard]] constexpr bool equals_limbs(std::span<const uint_multiprecision_t, extent> limbs,
                                              bool limbs_negative) const noexcept;

    template <detail::signed_or_unsigned Integer>
    [[nodiscard]] constexpr std::strong_ordering compare_integer(Integer x) const noexcept;
    [[nodiscard]] constexpr std::strong_ordering compare_big_int(const basic_big_int& x) const noexcept;
    template <std::size_t extent>
    [[nodiscard]] constexpr std::strong_ordering compare_limbs(std::span<const uint_multiprecision_t, extent> limbs,
                                                               bool limbs_negative) const noexcept;

    // Adds `(other, other_neg)` into `*this` in place. Shared core for `operator+`
    // and `operator-`: the caller chooses the destination (an rvalue operand's
    // storage or a copy of an lvalue operand) and supplies the other side as a limb
    // span + sign. Only allocates when the result genuinely requires more limbs
    // than the current capacity. Preserves the no-negative-zero and trimmed-top-limb
    // invariants.
    template <std::size_t extent_other>
    constexpr void add_in_place(std::span<const uint_multiprecision_t, extent_other> other, bool other_neg);

    // Computes `a * b` and stores the result into `*this`.
    template <std::size_t extent_a, std::size_t extent_b>
    constexpr void multiply_into(std::span<const uint_multiprecision_t, extent_a> a,
                                 bool                                             a_neg,
                                 std::span<const uint_multiprecision_t, extent_b> b,
                                 bool                                             b_neg);

    // Computes the quotient, remainder, or both, of an integer division, depending on `op`.
    // If `op` is `div` or `rem`, this object is set to the desired result,
    // and the returned value is unspecified.
    // If `op` is `div_rem`, this object is set to the quotient, and the remainder is returned.
    //
    // In any case, the division is rounded towards zero (i.e. it is truncating),
    // same as the division between fundamental types.
    template <std::size_t extent_a, std::size_t extent_b>
    constexpr basic_big_int divmod_into(std::span<const uint_multiprecision_t, extent_a> dividend,
                                        bool                                             dividend_neg,
                                        std::span<const uint_multiprecision_t, extent_b> divisor,
                                        bool                                             divisor_neg,
                                        detail::division_op                              op);

    // Single-limb divisor fast path.
    // Avoids limb-vector promotion and allocation of a scratch remainder buffer.
    template <std::size_t extent_a>
    constexpr uint_multiprecision_t divmod_into_short(std::span<const uint_multiprecision_t, extent_a> dividend,
                                                      bool                                             dividend_neg,
                                                      uint_multiprecision_t                            divisor,
                                                      bool                                             divisor_neg,
                                                      detail::division_op                              op);

    // Shared implementation behind copy-assign, move-assign, and the lvalue
    // branches of `operator+` / `operator-`.
    // Sets `*this` to a copy of `src`, reusing the existing allocation whenever
    // the effective capacity already fits `src.limb_count() + extra_space` limbs.
    // `extra_space` lets callers that know they are about to grow by a fixed
    // amount (e.g., a carry-out of one limb in addition) reserve that space
    // up front so that a subsequent grow is not needed.
    template <class Src>
        requires std::same_as<std::remove_cvref_t<Src>, basic_big_int>
    constexpr void assign_value(Src&& src, const std::size_t extra_space = 0) {
        if (std::addressof(*this) == std::addressof(src)) {
            // `assign_value(std::move(*this), ...)` is a no-op. Also guards the
            // self-aliasing the existing `operator=` overloads protected against.
            return;
        }

        constexpr bool    is_move   = !std::is_lvalue_reference_v<Src>;
        const std::size_t src_count = src.limb_count();
        const std::size_t needed    = src_count + extra_space;
        const std::size_t eff_cap   = is_representation_inplace() ? inplace_capacity : m_capacity;

        if (needed <= eff_cap) {
            // Fast path: current buffer is already big enough
            const auto old_count = limb_count();
            m_size_and_sign      = src.m_size_and_sign;
            if constexpr (is_move) {
                m_alloc = std::move(src.m_alloc);
            } else {
                m_alloc = src.m_alloc;
            }
            limb_type* const       dst_limbs = limb_ptr();
            const limb_type* const src_limbs = src.limb_ptr();
            std::copy_n(src_limbs, src_count, dst_limbs);
            // Preserve the "limbs[limb_count..inplace_capacity) == 0" invariant that
            // `inplace_to_bit_uint` relies on. Only relevant for inline storage.
            if (is_representation_inplace()) {
                for (std::size_t i = src_count; i < old_count; ++i) {
                    dst_limbs[i] = limb_type{0};
                }
            }
            return;
        }

        // Slow path: current buffer is too small.
        // Release it and get a new allocation
        free_storage();
        m_size_and_sign = src.m_size_and_sign;
        if constexpr (is_move) {
            m_alloc = std::move(src.m_alloc);
        } else {
            m_alloc = src.m_alloc;
        }

        if (src.is_representation_inplace() && needed <= inplace_capacity) {
            // Both src and the requested headroom fit inline.
            m_capacity = 0;
            for (std::size_t i = 0; i < inplace_capacity; ++i) {
                m_storage.limbs[i] = src.m_storage.limbs[i];
            }
            return;
        }

        if constexpr (is_move) {
            // For a heap `src`, adopt its pointer unconditionally and then grow
            // if the stolen capacity doesn't cover `needed`.
            if (!src.is_representation_inplace()) {
                m_capacity             = src.m_capacity;
                m_storage.data         = src.m_storage.data;
                src.m_capacity         = 0;
                src.m_size_and_sign    = 1;
                src.m_storage.limbs[0] = limb_type{0};
                if (m_capacity < needed) {
                    grow(needed);
                }
                return;
            }
            // `src` is inline, fall through to the allocate-and-copy path below.
        }

        // Fall back to a fresh allocation of `needed` limbs.
        const alloc_result allocation = alloc_limbs(needed);
        copy_n_to_allocation(src.limb_ptr(), src_count, allocation);
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
        m_storage.data = allocation.ptr;
    }

    // Efficiently performs `*this |= bits << offset`, as if `bits` was wrapped in `basic_big_int`.
    // The behavior is undefined there are any existing nonzero bits overwritten.
    constexpr void unchecked_init_magnitude_bits_at(const uint_multiprecision_t bits, const size_type offset) {
        const size_type limb_offset = offset / bits_per_limb;
        const size_type bit_offset  = offset % bits_per_limb;
        if (bit_offset == 0) {
            grow(limb_offset + 1);
            BEMAN_BIG_INT_DEBUG_ASSERT(limb_ptr()[limb_offset] == 0);
            limb_ptr()[limb_offset] = bits;
            if (limb_count() < limb_offset + 1) {
                unchecked_set_limb_count(static_cast<std::uint32_t>(limb_offset + 1));
            }
        } else {
            grow(limb_offset + 2);
            limb_ptr()[limb_offset + 0] |= bits << bit_offset;
            limb_ptr()[limb_offset + 1] |= bits >> (bits_per_limb - bit_offset);
            const size_type hi_limb = (bits >> (bits_per_limb - bit_offset)) != 0 ? limb_offset + 2 : limb_offset + 1;
            if (limb_count() < hi_limb) {
                unchecked_set_limb_count(static_cast<std::uint32_t>(hi_limb));
            }
        }
    }

    // Returns the bits at the specified bit offset.
    // Bits from at most two limbs are fetched via funnel-shift.
    [[nodiscard]] constexpr uint_multiprecision_t get_bits_at(const size_type offset) const {
        const auto limb_offset = offset / bits_per_limb;
        const auto bit_offset  = static_cast<unsigned int>(offset % bits_per_limb);
        BEMAN_BIG_INT_ASSERT(limb_offset < limb_count());
        const detail::wide<uint_multiprecision_t> bits{
            .low_bits  = limb_ptr()[limb_offset],
            .high_bits = limb_offset + 1 == limb_count() ? 0 : limb_ptr()[limb_offset + 1],
        };
        return detail::funnel_shr(bits, bit_offset);
    }

    // If `true`, `inplace_to_bit_uint` may be called.
    // Otherwise, the function is deleted.
    static constexpr bool        has_inplace_to_bit_uint = inplace_bits <= BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_bit_uint() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_bit_uint
    {
        static_assert(std::has_unique_object_representations_v<uint_multiprecision_t>,
                      "_BitInt conversion doesn't work when there is padding.");
        BEMAN_BIG_INT_DEBUG_ASSERT(is_representation_inplace());

        using Result = unsigned _BitInt(inplace_bits);
        if constexpr (inplace_bits == bits_per_limb) {
            // If there is only a single inplace limb,
            // static_cast and std::bit_cast are equivalent.
            // This special case also makes the <<= below safe.
            return static_cast<Result>(m_storage.limbs[0]);
        } else {
            if constexpr (std::endian::native == std::endian::little) {
                if BEMAN_BIG_INT_IS_NOT_CONSTEVAL_IF_HAS_NO_CONSTEXPR_BIT_CAST_TO_BIT_INT {
                    return std::bit_cast<Result>(m_storage.limbs);
                }
            }
            // Naive fallback implementation always works.
            // This is needed for big endian and when neither static_cast nor bit_cast work.
            Result result = 0;
            for (const uint_multiprecision_t limb : m_storage.limbs) {
                result <<= bits_per_limb;
                result |= limb;
            }
            return result;
        }
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    // If `true`, `inplace_to_wide_bit_uint` may be called.
    // Otherwise, the function is deleted.
    static constexpr bool        has_inplace_to_wide_bit_uint = inplace_bits * 2 <= BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_wide_bit_uint() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_wide_bit_uint
    {
        static_assert(std::has_unique_object_representations_v<uint_multiprecision_t>,
                      "_BitInt conversion doesn't work when there is padding.");
        return static_cast<unsigned _BitInt(2 * inplace_bits)>(inplace_to_bit_uint());
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    // If `true`, `inplace_to_bit_sint` may be called.
    // Otherwise, the function is deleted.
    static constexpr bool        has_inplace_to_bit_sint = inplace_bits < BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_sbit_int() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_bit_sint
    {
        static_assert(std::has_unique_object_representations_v<uint_multiprecision_t>,
                      "_BitInt conversion doesn't work when there is padding.");
        // Use `inplace_bits + 1` to avoid signed overflow when negating a value
        // with the high bit set.
        const auto mag = static_cast<_BitInt(inplace_bits + 1)>(inplace_to_bit_uint());
        return is_negative() ? -mag : mag;
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    template <detail::cv_unqualified_arithmetic T, bool ignore_sign = false>
    [[nodiscard]] constexpr T to() const noexcept;

    template <detail::bitwise_op op, bool neg_left, bool neg_right, std::size_t extent_a, std::size_t extent_b>
    [[nodiscard]] static constexpr basic_big_int
    make_bitwise_of_limbs(std::span<const uint_multiprecision_t, extent_a> lhs,
                          std::span<const uint_multiprecision_t, extent_b> rhs);

    template <detail::bitwise_op op, std::size_t extent_a, std::size_t extent_b>
    [[nodiscard]] static constexpr basic_big_int
    dispatch_bitwise(const std::span<const uint_multiprecision_t, extent_a> lhs,
                     const bool                                             lhs_neg,
                     const std::span<const uint_multiprecision_t, extent_b> rhs,
                     const bool                                             rhs_neg);

    template <detail::bitwise_op op, class L, class R>
    [[nodiscard]] static constexpr detail::common_big_int_type<L, R> bitwise_impl(L&& x, R&& y);
};

// =============================================================================
// Out-of-class definitions
// =============================================================================

// Internal accessors

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::is_representation_inplace() const noexcept {
    return m_capacity == 0;
}

template <std::size_t b, class A>
constexpr std::uint32_t basic_big_int<b, A>::limb_count() const noexcept {
    constexpr std::uint32_t negative_zero_size_and_sign = 0x8000'0000U;
    BEMAN_BIG_INT_DEBUG_ASSERT(m_size_and_sign != negative_zero_size_and_sign);
    return m_size_and_sign & 0x7FFF'FFFFU;
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::is_negative() const noexcept {
    constexpr std::uint32_t negative_zero_size_and_sign = 0x8000'0000U;
    BEMAN_BIG_INT_DEBUG_ASSERT(m_size_and_sign != negative_zero_size_and_sign);
    return (m_size_and_sign & 0x8000'0000U) != 0;
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::is_zero() const noexcept {
    // We have the invariant that the sign bit is never set for zero magnitude,
    // so negative numbers short-circuit here.
    return !is_negative() && unchecked_is_magnitude_zero();
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::unchecked_is_magnitude_zero() const noexcept {
    const std::uint32_t unchecked_limb_count = m_size_and_sign & 0x7FFF'FFFFU;
    const limb_type*    limbs                = limb_ptr();
    for (std::uint32_t i = 0; i < unchecked_limb_count; ++i) {
        if (limbs[i] != 0) {
            return false;
        }
    }
    return true;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::unchecked_set_limb_count(const std::uint32_t n) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0);
    m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | n;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::unchecked_set_sign(const bool s) noexcept {
    m_size_and_sign = (m_size_and_sign & 0x7FFF'FFFFU) | (static_cast<std::uint32_t>(s) << 31);
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::negate() noexcept {
    if (!is_zero()) {
        m_size_and_sign ^= 0x8000'0000U;
    }
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::set_zero() noexcept {
    limb_ptr()[0]   = 0;
    m_size_and_sign = 1;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::unchecked_trim_magnitude() noexcept {
    const limb_type* const limbs = limb_ptr();
    std::uint32_t          size  = m_size_and_sign & 0x7FFF'FFFFU;
    if (size > 1 && limbs[size - 1] == 0) {
        do {
            if (limbs[size - 1] != 0) {
                break;
            }
        } while (--size > 1);
        m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | size;
    }
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::limb_ptr() noexcept -> limb_type* {
    return is_representation_inplace() ? m_storage.limbs : m_storage.data;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::limb_ptr() const noexcept -> const limb_type* {
    return is_representation_inplace() ? m_storage.limbs : m_storage.data;
}

// [big.int.cons] — constructors

template <std::size_t b, class A>
constexpr basic_big_int<b, A>::basic_big_int(const basic_big_int& x)
    : m_capacity{0}, m_size_and_sign{x.m_size_and_sign}, m_storage{}, m_alloc{x.m_alloc} {
    if (x.limb_count() <= inplace_capacity) {
        if (x.is_representation_inplace()) {
            for (size_type i = 0; i < inplace_capacity; ++i) {
                m_storage.limbs[i] = x.m_storage.limbs[i];
            }
        } else {
            // This case can happen if e.g. `x.reserve(100)` was called
            // but the integer value of `x` fits into inplace storage.
            for (size_type i = 0; i < x.limb_count(); ++i) {
                m_storage.limbs[i] = x.m_storage.data[i];
            }
            for (size_type i = x.limb_count(); i < inplace_capacity; ++i) {
                m_storage.limbs[i] = {};
            }
        }
    } else {
        const alloc_result allocation = alloc_limbs(x.limb_count());
        copy_n_to_allocation(x.m_storage.data, x.limb_count(), allocation);
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
        m_storage.data = allocation.ptr;
    }
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>::basic_big_int(basic_big_int&& x) noexcept
    : m_capacity{x.m_capacity}, m_size_and_sign{x.m_size_and_sign}, m_storage{}, m_alloc{std::move(x.m_alloc)} {
    if (x.is_representation_inplace()) {
        for (size_type i = 0; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = x.m_storage.limbs[i];
        }
    } else {
        m_storage.data    = x.m_storage.data;
        x.m_capacity      = 0;
        x.m_size_and_sign = 1;
        x.m_storage       = {};
    }
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>& basic_big_int<b, A>::operator=(const basic_big_int& x) {
    assign_value(x);
    return *this;
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>& basic_big_int<b, A>::operator=(basic_big_int&& x) noexcept {
    // Invariant: `assign_value` with `extra_space == 0` and an rvalue `src`
    // never allocates -- either `*this` already fits `x.limb_count()`, or we
    // steal `x`'s heap buffer, or `x` is inline and fits inline in `*this`.
    // So the `noexcept` contract holds even though `assign_value` itself is
    // not marked `noexcept`.
    assign_value(std::move(x));
    return *this;
}

template <std::size_t b, class A>
template <detail::arbitrary_arithmetic T>
constexpr basic_big_int<b, A>::basic_big_int(const T& value, const allocator_type& a) noexcept(
    detail::no_alloc_constructible_from<inplace_bits, T>)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {
    if constexpr (std::is_floating_point_v<std::remove_cvref_t<T>>) {
        assign_from_float(value);
    } else {
        if constexpr (std::is_signed_v<std::remove_cvref_t<T>>) {
            unchecked_set_sign(value < 0);
            assign_magnitude(detail::uabs(value));
        } else {
            assign_magnitude(value);
        }
    }
}

template <std::size_t b, class A>
template <std::input_iterator I, std::sentinel_for<I> S>
    requires detail::signed_or_unsigned<std::iter_value_t<I>>
constexpr basic_big_int<b, A>::basic_big_int(I begin, S end, const allocator_type& a)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {

    if constexpr (std::ranges::sized_range<std::ranges::subrange<I, S>>) {
        reserve(std::ranges::size(std::ranges::subrange(begin, end)));
        std::size_t i   = 0;
        auto* const dst = limb_ptr();
        for (; begin != end; ++begin) {
            using U = std::make_unsigned_t<std::iter_value_t<I>>;
            std::construct_at(dst + i++, static_cast<limb_type>(static_cast<U>(*begin)));
        }
        if (i == 0) {
            unchecked_set_limb_count(1);
        } else {
            unchecked_set_limb_count(static_cast<std::uint32_t>(i));
            unchecked_trim_magnitude();
        }
    } else {
        for (; begin != end; ++begin) {
            using U = std::make_unsigned_t<std::iter_value_t<I>>;
            push_back_limb(static_cast<limb_type>(static_cast<U>(*begin)));
        }
        unchecked_trim_magnitude();
    }
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>::~basic_big_int() {
    free_storage();
}

// [big.int.modifiers]

template <std::size_t b, class A>
template <detail::signed_or_unsigned S>
constexpr auto basic_big_int<b, A>::operator>>=(const S s) -> basic_big_int& {
    // If this pattern comes up more often, we should consider something like a `safe_cast` utility.
    // This would convert to another type and signal whether the result is exactly representable.
    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::make_unsigned_t<S>>(s) <= shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    }
    shift_right(static_cast<shift_type>(s));
    return *this;
}

template <std::size_t b, class A>
template <detail::signed_or_unsigned S>
constexpr auto basic_big_int<b, A>::operator<<=(const S s) -> basic_big_int& {
    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::make_unsigned_t<S>>(s) <= shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    }
    shift_left(static_cast<shift_type>(s));
    return *this;
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::unchecked_increment_magnitude() {
    limb_type* const limbs    = limb_ptr();
    bool             carry_in = true;
    for (size_type i = 0; carry_in && i < limb_count(); ++i) {
        const auto [sum, carry] = detail::carrying_add(limbs[i], limb_type{0}, carry_in);
        limbs[i]                = sum;
        carry_in                = carry;
    }
    if (carry_in) {
        reserve(limb_count() + 1);
        limb_ptr()[limb_count()] = limb_type{1};
        unchecked_set_limb_count(limb_count() + 1);
    }
    return carry_in;
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::unchecked_decrement_magnitude() {
    limb_type* const limbs     = limb_ptr();
    bool             borrow_in = true;
    for (size_type i = 0; borrow_in && i < limb_count(); ++i) {
        const auto [difference, borrow] = detail::borrowing_sub(limbs[i], limb_type{0}, borrow_in);
        limbs[i]                        = difference;
        borrow_in                       = borrow;
    }

    if (borrow_in) {
        // Getting a borrow after the loop can only happen if the magnitude was zero,
        // meaning that we produce `-1` with this operation.
        BEMAN_BIG_INT_DEBUG_ASSERT(limb_count() != 0);
        limbs[0] = 1;
        unchecked_set_limb_count(1);
    }
    return borrow_in;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::shift_left(const shift_type s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    if (s == 0) {
        return;
    }

    const shift_type shifted_limbs = s / bits_per_limb;
    const shift_type shifted_bits  = s % bits_per_limb;

    // Only reserve an extra limb for the bit-shift when the top limb doesn't enough leading zeros.
    // `countl_zero` tells us exactly how many bits of headroom the current top limb provides.
    const bool needs_extra_limb =
        shifted_bits != 0 && static_cast<shift_type>(std::countl_zero(limb_ptr()[limb_count() - 1])) < shifted_bits;
    reserve(limb_count() + shifted_limbs + static_cast<size_type>(needs_extra_limb));
    limb_type* const limbs = limb_ptr();

    if (shifted_limbs != 0) {
        const auto current_count = limb_count();
        std::copy_backward(limbs, limbs + current_count, limbs + current_count + shifted_limbs);
        std::fill_n(limbs, static_cast<std::ptrdiff_t>(shifted_limbs), limb_type{0});
        unchecked_set_limb_count(static_cast<std::uint32_t>(current_count + shifted_limbs));
    }
    if (shifted_bits != 0) {
        const auto      current_count = limb_count();
        const limb_type overflow      = limbs[current_count - 1] >> (bits_per_limb - shifted_bits);

        // Shift all limbs in place, top to bottom.
        // Each iteration reads limbs[i-1] (original) and limbs[i] (original), writes limbs[i].
        // This avoids an out-of-bounds write
        for (shift_type i = current_count - 1; i > shifted_limbs; --i) {
            const detail::wide<limb_type> all_bits{.low_bits = limbs[i - 1], .high_bits = limbs[i]};
            limbs[i] = detail::funnel_shl(all_bits, static_cast<unsigned int>(shifted_bits));
        }
        limbs[shifted_limbs] <<= shifted_bits;

        if (overflow != 0) {
            limbs[current_count] = overflow;
            unchecked_set_limb_count(static_cast<std::uint32_t>(current_count + 1));
        }
    }
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::shift_right(const shift_type s) {
    BEMAN_BIG_INT_DEBUG_ASSERT(s <= shift_max);
    if (s == 0) {
        return;
    }

    const shift_type shifted_limbs = s / bits_per_limb;
    const shift_type shifted_bits  = s % bits_per_limb;
    limb_type* const limbs         = limb_ptr();

    // Shifting to the right has the effect of dividing by `pow(2, N)` rounded towards negative infinity.
    // When we "discard" limbs, this has truncating rounding;
    // for positive numbers that is already what we need, but requires adjustment for negative numbers.
    // In that case, we need to figure out whether the result is inexact
    // by detecting whether any nonzero bits are shifted out.
    const bool needs_decrement = is_negative() && [&] {
        for (shift_type i = 0; i < shifted_limbs; ++i) {
            if (i >= limb_count()) {
                return false;
            }
            if (limbs[i] != 0) {
                return true;
            }
        }
        const auto shift_mask = (limb_type{1} << shifted_bits) - 1;
        return shifted_limbs < limb_count() && (limbs[shifted_limbs] & shift_mask) != 0;
    }();

    if (shifted_limbs != 0) {
        const auto current_count = limb_count();
        if (shifted_limbs >= current_count) {
            limbs[0] = 0;
            unchecked_set_limb_count(1);
        } else {
            std::shift_left(limbs, limbs + current_count, static_cast<std::ptrdiff_t>(shifted_limbs));
            unchecked_set_limb_count(static_cast<std::uint32_t>(current_count - shifted_limbs));
        }
    }
    if (shifted_bits != 0) {
        for (size_type i = 0; i + 1 < limb_count(); ++i) {
            const detail::wide<limb_type> all_bits{.low_bits = limbs[i], .high_bits = limbs[i + 1]};
            limbs[i] = detail::funnel_shr(all_bits, static_cast<unsigned int>(shifted_bits));
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(limb_count() != 0);
        limbs[limb_count() - 1] >>= shifted_bits;
    }
    unchecked_trim_magnitude();
    if (needs_decrement) {
        // See above for rounding considerations.
        // Also note that we may be holding a negative zero right now,
        // and increasing the magnitude converts that into `-1`.
        unchecked_increment_magnitude();
        BEMAN_BIG_INT_DEBUG_ASSERT(is_negative());
    }
}

// [big.int.ops]

template <std::size_t b, class A>
constexpr std::size_t basic_big_int<b, A>::width_mag() const noexcept {
    const auto count = limb_count();
    const auto top   = limb_ptr()[count - 1];
    if (top == 0) {
        return 0;
    }
    return (count - 1) * bits_per_limb + (bits_per_limb - static_cast<size_type>(std::countl_zero(top)));
}

template <std::size_t b, class A>
constexpr std::span<const uint_multiprecision_t> basic_big_int<b, A>::representation() const noexcept {
    return {limb_ptr(), limb_count()};
}

template <std::size_t b, class A>
constexpr typename basic_big_int<b, A>::allocator_type basic_big_int<b, A>::get_allocator() const noexcept {
    return m_alloc;
}

template <std::size_t b, class A>
constexpr std::size_t basic_big_int<b, A>::size() const noexcept {
    if (is_representation_inplace()) {
        return inplace_capacity;
    } else {
        return limb_count();
    }
}

template <std::size_t b, class A>
constexpr std::size_t basic_big_int<b, A>::max_size() noexcept {
    // We use the high bit to encode the sign, so we are limited to 2^31 on 64-bit architectures
    // On 32-bit architectures we reduce to 2^27 so that the number of bits can be represented by size_type
    constexpr std::uint32_t offset = sizeof(size_type) == sizeof(std::uint64_t) ? 1U : 4U;
    return std::numeric_limits<std::uint32_t>::max() >> offset;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::reserve(const size_type n) {
    grow(n);
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::capacity() const noexcept -> size_type {
    return m_capacity;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::shrink_to_fit() {
    const auto count = limb_count();

    if (is_representation_inplace() || m_capacity <= count) {
        return;
    }

    if (count <= inplace_capacity) {
        // Move back to inline storage
        // We need a manual loop to switch the active union member in consteval context
        // At runtime this should become equivalent to std::uninitialized_copy_n
        pointer    old_data = m_storage.data;
        const auto old_cap  = m_capacity;
        m_capacity          = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            m_storage.limbs[i] = old_data[i];
        }
        for (size_type i = count; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = 0;
        }
        free_limbs(old_data, old_cap);
    } else {
        // Reallocate to a smaller heap buffer
        const alloc_result allocation = alloc_limbs(count);
        copy_n_to_allocation(m_storage.data, count, allocation);
        free_limbs(m_storage.data, m_capacity);
        m_storage.data = allocation.ptr;
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
    }
}

// [big.int.unary]

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator+() const& -> basic_big_int {
    return *this;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator+() && noexcept -> basic_big_int {
    return std::move(*this);
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator-() const& -> basic_big_int {
    auto copy = *this;
    copy.negate();
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator-() && noexcept -> basic_big_int {
    auto copy = std::move(*this);
    copy.negate();
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator~() const& -> basic_big_int {
    // Bitwise operations emulate two's complement behavior,
    // where ~x is mathematically (-x - 1).
    auto copy = *this;
    copy.negate();
    --copy;
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator~() && -> basic_big_int {
    // See also the const& overload.
    // Unlike operator-, we cannot make this noexcept because the decrement may reallocate.
    auto copy = std::move(*this);
    copy.negate();
    --copy;
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator++() & -> basic_big_int& {
    if (is_negative()) {
        unchecked_decrement_magnitude();
        if (limb_count() == 1 && limb_ptr()[0] == 0) {
            unchecked_set_sign(false);
        }
    } else {
        unchecked_increment_magnitude();
    }
    return *this;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator++(int) & -> basic_big_int {
    auto copy = *this;
    ++(*this);
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator--() & -> basic_big_int& {
    if (is_negative()) {
        unchecked_increment_magnitude();
    } else {
        if (unchecked_decrement_magnitude()) {
            unchecked_set_sign(true);
        }
    }
    return *this;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator--(int) & -> basic_big_int {
    auto copy = *this;
    --(*this);
    return copy;
}

// [big.int.cmp]
template <class L, detail::common_big_int_type_with<L> R>
constexpr bool operator==(const L& lhs, const R& rhs) noexcept {
    if constexpr (detail::is_basic_big_int_v<L>) {
        if constexpr (detail::is_basic_big_int_v<R>) {
            return lhs.equals_big_int(rhs);
        } else {
            return lhs.equals_integer(rhs);
        }
    } else {
        static_assert(detail::is_basic_big_int_v<R>);
        return rhs.equals_integer(lhs);
    }
}

template <class L, detail::common_big_int_type_with<L> R>
constexpr std::strong_ordering operator<=>(const L& lhs, const R& rhs) noexcept {
    if constexpr (detail::is_basic_big_int_v<L>) {
        if constexpr (detail::is_basic_big_int_v<R>) {
            return lhs.compare_big_int(rhs);
        } else {
            return lhs.compare_integer(rhs);
        }
    } else {
        static_assert(detail::is_basic_big_int_v<R>);
        return detail::invert(rhs.compare_integer(lhs));
    }
}

template <std::size_t b, class A>
template <detail::cv_unqualified_arithmetic T, bool ignore_sign>
constexpr T basic_big_int<b, A>::to() const noexcept {
    if constexpr (std::is_same_v<T, bool>) {
        return !is_zero();
    } else {
        if constexpr (ignore_sign && has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return static_cast<T>(inplace_to_bit_uint());
            }
        } else if constexpr (has_inplace_to_bit_sint) {
            if (is_representation_inplace()) {
                return static_cast<T>(inplace_to_sbit_int());
            }
        }
        if constexpr (std::is_floating_point_v<T>) {
            const bool negative = !ignore_sign && is_negative();
            // If the value exceeds the maximum finite value of T, return infinity.
            // See P3899R1.
            if (width_mag() >= static_cast<std::size_t>(std::numeric_limits<T>::max_exponent)) {
                return negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
            }
            constexpr T       two64 = static_cast<T>(1ULL << 32) * static_cast<T>(1ULL << 32);
            T                 result{0};
            const auto* const limbs = limb_ptr();
            for (std::size_t i = limb_count(); i != 0;) {
                --i;
                result = result * two64 + static_cast<T>(limbs[i]);
            }
            return negative ? -result : result;
        } else {
            using U = std::make_unsigned_t<T>;
            U                 mag{0};
            constexpr auto    n     = detail::div_to_pos_inf(sizeof(U), sizeof(limb_type));
            const auto* const limbs = limb_ptr();
            for (std::size_t i = 0; i < std::min(n, static_cast<std::size_t>(limb_count())); ++i) {
                mag |= static_cast<U>(limbs[i]) << (i * bits_per_limb);
            }
            if constexpr (ignore_sign) {
                return mag;
            } else {
                return static_cast<T>(is_negative() ? ~mag + U{1} : mag);
            }
        }
    }
}

namespace detail {

// Converts a given unsigned integer to a `std::array<uint_multiprecision_t, N>`,
// where `N` is sufficiently large to store the value of `x`.
template <unsigned_integer T>
[[nodiscard]] constexpr auto to_limbs(const T x) noexcept {
    constexpr std::size_t bits_per_limb = width_v<uint_multiprecision_t>;
    constexpr std::size_t limb_count    = div_to_pos_inf(width_v<T>, bits_per_limb);
    static_assert(limb_count != 0);
    using Result                         = std::array<uint_multiprecision_t, limb_count>;
    constexpr bool eligible_for_bit_cast = sizeof(Result) == sizeof(T) && //
                                           width_v<T> % bits_per_limb == 0 &&
                                           std::endian::native == std::endian::little;
    if constexpr (eligible_for_bit_cast) {
        // While `std::bit_cast` should be the fastest form of conversion
        // (especially in constant evaluation and on debug builds),
        // many conditions must be met.
        // For example, `_BitInt(100)` is not eligible due to padding bits,
        // (this could change with a `std::bit_cast_clear_padding` function in the future)
        // `_BitInt(32)` is too small to be eligible on 64-bit, and
        // `_BitInt(128)` is eligible, but (as in all other cases),
        // only on little-endian architectures.
        return std::bit_cast<Result>(x);
    } else {
        Result result;
        for (std::size_t i = 0; i < limb_count; ++i) {
            result[i] = static_cast<uint_multiprecision_t>(x >> (i * bits_per_limb));
        }
        return result;
    }
}

// Creates a span with fixed extent rather than dynamic extent,
// while deducing the size from the argument.
// This is useful for all kinds of binary operations between `big_int` and integers,
// where the integer is converted to a fixed amount of limbs.
// That fixed amount is often just `1`,
// which can be special-cased using `if constexpr` in various algorithms,
// and even without special casing, a fixed extent assists in constant folding/propagation.
template <class T, std::size_t N>
[[nodiscard]] constexpr std::span<const T, N> to_fixed_span(const std::array<T, N>& arr) noexcept {
    return std::span<const T, N>(arr);
}

// Three-way compares the magnitudes represented by two limb spans (little-endian, zero-padded).
// Treats both as non-negative; callers layer sign handling on top.
// Split on which side is longer so each branch carries only one high-tail scan.
// When an extent is not `dynamic_extent`, `.size()` is a compile-time constant,
// so the loops are easier to unroll.
template <std::size_t extent_a, std::size_t extent_b>
[[nodiscard]] constexpr std::strong_ordering
compare_limb_magnitudes(const std::span<const uint_multiprecision_t, extent_a> a,
                        const std::span<const uint_multiprecision_t, extent_b> b) noexcept {
    if (a.size() > b.size()) {
        // If there are more significant nonzero digits in `a`, it is greater.
        // Decimal example: 123 > 23
        for (std::size_t i = a.size(); i-- > b.size();) {
            if (a[i] != 0) {
                return std::strong_ordering::greater;
            }
        }
        // Compare the common digits from most to least significant.
        for (std::size_t i = b.size(); i-- > 0;) {
            const auto result = a[i] <=> b[i];
            if (std::is_neq(result)) {
                return result;
            }
        }
    } else {
        for (std::size_t i = b.size(); i-- > a.size();) {
            if (b[i] != 0) {
                return std::strong_ordering::less;
            }
        }
        for (std::size_t i = a.size(); i-- > 0;) {
            const auto result = a[i] <=> b[i];
            if (std::is_neq(result)) {
                return result;
            }
        }
    }
    return std::strong_ordering::equal;
}

// Convenience macro that describes one of eight forms of binary operation
// that any binary operation for `big_int` can take.
enum struct binary_op_form : unsigned char {
    // Both sides are movable.
    // Typically, the operations is performed by mutating the integer
    // with the most capacity.
    move_move,
    // Only the left side is movable, and its allocation is reused.
    // The left side is also a `basic_big_int`, but not movable.
    move_copy,
    // Only the right side is movable, and its allocation is reused.
    // The right side is also a `basic_big_int`, but not movable.
    copy_move,
    // Neither side is movable, so a fresh `basic_big_int` is created.
    copy_copy,
    // The left side is movable, and its allocation is reused.
    // The right side is a fundamental integer (of any cvref qualification).
    move_int,
    // The right side is movable, and its allocation is reused.
    // The left side is a fundamental integer (of any cvref qualification).
    int_move,
    // A fresh `basic_big_int` is created because the left side is not movable.
    // The right side is a fundamental integer (of any cvref qualification).
    copy_int,
    // A fresh `basic_big_int` is created because the left side is not movable.
    // The left side is a fundamental integer (of any cvref qualification).
    int_copy,
};

// Variable template that classifies the form of binary operation
// into one of the `binary_op_form` enumerators,
// using the `L` and `R` template parameters deduced from the forwarding references
// in one of the binary operations.
template <class L, class R>
inline constexpr binary_op_form classify_form_v = [] {
    using LT                                   = std::remove_cvref_t<L>;
    using RT                                   = std::remove_cvref_t<R>;
    [[maybe_unused]] constexpr bool copy_left  = std::is_reference_v<L>;
    [[maybe_unused]] constexpr bool copy_right = std::is_reference_v<R>;

    if constexpr (is_basic_big_int_v<LT> && is_basic_big_int_v<RT>) {
        return copy_left && copy_right    ? binary_op_form::copy_copy
               : !copy_left && copy_right ? binary_op_form::move_copy
               : copy_left && !copy_right ? binary_op_form::copy_move
                                          : binary_op_form::move_move;
    } else if constexpr (is_basic_big_int_v<LT>) {
        return copy_left ? binary_op_form::copy_int : binary_op_form::move_int;
    } else if constexpr (is_basic_big_int_v<RT>) {
        return copy_right ? binary_op_form::int_copy : binary_op_form::int_move;
    } else {
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("Invalid case");
    }
}();

} // namespace detail

// [big.int.binary]
//
// The shared pattern for `operator+` and `operator-` is: build `Result r` from one
// operand (moving an rvalue `basic_big_int`'s storage, copying an lvalue, or
// constructing from a primitive), then fold the other operand in via `add_in_place`.
// `add_in_place` handles carry, borrow, trim, and sign normalization uniformly.
//
// Destination priority:
//   * both `basic_big_int` rvalues  -> move the one with more limbs (no subsequent grow)
//   * one  `basic_big_int` rvalue   -> move it
//   * both `basic_big_int` lvalues  -> copy the one with more limbs
//   * one  `basic_big_int` lvalue   -> copy it (the other side is a primitive)
//
// `common_big_int_type` only yields a type when both `basic_big_int` operands are the
// exact same `basic_big_int<b, A>` instantiation, so the operand type already matches
// `Result` and no explicit type-equality guard is needed.
template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator+(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    // In each of these branches we try to take the largest storage available
    // In the case that we do have to allocate, we automatically add in an extra limb,
    // otherwise we run the risk of a second allocation occurring a the end of addition
    // In the case that we are using inline storage we do not request an extra limb,
    // we defer that decision till as late as possible in case the addition result fits
    // into the static storage rather than having to allocate for no reason
    if constexpr (form == detail::binary_op_form::move_move) {
        if (x.limb_count() >= y.limb_count()) {
            Result r = std::move(x);
            r.add_in_place(y.representation(), y.is_negative());
            return r;
        }
        Result r = std::move(y);
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_copy) {
        Result r = std::move(x);
        r.add_in_place(y.representation(), y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_move) {
        Result r = std::move(y);
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_copy) {
        // Both lvalue `basic_big_int`s: copy the larger operand, then add the smaller.
        // For inline sources no extra space is requested, so the copy stays inline
        // The move to heap storage is deferred to add_in_place if required
        Result r;
        if (x.limb_count() >= y.limb_count()) {
            r.assign_value(x, !x.is_representation_inplace());
            r.add_in_place(y.representation(), y.is_negative());
            return r;
        }
        r.assign_value(y, !y.is_representation_inplace());
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        Result     r       = std::move(x);
        r.add_in_place(detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_move) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        Result     r       = std::move(y);
        r.add_in_place(detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        Result r;
        r.assign_value(x, !x.is_representation_inplace());
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.add_in_place(detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_copy) {
        Result r;
        r.assign_value(y, !y.is_representation_inplace());
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.add_in_place(detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    }
}

// `x - y` is implemented as `x + (-y)`: we flip the sign of the right-hand operand
// (without materializing a negated value) and dispatch through the same magnitude
// add/subtract core as `operator+`. Destination priority matches `operator+`:
//   * lhs-destination paths pass the other side's span with sign `!rhs_neg`
//   * rhs-destination paths `r.negate()` first (cheap XOR on the sign word),
//     then add the lhs side with its own sign, yielding `(-y) + x = x - y`
template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator-(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    // See `operator+` description of logic, as it is the same
    if constexpr (form == detail::binary_op_form::move_move) {
        if (x.limb_count() >= y.limb_count()) {
            Result r = std::move(x);
            r.add_in_place(y.representation(), !y.is_negative()); // r + (-y)
            return r;
        }
        Result r = std::move(y);
        r.negate();                                          // r = -y
        r.add_in_place(x.representation(), x.is_negative()); // (-y) + x = x - y
        return r;
    } else if constexpr (form == detail::binary_op_form::move_copy) {
        Result r = std::move(x);
        r.add_in_place(y.representation(), !y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_move) {
        // `r = -y; r += x` gives `x - y`.
        Result r = -std::move(y);
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_copy) {
        // Both lvalue `basic_big_int`s: copy the larger, then subtract the smaller.
        // For inline sources no extra space is requested, so the copy stays inline.
        Result r;
        if (x.limb_count() >= y.limb_count()) {
            r.assign_value(x, !x.is_representation_inplace());
            r.add_in_place(y.representation(), !y.is_negative());
            return r;
        }
        r.assign_value(y, !y.is_representation_inplace());
        r.negate();
        r.add_in_place(x.representation(), x.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        Result     r       = std::move(x);
        r.add_in_place(detail::to_fixed_span(y_limbs), !detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_move) {
        // `r = -y; r += x` gives `x - y`.
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        Result     r       = -std::move(y);
        r.add_in_place(detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        Result r;
        r.assign_value(x, !x.is_representation_inplace());
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.add_in_place(detail::to_fixed_span(y_limbs), !detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_copy) {
        Result r;
        r.assign_value(y, !y.is_representation_inplace());
        r.negate();
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.add_in_place(detail::to_fixed_span(x_limbs), detail::integer_signbit(x));
        return r;
    }
}

template <std::size_t b, class A>
template <detail::bitwise_op op, class L, class R>
constexpr detail::common_big_int_type<L, R> basic_big_int<b, A>::bitwise_impl(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        return Result::template dispatch_bitwise<op>(
            x.representation(), x.is_negative(), y.representation(), y.is_negative());
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        return Result::template dispatch_bitwise<op>(
            x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
    } else {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        return Result::template dispatch_bitwise<op>(
            detail::to_fixed_span(x_limbs), detail::integer_signbit(x), y.representation(), y.is_negative());
    }
}

template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator&(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    return Result::template bitwise_impl<detail::bitwise_op::and_>(std::forward<L>(x), std::forward<R>(y));
}

template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator|(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    return Result::template bitwise_impl<detail::bitwise_op::or_>(std::forward<L>(x), std::forward<R>(y));
}

template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator^(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    return Result::template bitwise_impl<detail::bitwise_op::xor_>(std::forward<L>(x), std::forward<R>(y));
}

template <class T, detail::signed_or_unsigned S>
    requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
constexpr std::remove_cvref_t<T> operator<<(T&& x, const S s) {
    using Result        = std::remove_cvref_t<T>;
    using shift_type    = typename Result::shift_type;
    constexpr auto form = detail::classify_form_v<T, S>;

    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::make_unsigned_t<S>>(s) <= Result::shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= Result::shift_max);
    }
    const auto shift = static_cast<shift_type>(s);

    if constexpr (form == detail::binary_op_form::move_int) {
        // rvalue: shift in place, no copy needed.
        Result r = std::move(x);
        r.shift_left(shift);
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        // lvalue: use assign_value with headroom so shift_left doesn't reallocate.
        const shift_type shifted_limbs = shift / Result::bits_per_limb;
        const shift_type shifted_bits  = shift % Result::bits_per_limb;
        const bool       needs_extra =
            shifted_bits != 0 &&
            static_cast<shift_type>(std::countl_zero(x.limb_ptr()[x.limb_count() - 1])) < shifted_bits;
        const std::size_t headroom = shifted_limbs + static_cast<std::size_t>(needs_extra);

        Result r;
        r.assign_value(x, headroom);
        r.shift_left(shift);
        return r;
    }
}

template <class T, detail::signed_or_unsigned S>
    requires detail::is_basic_big_int_v<std::remove_cvref_t<T>>
constexpr std::remove_cvref_t<T> operator>>(T&& x, const S s) {
    using Result        = std::remove_cvref_t<T>;
    using shift_type    = typename Result::shift_type;
    constexpr auto form = detail::classify_form_v<T, S>;

    if constexpr (std::is_signed_v<S>) {
        BEMAN_BIG_INT_DEBUG_ASSERT(s >= 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::make_unsigned_t<S>>(s) <= Result::shift_max);
    } else {
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= Result::shift_max);
    }
    const auto shift = static_cast<shift_type>(s);

    if constexpr (form == detail::binary_op_form::move_int) {
        // rvalue: shift in place, no copy needed.
        Result r = std::move(x);
        r.shift_right(shift);
        return r;
    } else if constexpr (form == detail::binary_op_form::copy_int) {
        // lvalue: copy then shift. Two fast paths avoid copying bits that are
        // about to be discarded anyway
        //
        // First is the case where the value is discarded entirely
        // All we need to do is copy the sign
        //
        // Second is the case where some of the value is discarded
        // For positive values this is a simple copy what we need and make a final limb internal shift
        // For negative values we need to account for the rounding at the end
        //
        // If neither of these situations applies, make a full copy and shift.

        using limb_type = uint_multiprecision_t;

        const shift_type shifted_limbs = shift / Result::bits_per_limb;
        const shift_type shifted_bits  = shift % Result::bits_per_limb;
        const shift_type x_width_mag   = static_cast<shift_type>(x.width_mag());

        // Case 1: Everything is discarded except the sign
        if (shift >= x_width_mag) {
            if (x.is_negative()) {
                return Result{-1};
            }
            return Result{};
        }

        const limb_type* const src_limbs = x.limb_ptr();
        const shift_type       src_count = static_cast<shift_type>(x.limb_count());

        // Number of limbs the shifted result actually occupies. Smaller than
        // `src_count` exactly when at least one source limb (or the top
        // limb's surviving bits) shrinks away.
        const auto new_count =
            detail::div_to_pos_inf(x_width_mag - shift, static_cast<shift_type>(Result::bits_per_limb));

        // Case 2: Result is strictly smaller than the source
        if (new_count < src_count) {
            const shift_type src_offset = shifted_limbs;

            Result r;
            r.reserve(new_count);
            limb_type* const dst = r.limb_ptr();

            if (shifted_bits == 0) {
                std::copy_n(src_limbs + src_offset, new_count, dst);
            } else {
                // Funnel-shift directly from source limbs into `dst`.
                // Handles the case where we can theoretically go from heap -> inline storage with our result
                for (shift_type i = 0; i < new_count; ++i) {
                    const shift_type src_idx = src_offset + i;
                    const limb_type  low     = src_limbs[src_idx];
                    const limb_type  high    = (src_idx + 1 < src_count) ? src_limbs[src_idx + 1] : limb_type{0};
                    const detail::wide<limb_type> all_bits{.low_bits = low, .high_bits = high};
                    dst[i] = detail::funnel_shr(all_bits, static_cast<unsigned int>(shifted_bits));
                }
            }
            r.unchecked_set_limb_count(static_cast<std::uint32_t>(new_count));

            if (x.is_negative()) {
                // Mirror `shift_right`'s rounding-toward-negative-infinity
                // We have to account even for the bits that will never be copied
                bool discarded_nonzero = false;
                for (shift_type i = 0; i < src_offset; ++i) {
                    if (src_limbs[i] != 0) {
                        discarded_nonzero = true;
                        break;
                    }
                }

                if (!discarded_nonzero && shifted_bits != 0) {
                    const auto mask = static_cast<limb_type>((limb_type{1} << shifted_bits) - 1);
                    if ((src_limbs[src_offset] & mask) != 0) {
                        discarded_nonzero = true;
                    }
                }

                r.unchecked_set_sign(true);
                if (discarded_nonzero) {
                    r.unchecked_increment_magnitude();
                }
            }
            return r;
        }

        // Case 3: Make a full copy and shift
        Result r;
        r.assign_value(x);
        r.shift_right(shift);
        return r;
    }
}

template <std::size_t b, class A>
template <detail::signed_or_unsigned Integer>
constexpr bool basic_big_int<b, A>::equals_integer(const Integer x) const noexcept {
    if constexpr (std::is_unsigned_v<Integer>) {
        if (is_negative()) {
            return false;
        }
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return inplace_to_bit_uint() == x;
            }
        }
        return equals_limbs(detail::to_fixed_span(detail::to_limbs(x)), false);
    } else {
        const auto limbs = detail::to_limbs(detail::uabs(x));
        return equals_limbs(detail::to_fixed_span(limbs), x < 0);
    }
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::equals_big_int(const basic_big_int& x) const noexcept {
    // We can do fancier things in the future, but this works for now.
    return equals_limbs(x.representation(), x.is_negative());
}

template <std::size_t b, class A>
template <std::size_t extent>
constexpr bool basic_big_int<b, A>::equals_limbs(const std::span<const uint_multiprecision_t, extent> limbs,
                                                 const bool limbs_negative) const noexcept {
    if (is_negative() != limbs_negative) {
        return false;
    }

    const auto* const   self = limb_ptr();
    const std::uint32_t lc   = limb_count();

    // Split on which side is longer so each branch carries only one tail-zero loop.
    // When extent != dynamic_extent, `limbs.size()` is a compile-time constant,
    // so these loops can be more easily unrolled.
    // We also don't need to do all three scans, just two for any given case.
    if (lc >= limbs.size()) {
        for (std::size_t i = 0; i < limbs.size(); ++i) {
            if (self[i] != limbs[i]) {
                return false;
            }
        }
        // Our own limbs can have additional ignored zeroes.
        for (std::size_t i = limbs.size(); i < lc; ++i) {
            if (self[i] != 0) {
                return false;
            }
        }
    } else {
        for (std::size_t i = 0; i < lc; ++i) {
            if (self[i] != limbs[i]) {
                return false;
            }
        }
        // The provided limbs can have additional ignored zeroes.
        for (std::size_t i = lc; i < limbs.size(); ++i) {
            if (limbs[i] != 0) {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t b, class A>
template <detail::signed_or_unsigned Integer>
constexpr std::strong_ordering basic_big_int<b, A>::compare_integer(const Integer x) const noexcept {
    if constexpr (std::is_unsigned_v<Integer>) {
        if (is_negative()) {
            return std::strong_ordering::less;
        }
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                return inplace_to_bit_uint() <=> x;
            }
        }
        return compare_limbs(detail::to_fixed_span(detail::to_limbs(x)), false);
    } else {
        if constexpr (has_inplace_to_bit_uint) {
            if (is_representation_inplace()) {
                const auto sign_compare = (x < 0) <=> is_negative();
                if (std::is_neq(sign_compare)) {
                    return sign_compare;
                }
                // For two-negative operands, bigger magnitude means a smaller
                // value, so swap the operand order of the magnitude compare.
                return is_negative() ? detail::uabs(x) <=> inplace_to_bit_uint()
                                     : inplace_to_bit_uint() <=> detail::uabs(x);
            }
        }
        const auto limbs = detail::to_limbs(detail::uabs(x));
        return compare_limbs(detail::to_fixed_span(limbs), x < 0);
    }
}

template <std::size_t b, class A>
constexpr std::strong_ordering basic_big_int<b, A>::compare_big_int(const basic_big_int& x) const noexcept {
    // We can do fancier things in the future, but this works for now.
    return compare_limbs(x.representation(), x.is_negative());
}

template <std::size_t b, class A>
template <std::size_t extent>
constexpr std::strong_ordering
basic_big_int<b, A>::compare_limbs(const std::span<const uint_multiprecision_t, extent> limbs,
                                   const bool limbs_negative) const noexcept {
    // A mismatch between signs lets us short-circuit without comparing the magnitudes.
    const auto sign_compare = limbs_negative <=> is_negative();
    if (std::is_neq(sign_compare)) {
        return sign_compare;
    }

    // Compute the ordering as if both operands were non-negative. For two-negative
    // operands we invert the result at the end, because a larger magnitude means a
    // smaller value.
    const auto magnitude_ordering = detail::compare_limb_magnitudes(representation(), limbs);

    return is_negative() ? detail::invert(magnitude_ordering) : magnitude_ordering;
}

// Adds `(other, other_neg)` into `*this` in place. Shared core for `operator+` and
// `operator-`: the caller chooses the destination (an rvalue operand's storage or a
// copy of an lvalue operand) and supplies the other side as a limb span + sign.
template <std::size_t b, class A>
template <std::size_t extent_other>
constexpr void basic_big_int<b, A>::add_in_place(const std::span<const uint_multiprecision_t, extent_other> other,
                                                 const bool other_neg) {
    const bool this_neg = is_negative();

    if (this_neg == other_neg) {
        // Same sign: add magnitudes limb-by-limb. Target sign stays `this_neg`.
        const std::uint32_t old_count = limb_count();
        const std::size_t   big       = std::max<std::size_t>(old_count, other.size());

        // `grow(big)` only allocates when `big` exceeds our current capacity;
        // otherwise it's a no-op and we stay in our existing buffer.
        grow(big);
        limb_type* limbs = limb_ptr();

        bool carry = false;
        for (std::size_t i = 0; i < big; ++i) {
            const limb_type li            = i < old_count ? limbs[i] : limb_type{0};
            const limb_type ri            = i < other.size() ? other[i] : limb_type{0};
            const auto [r_value, r_carry] = detail::carrying_add(li, ri, carry);
            limbs[i]                      = r_value;
            carry                         = r_carry;
        }
        unchecked_set_limb_count(static_cast<std::uint32_t>(big));

        // Only allocate for the extra top limb if the ripple carry has actually escaped.
        if (carry) {
            grow(big + 1);
            limb_ptr()[big] = limb_type{1};
            unchecked_set_limb_count(static_cast<std::uint32_t>(big + 1));
        }

        unchecked_set_sign(this_neg && !unchecked_is_magnitude_zero());
        return;
    }

    // Differing signs: subtract smaller magnitude from larger; take the sign of the
    // larger-magnitude operand.
    const auto magnitude_order = detail::compare_limb_magnitudes(representation(), other);

    if (std::is_gteq(magnitude_order)) {
        // `|*this| >= |other|`: compute `*this - other` in place. Target sign is `this_neg`.
        const std::uint32_t n     = limb_count();
        limb_type* const    limbs = limb_ptr();

        bool borrow = false;
        for (std::size_t i = 0; i < n; ++i) {
            const limb_type li             = limbs[i];
            const limb_type si             = i < other.size() ? other[i] : limb_type{0};
            const auto [r_value, r_borrow] = detail::borrowing_sub(li, si, borrow);
            limbs[i]                       = r_value;
            borrow                         = r_borrow;
        }
        // Having picked `*this` as the larger operand, the final borrow must be zero.
        BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);

        unchecked_trim_magnitude();
        unchecked_set_sign(this_neg && !unchecked_is_magnitude_zero());
        return;
    }

    // `|other| > |*this|`: compute `other - *this` into our buffer. Target sign is `other_neg`.
    const std::uint32_t old_count = limb_count();
    const std::size_t   n         = other.size();

    // Subtraction can never produce more limbs than the larger operand, so this grow is tight.
    grow(n);
    limb_type* const limbs = limb_ptr();

    bool borrow = false;
    for (std::size_t i = 0; i < n; ++i) {
        // Read our old limb at index `i` *before* overwriting it, so this loop is
        // aliasing-safe if `other` happens to point into our own limb buffer.
        const limb_type si             = i < old_count ? limbs[i] : limb_type{0};
        const limb_type oi             = other[i];
        const auto [r_value, r_borrow] = detail::borrowing_sub(oi, si, borrow);
        limbs[i]                       = r_value;
        borrow                         = r_borrow;
    }
    // Having picked `other` as the larger operand, the final borrow must be zero.
    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    unchecked_set_limb_count(static_cast<std::uint32_t>(n));
    unchecked_trim_magnitude();

    // `|other| > |*this|` strictly, so the magnitude is guaranteed nonzero.
    unchecked_set_sign(other_neg && !unchecked_is_magnitude_zero());
}

// Computes `a * b` and stores the result into `*this`.
template <std::size_t b, class A>
template <std::size_t extent_a, std::size_t extent_b>
constexpr void basic_big_int<b, A>::multiply_into(const std::span<const uint_multiprecision_t, extent_a> a,
                                                  const bool                                             a_neg,
                                                  const std::span<const uint_multiprecision_t, extent_b> b_span,
                                                  const bool                                             b_neg) {
    const auto a_trimmed = a.first(detail::trimmed_size(a));
    const auto b_trimmed = b_span.first(detail::trimmed_size(b_span));

    // Zero * anything = positive 0
    if (detail::is_span_zero(a_trimmed) || detail::is_span_zero(b_trimmed)) {
        set_zero();
        return;
    }

    const std::size_t result_size = a_trimmed.size() + b_trimmed.size();
    grow(result_size);
    std::fill_n(limb_ptr(), result_size, limb_type{0});

    std::span<uint_multiprecision_t> result_span{limb_ptr(), result_size};
    const std::size_t                sig = detail::multiply_dispatch(result_span, a_trimmed, b_trimmed, m_alloc);
    unchecked_set_limb_count(static_cast<std::uint32_t>(sig));
    unchecked_set_sign(a_neg != b_neg && !unchecked_is_magnitude_zero());
}

template <std::size_t b, class A>
template <detail::bitwise_op op, bool neg_left, bool neg_right, std::size_t extent_a, std::size_t extent_b>
constexpr basic_big_int<b, A>
basic_big_int<b, A>::make_bitwise_of_limbs(const std::span<const uint_multiprecision_t, extent_a> lhs,
                                           const std::span<const uint_multiprecision_t, extent_b> rhs) {
    constexpr bool res_neg = detail::eval_bitwise<op>(neg_left, neg_right);
    const auto     n       = [&]() -> std::size_t {
        if constexpr (op == detail::bitwise_op::and_) {
            if constexpr (!neg_left && !neg_right) {
                return std::min(lhs.size(), rhs.size());
            } else if constexpr (!neg_left) {
                return lhs.size();
            } else if constexpr (!neg_right) {
                return rhs.size();
            } else {
                return std::max(lhs.size(), rhs.size());
            }
        } else {
            return std::max(lhs.size(), rhs.size());
        }
    }();

    basic_big_int result;
    result.grow(n + static_cast<std::size_t>(res_neg));
    limb_type* const dst = result.limb_ptr();

    bool carry_l = neg_left;
    bool carry_r = neg_right;
    bool carry_o = res_neg;
    for (std::size_t i = 0; i < n; ++i) {
        limb_type l = i < lhs.size() ? lhs[i] : limb_type{0};
        limb_type r = i < rhs.size() ? rhs[i] : limb_type{0};
        if constexpr (neg_left) {
            l                 = ~l;
            auto [sum, carry] = detail::carrying_add(l, limb_type{0}, carry_l);
            l                 = sum;
            carry_l           = carry;
        }
        if constexpr (neg_right) {
            r                 = ~r;
            auto [sum, carry] = detail::carrying_add(r, limb_type{0}, carry_r);
            r                 = sum;
            carry_r           = carry;
        }
        limb_type res = detail::eval_bitwise<op>(l, r);
        if constexpr (res_neg) {
            res               = ~res;
            auto [sum, carry] = detail::carrying_add(res, limb_type{0}, carry_o);
            res               = sum;
            carry_o           = carry;
        }
        dst[i] = res;
    }
    // `carry_o` surviving all `n` limbs means the two's complement result was all zeros,
    // i.e. the bitwise result before negation was all-ones in every limb.
    // For starters, (2^64 - 1) ^ -1 = 0 in two's complement, result is -2^64,
    // which requires an extra limb of magnitude[0, 1] = 2^64.
    if constexpr (res_neg) {
        if (carry_o) {
            dst[n] = limb_type{1};
            result.unchecked_set_limb_count(static_cast<std::uint32_t>(n + 1));
        } else {
            result.unchecked_set_limb_count(static_cast<std::uint32_t>(n));
        }
    } else {
        result.unchecked_set_limb_count(static_cast<std::uint32_t>(n));
    }

    result.unchecked_trim_magnitude();
    result.unchecked_set_sign(res_neg && !result.unchecked_is_magnitude_zero());
    return result;
}

template <std::size_t b, class A>
template <detail::bitwise_op op, std::size_t extent_a, std::size_t extent_b>
constexpr basic_big_int<b, A>
basic_big_int<b, A>::dispatch_bitwise(const std::span<const uint_multiprecision_t, extent_a> lhs,
                                      const bool                                             lhs_neg,
                                      const std::span<const uint_multiprecision_t, extent_b> rhs,
                                      const bool                                             rhs_neg) {
    if (!lhs_neg && !rhs_neg) {
        return make_bitwise_of_limbs<op, false, false>(lhs, rhs);
    }
    if (lhs_neg && !rhs_neg) {
        return make_bitwise_of_limbs<op, true, false>(lhs, rhs);
    }
    if (!lhs_neg && rhs_neg) {
        return make_bitwise_of_limbs<op, false, true>(lhs, rhs);
    }
    return make_bitwise_of_limbs<op, true, true>(lhs, rhs);
}

// Since multiplication needs a fresh output buffer (the result has up to
// a_size + b_size limbs and cannot overlap either input), every path creates a new
// `Result` and calls `multiply_into`.
//
// TODO : This is a member function instead of a free function like add_in_place,
// because maybe this is a pessimistic view on our allocation requirements?
template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator*(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        Result r;
        if constexpr (Result::has_inplace_to_wide_bit_uint) {
            if (x.is_representation_inplace() && y.is_representation_inplace()) {
                const auto product = x.inplace_to_wide_bit_uint() * y.inplace_to_wide_bit_uint();
                r.assign_magnitude(product);
                if (product != 0 && x.is_negative() != y.is_negative()) {
                    r.unchecked_set_sign(true);
                }
                return r;
            }
        }
        r.multiply_into(x.representation(), x.is_negative(), y.representation(), y.is_negative());
        return r;
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        Result r;
        if constexpr (Result::has_inplace_to_wide_bit_uint) {
            if constexpr (detail::width_v<std::remove_cvref_t<R>> <= Result::inplace_bits) {
                if (x.is_representation_inplace()) {
                    const auto product = x.inplace_to_wide_bit_uint() * detail::uabs(y);
                    r.assign_magnitude(product);
                    if (product != 0 && x.is_negative() != detail::integer_signbit(y)) {
                        r.unchecked_set_sign(true);
                    }
                    return r;
                }
            }
        }
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.multiply_into(
            x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y));
        return r;
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        // Multiplication is commutative, so in the `int * big_int` case,
        // we delegate to `big_int * int`.
        // The unary plus operator produces a prvalue, which reduces template instantiations.
        return std::forward<R>(y) * +x;
    } else {
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("Unknown form of multiplication.");
    }
}

// Compound addition and subtraction
//
// `*this` is always the destination, so we skip the copy/move step that the free
// `operator+` / `operator-` need and fold `rhs` directly into our own storage via
// `add_in_place`.
template <std::size_t b, class A>
template <class T>
constexpr auto basic_big_int<b, A>::operator+=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        add_in_place(rhs.representation(), rhs.is_negative());
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        add_in_place(detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs));
    }
    return *this;
}

template <std::size_t b, class A>
template <class T>
constexpr auto basic_big_int<b, A>::operator-=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if (std::addressof(rhs) == this) {
            set_zero();
        } else {
            add_in_place(rhs.representation(), !rhs.is_negative());
        }
    } else {
        const auto rhs_limbs = detail::to_limbs(detail::uabs(rhs));
        add_in_place(detail::to_fixed_span(rhs_limbs), !detail::integer_signbit(rhs));
    }
    return *this;
}

// Compound multiplication assignment.
template <std::size_t b, class A>
template <class T>
constexpr auto basic_big_int<b, A>::operator*=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        // Move *this to a temp so the old limbs become a read-only input,
        // then multiply into a new *this.
        const basic_big_int temp = std::move(*this);
        *this                    = basic_big_int{};
        // `rhs` may alias `*this` (e.g. `y *= y`).
        // After the move, `rhs` still references the moved-from destination, so read both operands from `temp`.
        if (std::addressof(rhs) == this) {
            multiply_into(temp.representation(), temp.is_negative(), temp.representation(), temp.is_negative());
            return *this;
        }
        multiply_into(temp.representation(), temp.is_negative(), rhs.representation(), rhs.is_negative());
    } else {
        const basic_big_int temp = std::move(*this);
        *this                    = basic_big_int{};
        const auto rhs_limbs     = detail::to_limbs(detail::uabs(rhs));
        multiply_into(
            temp.representation(), temp.is_negative(), detail::to_fixed_span(rhs_limbs), detail::integer_signbit(rhs));
    }
    return *this;
}

// Single-limb divisor fast path from Knuth
// Avoids allocating a remainder scratch buffer and a `t` buffer: we just stream the quotient through `*this`'s
// limbs and carry a single remainder limb between iterations.
template <std::size_t b, class A>
template <std::size_t extent_a>
constexpr uint_multiprecision_t
basic_big_int<b, A>::divmod_into_short(const std::span<const uint_multiprecision_t, extent_a> dividend,
                                       const bool                                             dividend_neg,
                                       const uint_multiprecision_t                            divisor,
                                       const bool                                             divisor_neg,
                                       const detail::division_op                              op) {
    BEMAN_BIG_INT_ASSERT(divisor != 0);

    const bool want_quotient = op != detail::division_op::rem;
    const auto dividend_trim = dividend.first(detail::trimmed_size(dividend));
    const bool result_neg    = want_quotient ? (dividend_neg != divisor_neg) : dividend_neg;

    if (detail::is_span_zero(dividend_trim)) {
        limb_ptr()[0] = 0;
        unchecked_set_limb_count(1);
        unchecked_set_sign(false);
        return {};
    }

    // Single-limb dividend: native divide/modulo.
    if (dividend_trim.size() == 1) {
        const uint_multiprecision_t d = dividend_trim[0];
        limb_ptr()[0]                 = want_quotient ? (d / divisor) : (d % divisor);
        unchecked_set_limb_count(1);
        unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
        return op != detail::division_op::div ? d % divisor : 0;
    }

    // Stream the quotient through *this's limb buffer,
    // returning the scalar remainder.
    grow(dividend_trim.size());
    std::span<uint_multiprecision_t> quot_span{limb_ptr(), dividend_trim.size()};
    const uint_multiprecision_t      remainder = detail::divide_unsigned_short(quot_span, dividend_trim, divisor);

    if (want_quotient) {
        const std::size_t qsize =
            detail::trimmed_size(std::span<const uint_multiprecision_t>{limb_ptr(), dividend_trim.size()});
        unchecked_set_limb_count(static_cast<std::uint32_t>(qsize));
    } else {
        limb_ptr()[0] = remainder;
        unchecked_set_limb_count(1);
    }

    unchecked_set_sign(result_neg && !unchecked_is_magnitude_zero());
    return remainder;
}

// Generic divide/modulus dispatcher.
// Handles the four special cases:
//   1) zero divisor,
//   2) zero dividend,
//   3) |dividend| < |divisor|,
//   4) single-limb divisor
// before falling through to `detail::divide_unsigned`.
template <std::size_t b, class A>
template <std::size_t extent_a, std::size_t extent_b>
constexpr auto basic_big_int<b, A>::divmod_into(const std::span<const uint_multiprecision_t, extent_a> dividend,
                                                const bool                                             dividend_neg,
                                                const std::span<const uint_multiprecision_t, extent_b> divisor,
                                                const bool                                             divisor_neg,
                                                const detail::division_op op) -> basic_big_int {
    const auto dividend_trim = dividend.first(detail::trimmed_size(dividend));
    const auto divisor_trim  = divisor.first(detail::trimmed_size(divisor));

    BEMAN_BIG_INT_ASSERT(!detail::is_span_zero(divisor_trim));

    const bool want_quotient          = op != detail::division_op::rem;
    const bool unrounded_quotient_neg = want_quotient ? (dividend_neg != divisor_neg) : dividend_neg;

    if (detail::is_span_zero(dividend_trim)) {
        set_zero();
        return {};
    }

    const std::strong_ordering mag_cmp = detail::compare_unsigned_spans(dividend_trim, divisor_trim);
    if (mag_cmp == std::strong_ordering::less) {
        // |dividend| < |divisor|: quotient is 0, remainder is dividend.
        if (op == detail::division_op::div) {
            set_zero();
            return {};
        }

        grow(dividend_trim.size());
        std::copy(dividend_trim.begin(), dividend_trim.end(), limb_ptr());
        unchecked_set_limb_count(static_cast<std::uint32_t>(dividend_trim.size()));
        unchecked_set_sign(dividend_neg && !unchecked_is_magnitude_zero());
        if (op == detail::division_op::rem) {
            return {};
        }

        BEMAN_BIG_INT_DEBUG_ASSERT(op == detail::division_op::div_rem);
        basic_big_int result = std::move(*this);
        set_zero();
        return result;
    }

    // Single-limb divisor fast path.
    if (divisor_trim.size() == 1) {
        const uint_multiprecision_t rem_limb =
            divmod_into_short(dividend_trim, dividend_neg, divisor_trim[0], divisor_neg, op);
        if (rem_limb == 0) {
            return {};
        }
        basic_big_int remainder = rem_limb;
        remainder.unchecked_set_sign(dividend_neg);
        return remainder;
    }

    // Multi-limb long division.
    const std::size_t n_div = dividend_trim.size();
    const std::size_t m_div = divisor_trim.size();
    const std::size_t q_cap = n_div - m_div + 1;
    const std::size_t r_cap = n_div + 1;
    const std::size_t t_cap = n_div + 1;

    if (want_quotient) {
        // *this will hold the quotient.
        // Remainder and `t` go in scratch.
        grow(q_cap);
        std::fill_n(limb_ptr(), q_cap, limb_type{0});

        detail::scratch_allocator<allocator_type> scratch(r_cap + t_cap, m_alloc);
        const std::span<uint_multiprecision_t>    rem_span = scratch.allocate(r_cap);

        detail::divide_unsigned(
            std::span<uint_multiprecision_t>{limb_ptr(), q_cap}, rem_span, dividend_trim, divisor_trim, scratch);

        const std::size_t qsize = detail::trimmed_size(std::span<const uint_multiprecision_t>{limb_ptr(), q_cap});
        unchecked_set_limb_count(static_cast<std::uint32_t>(qsize));
        unchecked_set_sign(unrounded_quotient_neg && !unchecked_is_magnitude_zero());
        if (op == detail::division_op::div) {
            return {};
        }

        BEMAN_BIG_INT_DEBUG_ASSERT(op == detail::division_op::div_rem);
        basic_big_int rem(rem_span.data(), rem_span.data() + rem_span.size());
        rem.unchecked_set_sign(dividend_neg && !rem.unchecked_is_magnitude_zero());
        return rem;
    }

    // *this will hold the remainder.
    // Quotient and `t` go in scratch.
    grow(r_cap);
    std::fill_n(limb_ptr(), r_cap, limb_type{0});

    detail::scratch_allocator<allocator_type> scratch(q_cap + t_cap, m_alloc);
    const std::span<uint_multiprecision_t>    quot_span = scratch.allocate(q_cap);

    detail::divide_unsigned(
        quot_span, std::span<uint_multiprecision_t>{limb_ptr(), r_cap}, dividend_trim, divisor_trim, scratch);

    const std::size_t rsize = detail::trimmed_size(std::span<const uint_multiprecision_t>{limb_ptr(), r_cap});
    unchecked_set_limb_count(static_cast<std::uint32_t>(rsize));
    unchecked_set_sign(dividend_neg && !unchecked_is_magnitude_zero());
    return {};
}

// Simultaneously computes the quotient and remainder of a division,
// rounded towards zero.
template <class L, class R>
[[nodiscard]] constexpr div_result<detail::common_big_int_type<L, R>> div_rem_to_zero(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;
    constexpr auto op   = detail::division_op::div_rem;

    Result quo;
    Result rem;
    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        rem = quo.divmod_into(x.representation(), x.is_negative(), y.representation(), y.is_negative(), op);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        rem                = quo.divmod_into(
            x.representation(), x.is_negative(), detail::to_fixed_span(y_limbs), detail::integer_signbit(y), op);
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        rem                = quo.divmod_into(
            detail::to_fixed_span(x_limbs), detail::integer_signbit(x), y.representation(), y.is_negative(), op);
    }
    return {std::move(quo), std::move(rem)};
}

template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator/(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    Result r;
    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        r.divmod_into(
            x.representation(), x.is_negative(), y.representation(), y.is_negative(), detail::division_op::div);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.divmod_into(x.representation(),
                      x.is_negative(),
                      detail::to_fixed_span(y_limbs),
                      detail::integer_signbit(y),
                      detail::division_op::div);
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.divmod_into(detail::to_fixed_span(x_limbs),
                      detail::integer_signbit(x),
                      y.representation(),
                      y.is_negative(),
                      detail::division_op::div);
    }
    return r;
}

template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator%(L&& x, R&& y) {
    using Result        = detail::common_big_int_type<L, R>;
    constexpr auto form = detail::classify_form_v<L, R>;

    Result r;
    if constexpr (form == detail::binary_op_form::move_move || form == detail::binary_op_form::move_copy ||
                  form == detail::binary_op_form::copy_move || form == detail::binary_op_form::copy_copy) {
        r.divmod_into(
            x.representation(), x.is_negative(), y.representation(), y.is_negative(), detail::division_op::rem);
    } else if constexpr (form == detail::binary_op_form::move_int || form == detail::binary_op_form::copy_int) {
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        r.divmod_into(x.representation(),
                      x.is_negative(),
                      detail::to_fixed_span(y_limbs),
                      detail::integer_signbit(y),
                      detail::division_op::rem);
    } else if constexpr (form == detail::binary_op_form::int_move || form == detail::binary_op_form::int_copy) {
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        r.divmod_into(detail::to_fixed_span(x_limbs),
                      detail::integer_signbit(x),
                      y.representation(),
                      y.is_negative(),
                      detail::division_op::rem);
    }
    return r;
}

// Compound division and modulus assignments.
template <std::size_t b, class A>
template <class T>
constexpr auto basic_big_int<b, A>::operator/=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, basic_big_int>) {
            // Self-division: `std::move(*this)` below would zero `rhs` when &rhs == this.
            if (&rhs == this) {
                BEMAN_BIG_INT_ASSERT(!is_zero());
                *this = basic_big_int{1};
                return *this;
            }
        }
        const basic_big_int temp = std::move(*this);
        *this                    = basic_big_int{};
        divmod_into(temp.representation(),
                    temp.is_negative(),
                    rhs.representation(),
                    rhs.is_negative(),
                    detail::division_op::div);
    } else {
        const basic_big_int temp = std::move(*this);
        *this                    = basic_big_int{};
        const auto rhs_limbs     = detail::to_limbs(detail::uabs(rhs));
        divmod_into(temp.representation(),
                    temp.is_negative(),
                    detail::to_fixed_span(rhs_limbs),
                    detail::integer_signbit(rhs),
                    detail::division_op::div);
    }
    return *this;
}

template <std::size_t b, class A>
template <class T>
constexpr auto basic_big_int<b, A>::operator%=(T&& rhs) -> basic_big_int&
    requires detail::common_big_int_type_with<T, basic_big_int>
{
    if constexpr (detail::is_basic_big_int_v<std::remove_cvref_t<T>>) {
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, basic_big_int>) {
            // Self-modulus: `std::move(*this)` below would zero `rhs` when &rhs == this.
            if (&rhs == this) {
                BEMAN_BIG_INT_ASSERT(!is_zero());
                *this = basic_big_int{};
                return *this;
            }
        }
        const basic_big_int temp = std::move(*this);
        *this                    = basic_big_int{};
        divmod_into(temp.representation(),
                    temp.is_negative(),
                    rhs.representation(),
                    rhs.is_negative(),
                    detail::division_op::rem);
    } else {
        const basic_big_int temp = std::move(*this);
        *this                    = basic_big_int{};
        const auto rhs_limbs     = detail::to_limbs(detail::uabs(rhs));
        divmod_into(temp.representation(),
                    temp.is_negative(),
                    detail::to_fixed_span(rhs_limbs),
                    detail::integer_signbit(rhs),
                    detail::division_op::rem);
    }
    return *this;
}

// private helpers

template <std::size_t b, class A>
template <detail::unsigned_integer T>
constexpr void basic_big_int<b, A>::assign_magnitude(T value) noexcept {
    constexpr size_type value_limbs = detail::div_to_pos_inf(detail::width_v<T>, bits_per_limb);
    if constexpr (value_limbs == 1) {
        limb_ptr()[0] = static_cast<limb_type>(value);
        unchecked_set_limb_count(1);
    } else {
        if constexpr (value_limbs > inplace_capacity) {
            grow(value_limbs);
        }
        auto* const dst = limb_ptr();
        for (size_type i = 0; i < value_limbs; ++i) {
            dst[i] = static_cast<limb_type>(value);
            value >>= bits_per_limb;
        }
        unchecked_set_limb_count(static_cast<std::uint32_t>(value_limbs));
        unchecked_trim_magnitude();
    }
}

template <std::size_t b, class A>
template <detail::cv_unqualified_floating_point F>
constexpr void basic_big_int<b, A>::assign_from_float(const F value) noexcept {
    using traits = detail::ieee_traits<F>;
#ifdef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
    static_assert(!std::is_same_v<F, long double>, "long double is not supported on this platform");
#endif
    BEMAN_BIG_INT_ASSERT(std::isfinite(value));

    // In the happiest case, we can use the intrinsic conversion from binary32 to uint128_t.
    // Compilers have optimized routines for this,
    // and this approach is very fast during constant evaluation.
#ifdef BEMAN_BIG_INT_HAS_INT128
    if constexpr (traits::width == 32 && std::is_convertible_v<F, detail::uint128_t>) {
        assign_magnitude(static_cast<detail::uint128_t>(detail::constexpr_fabs(value)));
        unchecked_set_sign(value < 0);
    }
#endif

    // In all other cases, some decomposition is necessary.
    const auto [sign, exponent, mantissa] = detail::decompose_float(value);

    // There are only fractional bits, and since we truncate, the value is zero.
    if (exponent < -traits::mantissa_bits) {
        set_zero();
        return;
    }

    // The exponent is slightly negative, which can be emulated using a right shift.
    if (exponent < 0) {
        assign_magnitude(mantissa >> -exponent);
        unchecked_set_sign(sign);
        return;
    }

    assign_magnitude(mantissa);
    shift_left(static_cast<shift_type>(exponent));
    unchecked_set_sign(sign);
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::alloc_limbs(const size_type n) -> alloc_result {
    BEMAN_BIG_INT_ASSERT(n != 0);
#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
    return alloc_traits::allocate_at_least(m_alloc, n);
#else
    return {.ptr = alloc_traits::allocate(m_alloc, n), .count = n};
#endif
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::free_limbs(pointer p, const size_type n) {
    BEMAN_BIG_INT_ASSERT(p != nullptr);
    BEMAN_BIG_INT_ASSERT(n != 0);
    // Need to suppress known false positive warning.
    // See also https://github.com/llvm/llvm-project/issues/53007
    BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
    BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfree-nonheap-object")
    alloc_traits::deallocate(m_alloc, p, n);
    BEMAN_BIG_INT_DIAGNOSTIC_POP()
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::free_storage() {
    if (!is_representation_inplace()) {
        free_limbs(m_storage.data, m_capacity);
    }
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::grow(const size_type limbs_needed) {
    const size_type current_cap = is_representation_inplace() ? inplace_capacity : m_capacity;
    if (limbs_needed <= current_cap) {
        return;
    }

    // libstdc++ and libc++ normally double storage each allocation
    // MSVC does 1.5x instead of 2x
    const size_type    new_cap    = std::max(limbs_needed, 2 * current_cap);
    const alloc_result allocation = alloc_limbs(new_cap);
    copy_n_to_allocation(limb_ptr(), limb_count(), allocation);

    free_storage();

    m_storage.data = allocation.ptr;
    m_capacity     = static_cast<std::uint32_t>(allocation.count);
}

template <std::size_t b, class A>
constexpr void
basic_big_int<b, A>::copy_n_to_allocation(const limb_type* const p, const size_type n, const alloc_result out) {
    BEMAN_BIG_INT_ASSERT(p != nullptr);
    BEMAN_BIG_INT_ASSERT(out.ptr != nullptr);
    BEMAN_BIG_INT_ASSERT(n <= out.count);
// If __cpp_lib_raw_memory_algorithms is available,
// we don't need to differentiate between constant evaluation and runtime.
// Even when we need this fallback case,
// it is always important that all elements in the allocation are initialized
// because we don't keep track of "requested" vs "received" capacity
// (these may not be the same with allocate_at_least).
#if !defined(__cpp_lib_raw_memory_algorithms) || __cpp_lib_raw_memory_algorithms < 202411L
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
#endif
        std::uninitialized_copy_n(p, n, out.ptr);
        std::uninitialized_value_construct_n(out.ptr + n, out.count - n);
#if !defined(__cpp_lib_raw_memory_algorithms) || __cpp_lib_raw_memory_algorithms < 202411L
    } else {
        for (size_type i = 0; i < n; ++i) {
            std::construct_at(out.ptr + i, p[i]);
        }
        for (size_type i = n; i < out.count; ++i) {
            std::construct_at(out.ptr + i);
        }
    }
#endif
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::push_back_limb(limb_type limb) {
    const auto count = limb_count();
    if (count >= (is_representation_inplace() ? inplace_capacity : m_capacity)) {
        grow(count + 1); // exponential growth
    }
    limb_ptr()[count] = limb;
    unchecked_set_limb_count(static_cast<std::uint32_t>(count + 1));
}

// Standard public alias for defaulted type
using big_int = basic_big_int<64, std::allocator<uint_multiprecision_t>>;

// [big.int.literal]
namespace detail {

[[nodiscard]] constexpr int digit_value_naive(const char c) noexcept {
    static_assert('A' == 0x41 && 'Z' == 0x5a, "This function requires the ordinary literal encoding to be ASCII.");
    return '0' <= c && c <= '9'   ? c - '0'
           : 'A' <= c && c <= 'Z' ? c - 'A' + 10
           : 'a' <= c && c <= 'z' ? c - 'a' + 10
                                  : -1;
}

inline constexpr auto digit_value_table = []() consteval {
    static_assert(CHAR_BIT == 8);
    std::array<signed char, 256> result;
    for (std::size_t i = 0; i < 256; ++i) {
        result[i] = static_cast<signed char>(digit_value_naive(static_cast<char>(i)));
    }
    return result;
}();

// Returns the value of a digit encoded in `c` using the ordinary literal encoding.
// For example, `digit_value('f')` returns `15`, which is consistent with `std::from_chars`.
// Returns `-1` if the `c` is not alphanumeric.
[[nodiscard]] constexpr int digit_value(const char c) noexcept {
    return digit_value_table[static_cast<std::size_t>(c)];
}

[[nodiscard]] consteval unsigned char limb_max_input_digits_naive(const int base) {
    BEMAN_BIG_INT_ASSERT(base >= 2);
    const auto ubase = static_cast<uint_multiprecision_t>(base);
    if (std::has_single_bit(ubase)) {
        return detail::width_v<uint_multiprecision_t> / static_cast<unsigned char>(std::countr_zero(ubase));
    }

    uint_multiprecision_t x      = 1;
    int                   result = 0;
    while (true) {
        const auto [product, overflow] = overflowing_mul(x, ubase);
        if (overflow) {
            return static_cast<unsigned char>(product == 0 ? result + 1 : result);
        }
        x = product;
        ++result;
        BEMAN_BIG_INT_ASSERT(product != 0);
    }
}

inline constexpr auto limb_max_input_digits_table = []() consteval {
    std::array<unsigned char, 37> result{};
    for (std::size_t i = 2; i < result.size(); ++i) {
        result[i] = limb_max_input_digits_naive(static_cast<int>(i));
    }
    return result;
}();

// Returns the amount of digits that `uint_multiprecision_t` can represent in the given base.
// Mathematically, this is `floor(log(pow(2, width_v<uint_multiprecision_t>)) / log(base))`.
[[nodiscard]] constexpr int limb_max_input_digits(const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    return limb_max_input_digits_table[std::size_t(base)];
}

[[nodiscard]] consteval uint_multiprecision_t limb_pow_naive(const uint_multiprecision_t x, const int y) {
    uint_multiprecision_t result = 1;
    for (int i = 0; i < y; ++i) {
        result *= x;
    }
    return result;
}

inline constexpr auto limb_max_power_table = []() consteval {
    std::array<uint_multiprecision_t, 37> result{};
    for (std::size_t i = 2; i < result.size(); ++i) {
        const int max_exponent = limb_max_input_digits(static_cast<int>(i));
        result[i]              = limb_pow_naive(i, max_exponent);
    }
    return result;
}();

// Returns the greatest power of `base` representable in `uint_multiprecision_t`,
// or zero if the next greater power is exactly `pow(2, width_v<uint_multiprecision_t>)`.
//
// A result of zero essentially communicates that no bit of `std::uint64_t` is wasted,
// such as in the base-2 or base-16 case.
[[nodiscard]] constexpr uint_multiprecision_t limb_max_power(const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    return limb_max_power_table[std::size_t(base)];
}

// Fixed-point ceil(log2(base)) coefficients in Q7.4 format.
// Each entry stores ceil(log2(base) * 16), i.e. ceil(log2(base)) represented with 4 fractional bits.
inline constexpr std::array<unsigned short, 37> approximate_ceil_mul_log2_q7_4_table{
    0x00, 0x00, 0x10, 0x1A, 0x20, 0x26, 0x2A, 0x2D, 0x30, 0x33, 0x36, 0x38, 0x3A, 0x3C, 0x3D, 0x3F, 0x40, 0x42, 0x43,
    0x44, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4D, 0x4E, 0x4F, 0x50, 0x50, 0x51, 0x52, 0x53, 0x53,
};

// Computes a fast approximation of the amount of bits
// required to represent an integer in base `base` with `digit_count` digits.
// Mathematically, this is `ceil(digit_count * log2(base))`.
// For powers of two, the exact result is returned.
[[nodiscard]] constexpr std::size_t approximate_ceil_mul_log2(const std::size_t digit_count, const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    constexpr std::size_t fractional_bits = 4;
    constexpr std::size_t fractional_mask = (std::size_t{1} << fractional_bits) - 1;

    const auto        coeff = static_cast<std::size_t>(approximate_ceil_mul_log2_q7_4_table[std::size_t(base)]);
    const std::size_t scaled_result = digit_count * coeff;
    return (scaled_result >> fractional_bits) + static_cast<std::size_t>((scaled_result & fractional_mask) != 0);
}

// Fixed-point ceil(1/log2(base)) coefficients in Q0.8 format.
// Each entry stores ceil(256 / log2(base)) as an unsigned char.
// Indices 0, 1, and 2 are unused; base 2 is handled separately in approximate_ceil_div_log2.
inline constexpr std::array<unsigned char, 37> approximate_ceil_div_log2_q0_8_table{
    0x00, 0x00, 0x00, 0xA2, 0x80, 0x6F, 0x64, 0x5C, 0x56, 0x51, 0x4E, 0x4B, 0x48, 0x46, 0x44, 0x42, 0x40, 0x3F, 0x3E,
    0x3D, 0x3C, 0x3B, 0x3A, 0x39, 0x38, 0x38, 0x37, 0x36, 0x36, 0x35, 0x35, 0x34, 0x34, 0x33, 0x33, 0x32, 0x32,
};

// Computes a fast approximation of `ceil(x / log2(base))`.
// For base 2, the exact result is returned.
[[nodiscard]] constexpr std::size_t approximate_ceil_div_log2(const std::size_t x, const int base) {
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2 && base <= 36);
    constexpr std::size_t fractional_bits = 8;
    constexpr std::size_t fractional_mask = (std::size_t{1} << fractional_bits) - 1;

    if (base == 2) {
        return x;
    }
    const auto coeff = static_cast<std::size_t>(approximate_ceil_div_log2_q0_8_table[static_cast<std::size_t>(base)]);
    const std::size_t scaled_result = x * coeff;
    return (scaled_result >> fractional_bits) + static_cast<std::size_t>((scaled_result & fractional_mask) != 0);
}

} // namespace detail

template <size_t b, class A>
constexpr std::to_chars_result
to_chars(char* const begin, char* const end, const basic_big_int<b, A>& x, const int base) {
    using size_type                   = typename basic_big_int<b, A>::size_type;
    constexpr size_type bits_per_limb = basic_big_int<b, A>::bits_per_limb;

    BEMAN_BIG_INT_DEBUG_ASSERT(begin);
    BEMAN_BIG_INT_DEBUG_ASSERT(end);
    BEMAN_BIG_INT_DEBUG_ASSERT(base >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(base <= 36);

    if (begin == end) {
        return {end, std::errc::value_too_large};
    }

    const size_type width = x.width_mag();
    if (width == 0) {
        *begin = '0';
        return {begin + 1, std::errc{}};
    }

    char* current_begin = begin;
    if (x.is_negative()) {
        *current_begin = '-';
        ++current_begin;
    }
    if (current_begin == end) {
        return {end, std::errc::value_too_large};
    }

    // For values that are small enough,
    // the entire implementation (regardless of base) can be delegated to `std::to_chars`.
    {
        constexpr bool ignore_sign = true;
        if (width <= bits_per_limb) {
            // The super happy case is that we can use the std::to_chars implementation for a single limb.
            return std::to_chars(current_begin, end, x.template to<uint_multiprecision_t, ignore_sign>(), base);
        } else if constexpr (bits_per_limb < detail::width_v<std::uintmax_t>) {
            // The slightly less happy case is that we need to use
            // the multiprecision implementation of `std::to_chars`.
            // While we could skip the previous "super happy" check, it may be cheaper to dispatch here
            // because `std::to_chars` would otherwise need to re-check
            // whether it can delegate to a single-limb implementation itself.
            if (width <= detail::width_v<std::uintmax_t>) {
                return std::to_chars(current_begin, end, x.template to<std::uintmax_t, ignore_sign>(), base);
            }
        }
    }

    const uint_multiprecision_t max_pow                  = detail::limb_max_power(base);
    const auto                  max_digits_per_iteration = static_cast<size_type>(detail::limb_max_input_digits(base));
    constexpr const char*       alphabet                 = "0123456789abcdefghijklmnopqrstuvwxyz";

    switch (base) {
    case 2:
    case 4:
    case 8:
    case 16:
    case 32: {
        const auto bits_per_digit     = static_cast<size_type>(std::countr_zero(static_cast<unsigned char>(base)));
        const auto bits_per_iteration = static_cast<size_type>(std::countr_zero(max_pow));
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration != 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration <= bits_per_limb);
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration % bits_per_digit == 0);

        const size_type leading_bits       = width % bits_per_iteration;
        size_type       current_bit_offset = width - leading_bits;

        // First, we need to take care of the leading "head" bits.
        // For example, for octal, we operate on 63 bits at a time,
        // and 2 leading bits are left over.
        if (leading_bits != 0) {
            const uint_multiprecision_t head = x.get_bits_at(current_bit_offset);
            if (head != 0) {
                const std::to_chars_result head_result = std::to_chars(current_begin, end, head, base);
                if (head_result.ec != std::errc{}) {
                    return head_result;
                }
                current_begin = head_result.ptr;
            }
        }

        const auto digit_mask = (uint_multiprecision_t{1} << bits_per_digit) - 1;

        // Once the head digits are printed out, every subsequent block of bits
        // has exactly the same amount of digits.
        // For example, when printing a 128-bit integer in octal, there are 126 bits left,
        // handled exactly 63 bits at a time.

        if (max_pow == 0) {
            // Special case for bases 2, 4, and 16 where we process one limb per iteration.
            BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration == bits_per_limb);
            BEMAN_BIG_INT_DEBUG_ASSERT(current_bit_offset % bits_per_limb == 0);
            BEMAN_BIG_INT_DEBUG_ASSERT(current_bit_offset % bits_per_iteration == 0);

            const auto* const limbs       = x.limb_ptr();
            const size_type   whole_limbs = current_bit_offset / bits_per_limb;
            for (size_type limb_index = whole_limbs; limb_index-- > 0;) {
                const uint_multiprecision_t limb = limbs[limb_index];

                for (size_type i = max_digits_per_iteration; i-- > 0;) {
                    if (current_begin == end) {
                        return {end, std::errc::value_too_large};
                    }
                    const uint_multiprecision_t digit_value = (limb >> (i * bits_per_digit)) & digit_mask;
                    *current_begin                          = alphabet[digit_value];
                    ++current_begin;
                }
            }
        } else {
            // General case for other powers of two.
            BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_iteration < bits_per_limb);
            const auto chunk_mask = (uint_multiprecision_t{1} << bits_per_iteration) - 1;

            while (current_bit_offset >= bits_per_iteration) {
                current_bit_offset -= bits_per_iteration;

                const uint_multiprecision_t chunk = x.get_bits_at(current_bit_offset) & chunk_mask;

                for (size_type i = max_digits_per_iteration; i-- > 0;) {
                    if (current_begin == end) {
                        return {end, std::errc::value_too_large};
                    }
                    const uint_multiprecision_t digit_value = (chunk >> (i * bits_per_digit)) & digit_mask;
                    *current_begin                          = alphabet[digit_value];
                    ++current_begin;
                }
            }
            BEMAN_BIG_INT_DEBUG_ASSERT(current_bit_offset == 0);
        }

        return {current_begin, std::errc{}};
    }

    case 3:
    case 5:
    case 6:
    case 7:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 33:
    case 34:
    case 35:
    case 36: {
        auto remainder = x;
        remainder.unchecked_set_sign(false);
        // Zero should have been handled above already.
        BEMAN_BIG_INT_DEBUG_ASSERT(!remainder.is_zero());

        // Process max_digits_per_iteration digits at a time by dividing by max_pow.
        // std::to_chars writes each remainder's digits most-significant-first;
        // we reverse each chunk immediately after writing (undoing the most-significant-first order),
        // then pad to exactly max_digits_per_iteration characters.
        // The final big reverse at the end converts the accumulated least-significant-first chunks
        // back into the correct most-significant-first output.
        while (remainder.limb_count() > 1) {
            if (end - current_begin < static_cast<std::ptrdiff_t>(max_digits_per_iteration)) {
                return {end, std::errc::value_too_large};
            }
            // The fact that we std::move here should ensure that only one allocation is used.
            // `div_rem_to_zero` should repurpose the allocation of `remainder`,
            // which is then moved back into `remainder` at the end of the loop.
            auto [q, r] = div_rem_to_zero(std::move(remainder), max_pow);

            // Very evil and unsafe (but direct and fast) access to the single limb
            // that the remainder of the division should hold.
            BEMAN_BIG_INT_DEBUG_ASSERT(r.is_representation_inplace());
            BEMAN_BIG_INT_DEBUG_ASSERT(r.limb_count() == 1);
            const uint_multiprecision_t r_limb = r.m_storage.limbs[0];

            const std::to_chars_result chunk_result =
                std::to_chars(current_begin, current_begin + max_digits_per_iteration, r_limb, base);
            BEMAN_BIG_INT_DEBUG_ASSERT(chunk_result.ec == std::errc{});

            const auto written = static_cast<size_type>(chunk_result.ptr - current_begin);
            std::reverse(current_begin, chunk_result.ptr);
            std::fill_n(chunk_result.ptr, max_digits_per_iteration - written, '0');

            current_begin += max_digits_per_iteration;
            remainder = std::move(q);
        }

        BEMAN_BIG_INT_DEBUG_ASSERT(remainder.limb_count() == 1);
        const std::to_chars_result final_result = std::to_chars(current_begin, end, remainder.limb_ptr()[0], base);
        if (final_result.ec != std::errc{}) {
            return final_result;
        }
        std::reverse(current_begin, final_result.ptr);
        // No zero-pad is needed for the final chunk because that would put leading zeros
        // in front of the entire result.
        current_begin = final_result.ptr;

        // We wrote all the digits in reverse order.
        // Everything except the leading minus sign (if any) needs to be reversed.
        std::reverse(begin + (x.is_negative() ? 1 : 0), current_begin);
        return {current_begin, std::errc{}};
    }

    default:
        break;
    }

    // Invalid base; earlier checks should have caught this already, but let's make sure.
    BEMAN_BIG_INT_ASSERT(false);
}

template <std::size_t b, class A>
[[nodiscard]] constexpr std::from_chars_result
from_chars(const char* const begin, const char* const end, basic_big_int<b, A>& out, const int base) {
    using size_type = typename basic_big_int<b, A>::size_type;

    if (begin == nullptr || begin == end || base < 2 || base > 36) {
        return {end, std::errc::invalid_argument};
    }

    const char* current_begin = *begin == '-' ? begin + 1 : begin;
    if (current_begin == end) {
        return {end, std::errc::invalid_argument};
    }

    const uint_multiprecision_t max_pow                  = detail::limb_max_power(base);
    const std::ptrdiff_t        max_digits_per_iteration = detail::limb_max_input_digits(base);
    const bool                  is_pow_2                 = (base & (base - 1)) == 0;
    BEMAN_BIG_INT_DEBUG_ASSERT(max_pow != 0 || is_pow_2);

    const char* current_end = current_begin;
    for (; current_end != end; ++current_end) {
        const int value = detail::digit_value(*current_end);
        if (value < 0 || value >= base) {
            break;
        }
    }
    // This indicates that we have parsed either nothing or only the minus sign:
    if (current_end == current_begin) {
        return {end, std::errc::invalid_argument};
    }
    const char* const returned_end   = current_end;
    const auto        digit_count    = static_cast<size_type>(current_end - current_begin);
    const auto        bits_per_digit = static_cast<size_type>(std::countr_zero(static_cast<unsigned char>(base)));

    if (max_pow == 0) {
        // Special powers of two (2, 4, 16): one digit block maps exactly to one limb.
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_digit != 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(detail::width_v<uint_multiprecision_t> % bits_per_digit == 0);

        const auto digits_per_limb = detail::width_v<uint_multiprecision_t> / bits_per_digit;
        const auto limbs_needed    = detail::div_to_pos_inf(digit_count, digits_per_limb);

        out.set_zero();
        out.grow(limbs_needed);

        auto* const     out_limbs      = out.limb_ptr();
        std::uint32_t   out_limb_index = 0;
        const size_type block_len      = static_cast<size_type>(digits_per_limb);

        while (true) {
            const auto digit_block_length =
                std::min(current_end - current_begin, static_cast<std::ptrdiff_t>(block_len));
            const char* const digit_block_begin = current_end - digit_block_length;
            BEMAN_BIG_INT_DEBUG_ASSERT(digit_block_length != 0);

            const std::from_chars_result digit_block_result =
                std::from_chars(digit_block_begin, current_end, out_limbs[out_limb_index], base);
            // We already validated the digit run and bounded each block to one limb.
            BEMAN_BIG_INT_DEBUG_ASSERT(digit_block_result.ec == std::errc{});
            ++out_limb_index;

            if (digit_block_begin == current_begin) {
                break;
            }
            current_end = digit_block_begin;
        }

        out.unchecked_set_limb_count(out_limb_index);
        // This trim may be necessary if we have parsed leading zeros in the string.
        out.unchecked_trim_magnitude();
        if (*begin == '-') {
            out.negate();
        }
        return {returned_end, std::errc{}};
    }

    if (is_pow_2) {
        // When the base is a power of two, parsing is significantly different.
        // Namely, it takes place FROM LAST TO FIRST, i.e. starting with the least significant digit.
        // This is done so that the parsed blocks of digits can simply be appended to the representation.
        // Parsing in this way essentially involves no multiprecision operations,
        // not even a multiprecision shift.
        BEMAN_BIG_INT_DEBUG_ASSERT(bits_per_digit != 0);
        const auto limbs_needed =
            detail::div_to_pos_inf(digit_count * bits_per_digit, detail::width_v<uint_multiprecision_t>);
        const auto         bits_per_iteration = static_cast<big_int::size_type>(std::countr_zero(max_pow));
        big_int::size_type bit_offset         = 0;

        // In any other case, we have the guarantee that at least one digit can be parsed.
        out.set_zero();
        out.grow(limbs_needed);
        while (true) {
            const auto        digit_block_length = std::min(current_end - current_begin, max_digits_per_iteration);
            const char* const digit_block_begin  = current_end - digit_block_length;
            BEMAN_BIG_INT_DEBUG_ASSERT(digit_block_length != 0);

            uint_multiprecision_t        bits{};
            const std::from_chars_result digit_block_result =
                std::from_chars(digit_block_begin, current_end, bits, base);
            // We already pre-parsed the string when advancing current_end,
            // and we made sure to only take as many digits as can fit into uint_multiprecision_t,
            // so use of `std::from_chars` should be infallible.
            BEMAN_BIG_INT_DEBUG_ASSERT(digit_block_result.ec == std::errc{});

            out.unchecked_init_magnitude_bits_at(bits, bit_offset);
            bit_offset += bits_per_iteration;

            if (digit_block_begin == current_begin) {
                break;
            }
            current_end -= digit_block_length;
            BEMAN_BIG_INT_DEBUG_ASSERT(current_end >= digit_block_begin);
        }
        // This trim may be necessary if we have parsed leading zeros in the string.
        out.unchecked_trim_magnitude();
        if (*begin == '-') {
            out.negate();
        }
        return {returned_end, std::errc{}};
    }

    // This is the "usual case" for all bases that are not powers of two.
    // Since every added digit can affect any existing bit in theory,
    // the problem can't be solved in blocks of bits.
    // Instead, we need to use the classic multiplication and addition approach.
    //
    // However, instead of doing so naively digit-by-digit,
    // each iteration, we can process as many digit as possible without exceeding
    // the greatest representable power in the base.
    //
    // For example, if `base` is `10`, instead of doing parsing base 10,
    // we do it base `1'000'000'000` assuming `uint_multiprecision_t` is 32-bit.

    // Parse the valid digit run in blocks that fit into `uint_multiprecision_t`.
    // The first block may be shorter so that all following blocks have the same width,
    // then the accumulated result is built left-to-right by multiplying by `max_pow` before adding each next block.
    {
        const std::size_t bits_needed_upper_bound = detail::approximate_ceil_mul_log2(digit_count, base);
        const std::size_t limbs_needed_upper_bound =
            detail::div_to_pos_inf(bits_needed_upper_bound, detail::width_v<uint_multiprecision_t>);
        out.grow(limbs_needed_upper_bound);
    }

    // clang-format off
    const std::ptrdiff_t first_block_length = (current_end - current_begin) % max_digits_per_iteration == 0
                                            ? max_digits_per_iteration
                                            : (current_end - current_begin) % max_digits_per_iteration;
    // clang-format on

    uint_multiprecision_t        leading_digits{};
    const std::from_chars_result first_result =
        std::from_chars(current_begin, current_begin + first_block_length, leading_digits, base);
    BEMAN_BIG_INT_DEBUG_ASSERT(first_result.ec == std::errc{});
    out = leading_digits;

    for (const char* current_first = current_begin + first_block_length; current_first != current_end;
         current_first += max_digits_per_iteration) {
        const char* const current_last = std::min(current_first + max_digits_per_iteration, current_end);

        uint_multiprecision_t        digits         = {};
        const std::from_chars_result partial_result = std::from_chars(current_first, current_last, digits, base);
        // Same reasoning as for powers of two; see above.
        BEMAN_BIG_INT_DEBUG_ASSERT(partial_result.ec == std::errc{});
        BEMAN_BIG_INT_DEBUG_ASSERT(digits < max_pow);

        out *= max_pow;
        out += digits;
    }

    if (*begin == '-') {
        out.negate();
    }
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        out.shrink_to_fit();
    }
    return {returned_end, std::errc{}};
}

namespace detail {

template <class C, std::size_t b, class A>
[[nodiscard]] constexpr std::basic_string<C> to_basic_string(const basic_big_int<b, A>& x, const int base) {
    BEMAN_BIG_INT_ASSERT(base >= 2 && base <= 36);
    constexpr std::size_t minus_sign_size = 1;

    // For up to 1 bit, we need at most one digit in any base.
    // Otherwise, the digit width in any base is generally `log_base(x) + 1`.
    // To convert from the width (which is `log2(x) + 1`), we need to decrement,
    // convert the binary logarithm to a logarithm base `base`, and increment.
    const auto        width = x.width_mag();
    const std::size_t required_digits =
        width <= 1 ? std::size_t{1} : detail::approximate_ceil_div_log2(width - 1, base) + 1 + minus_sign_size;

#ifdef __cpp_lib_string_resize_and_overwrite
    std::basic_string<C> result;
    result.resize_and_overwrite(required_digits, [&](char* const data, const std::size_t n) {
        const auto [p, ec] = to_chars(data, data + n, x, base);
        BEMAN_BIG_INT_ASSERT(ec == std::errc{});
        return static_cast<std::size_t>(p - data);
    });
#else
    std::basic_string<C> result(required_digits, char{});
    const auto [p, ec] = to_chars(result.data(), result.data() + result.size(), x, base);
    BEMAN_BIG_INT_ASSERT(ec == std::errc{});
    result.resize(static_cast<std::size_t>(p - result.data()));
#endif

    return result;
}

} // namespace detail

template <std::size_t b, class A>
[[nodiscard]] constexpr std::string to_string(const basic_big_int<b, A>& x, const int base = 10) {
    return detail::to_basic_string<char>(x, base);
}

// TODO(eisenwave): to_wstring.

namespace detail {

using std::from_chars;

// Detects the base automatically based on the `0x`, `0b`, or `0` prefix
// and dispatches to `from_chars` with the identified base.
// Leading minus signs are not supported.
template <class T>
[[nodiscard]] constexpr std::from_chars_result
from_chars_auto_base(const char* const begin, const char* const end, T& out)
    requires requires { from_chars(begin, end, out, 10); }
{
    if (begin == end || *begin < '0' || *begin > '9') {
        return {end, std::errc::invalid_argument};
    }
    if (*begin != '0' || end - begin <= 1) {
        return from_chars(begin, end, out, 10);
    }
    switch (begin[1]) {
    case 'b':
    case 'B':
        return from_chars(begin + 2, end, out, 2);
    // In the future, this will also have a case for 'o'
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p0085r3.html
    case 'x':
    case 'X':
        return from_chars(begin + 2, end, out, 16);
    default:
        break;
    }
    // This case (leading zero for octal) is deprecated,
    // but we have no real way to communicate that and raise a warning here.
    return from_chars(begin, end, out, 8);
}

// Helper class which assists in removing digit separators from literals.
template <char... digits>
struct literal_buffer {
  private:
    // The sizeof... is necessary here because of:
    // https://github.com/llvm/llvm-project/issues/79750
    static constexpr char        raw_buffer[sizeof...(digits)]{digits...};
    static constexpr const char* raw_end = raw_buffer + sizeof(raw_buffer);

    static consteval std::size_t make_buffer_impl(const char* begin, const char* const end, char* out = nullptr) {
        bool        digit_separator_allowed = false;
        std::size_t length                  = 0;
        for (; begin != end; ++begin) {
            if (*begin == '\'') {
                if (!digit_separator_allowed) {
                    return 0;
                }
                digit_separator_allowed = false;
            } else {
                digit_separator_allowed = true;
                ++length;
                if (out != nullptr) {
                    *out++ = *begin;
                }
            }
        }
        return length;
    }

    static constexpr std::size_t result_length = make_buffer_impl(raw_buffer, raw_end);

    [[nodiscard]] static consteval auto make_buffer() {
        if constexpr (result_length == 0) {
            return std::array<char, 0>{};
        } else {
            std::array<char, result_length> result;
            make_buffer_impl(raw_buffer, raw_end, result.data());
            return result;
        }
    }

  public:
    static constexpr std::array<char, result_length> value = make_buffer();
};

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")
struct parse_non_allocating_result {
    big_int            value;
    big_int::size_type limb_count;
    std::errc          ec;
};
BEMAN_BIG_INT_DIAGNOSTIC_POP()

// Returns the result of parsing a `big_int` using `from_chars_auto_base`.
// However, if the result is too large to fit into inplace storage,
// `{limb_count(), std::errc::result_out_of_range}` is returned.
[[nodiscard]] static constexpr parse_non_allocating_result parse_non_allocating_impl(const char* const begin,
                                                                                     const char* const end) {
    // This function is not consteval because of compiler bugs,
    // but should only be called during constant evaluation.
    // https://developercommunity.microsoft.com/t/Nonsensical-error-C2440-when-initializin/11077170

    big_int parsed;
    const auto [p, ec] = from_chars_auto_base(begin, end, parsed);
    if (ec != std::errc{}) {
        return parse_non_allocating_result{0, 0, ec};
    }
    if (p != end) {
        return parse_non_allocating_result{0, 0, std::errc::invalid_argument};
    }
    const auto parsed_size = parsed.size();
    if (parsed.capacity() != 0) {
        return {big_int{}, parsed_size, std::errc::result_out_of_range};
    }
    return {std::move(parsed), parsed_size, std::errc{}};
}

template <std::array buffer>
struct parse_non_allocating {
  private:
    static_assert(std::is_same_v<typename decltype(buffer)::value_type, char>);
    static constexpr parse_non_allocating_result result =
        parse_non_allocating_impl(buffer.data(), buffer.data() + buffer.size());

  public:
    static constexpr big_int   value      = result.value;
    static constexpr auto      limb_count = result.limb_count;
    static constexpr std::errc ec         = result.ec;
};

template <std::size_t limb_count>
[[nodiscard]] consteval std::array<uint_multiprecision_t, limb_count>
literal_operator_n_compute_limbs(const char* const begin, const char* const end) {
    std::array<uint_multiprecision_t, limb_count> result;
    big_int                                       parsed;
    const auto [p, ec] = detail::from_chars_auto_base(begin, end, parsed);
    // We've already parsed this successfully once in parse_non_allocating,
    // so there's no reason it would fail now.
    BEMAN_BIG_INT_ASSERT(p == end);
    BEMAN_BIG_INT_ASSERT(ec == std::errc{});
    BEMAN_BIG_INT_ASSERT(parsed.size() == limb_count);
    std::copy_n(parsed.representation().data(), limb_count, result.data());
    return result;
}

template <std::array buffer>
inline constexpr std::array<uint_multiprecision_t, detail::parse_non_allocating<buffer>::limb_count>
    literal_operator_n_limbs_v = literal_operator_n_compute_limbs<detail::parse_non_allocating<buffer>::limb_count>(
        buffer.data(), buffer.data() + buffer.size());

} // namespace detail

inline namespace literals {
inline namespace big_int_literals {

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wuser-defined-literals")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wreserved-user-defined-literal")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wliteral-suffix")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_MSVC(4455)

// Formatting suppressions are needed to prevent insertion of space between `""` and `n`.
// clang-format off
template <char... digits>
[[nodiscard]] constexpr big_int operator""n()
  noexcept(detail::parse_non_allocating<detail::literal_buffer<digits...>::value>::ec == std::errc{})
  {
    // clang-format on

    // The first step is to do basic validation and to eliminate any digit separators.
    constexpr const auto& buffer = detail::literal_buffer<digits...>::value;
    static_assert(!buffer.empty(), "The given literal is not a valid integer-literal.");

    if constexpr (detail::parse_non_allocating<buffer>::ec == std::errc{}) {
        // The happy case is where the literal's integer value is small enough to fit into big_int without allocations.
        // In that case, we've already parsed the literal, and simply return the pre-computed big_int.
        return detail::parse_non_allocating<buffer>::value;
    } else if constexpr (detail::parse_non_allocating<buffer>::ec == std::errc::result_out_of_range) {
        // The unhappy case is where allocations are required.
        // While we don't have non-transient allocations and cannot store a constexpr big_int,
        // we can precompute a constexpr limb array which is used for fast runtime initialization.
        // We already know the limb count from the previous parsing attempt:
        constexpr const auto& limbs = detail::literal_operator_n_limbs_v<buffer>;
        return big_int(limbs.data(), limbs.data() + limbs.size());
    } else {
        static_assert(detail::parse_non_allocating<buffer>::ec == std::errc::invalid_argument);
        BEMAN_BIG_INT_STATIC_ASSERT_FALSE("The given literal is not a valid integer-literal.");
    }
}

// UDLs without underscore don't work on Clang:
// https://github.com/llvm/llvm-project/issues/76394
// clang-format off
template <char... digits>
[[nodiscard]] constexpr big_int operator""_n() noexcept(noexcept(operator""n<digits...>())) {
    return operator""n<digits...>();
}
// clang-format on

BEMAN_BIG_INT_DIAGNOSTIC_POP()

} // namespace big_int_literals
} // namespace literals
} // namespace beman::big_int

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_BIG_INT_HPP
