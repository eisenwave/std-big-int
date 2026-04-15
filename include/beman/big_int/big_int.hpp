// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BIG_INT_HPP
#define BEMAN_BIG_INT_BIG_INT_HPP

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
#if __has_include(<stdfloat>)
    #include <stdfloat>
#endif
#include <type_traits>

#include <beman/big_int/config.hpp>
#include <beman/big_int/wide_ops.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")

namespace beman::big_int {

// alias uint_multiprecision_t
using beman::big_int::uint_multiprecision_t;

// Forward decl so that we can define our concepts
template <std::size_t min_inplace_bits, class Allocator = std::allocator<uint_multiprecision_t>>
class basic_big_int;

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
using common_big_int_type = decltype(common_big_int_type_impl<std::remove_cvref_t<L>, std::remove_cvref_t<R>>())::type;

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

} // namespace detail

// [big.int.class], class template basic_big_int
template <std::size_t min_inplace_bits, class Allocator>
class BEMAN_BIG_INT_TRIVIAL_ABI basic_big_int {

    using limb_type               = uint_multiprecision_t;
    using double_limb_type        = detail::uint_wide_t;
    using signed_limb_type        = detail::int_multiprecision_t;
    using signed_double_limb_type = detail::int_wide_t;

  public:
    using allocator_type = Allocator;
    using size_type      = std::size_t;
    using pointer        = std::allocator_traits<Allocator>::pointer;
    using const_pointer  = std::allocator_traits<Allocator>::const_pointer;
    static_assert(std::is_same_v<typename Allocator::value_type, uint_multiprecision_t>,
                  "Allocator::value_type must be uint_multiprecision_t.");

    template <std::size_t, class>
    friend class basic_big_int;

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
    [[nodiscard]] constexpr bool             is_storage_static() const noexcept;
    [[nodiscard]] constexpr std::uint32_t    limb_count() const noexcept;
    [[nodiscard]] constexpr bool             is_negative() const noexcept;
    [[nodiscard]] constexpr bool             is_zero() const noexcept;
    constexpr void                           set_limb_count(std::uint32_t n) noexcept;
    constexpr void                           set_sign(bool s) noexcept;
    constexpr void                           negate() noexcept;
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
#ifdef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
            static_assert(!std::is_same_v<std::remove_cvref_t<T>, long double>,
                          "long double is not supported on this platform");
#endif
            assign_from_float(value);
        } else {
            if constexpr (std::is_signed_v<std::remove_cvref_t<T>>) {
                set_sign(value < std::remove_cvref_t<T>{0});
                assign_magnitude(detail::uabs(value));
            } else {
                assign_magnitude(value);
            }
        }
    }

    template <detail::arbitrary_arithmetic T>
    constexpr basic_big_int(const T&              value,
                            const allocator_type& a) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>);

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    template <std::ranges::input_range R>
        requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
    constexpr explicit basic_big_int(std::from_range_t, R&& r, const allocator_type& a = allocator_type());
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
            set_limb_count(count);
            set_sign(x.is_negative());
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
            set_sign(neg);
        }
        return *this;
    }

    // TODO(alcxpr): compound operators

    template <detail::signed_or_unsigned S>
    constexpr basic_big_int& operator>>=(S s);
    template <detail::signed_or_unsigned S>
    constexpr basic_big_int& operator<<=(S s);

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

    // [big.int.conv], conversions
    template <class T>
        requires detail::cv_unqualified_arithmetic<T>
    constexpr explicit operator T() const noexcept;

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

    using shift_type                      = unsigned long long;
    static constexpr shift_type shift_max = std::numeric_limits<shift_type>::max();

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

    // Returns the sum `(lhs, lhs_neg) + (rhs, rhs_neg)` as a fresh `basic_big_int`.
    // Only allocates heap storage if the final result is certain to exceed the inline limb capacity
    // (a speculative top-limb carry only triggers a grow after the carry actually occurs).
    template <std::size_t extent_a, std::size_t extent_b>
    [[nodiscard]] static constexpr basic_big_int
    make_sum_of_limbs(std::span<const uint_multiprecision_t, extent_a> lhs,
                      bool                                             lhs_neg,
                      std::span<const uint_multiprecision_t, extent_b> rhs,
                      bool                                             rhs_neg);

    static constexpr bool        has_inplace_to_bit_uint = inplace_bits <= BEMAN_BIG_INT_BITINT_MAXWIDTH;
    [[nodiscard]] constexpr auto inplace_to_bit_uint() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_bit_uint
    {
        static_assert(std::has_unique_object_representations_v<decltype(m_storage.limbs)>,
                      "Bit-casting doesn't work when there is padding.");
        return std::bit_cast<unsigned _BitInt(inplace_bits)>(m_storage.limbs);
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT

    [[nodiscard]] constexpr auto inplace_to_sbit_int() const noexcept
#ifdef BEMAN_BIG_INT_HAS_BITINT
        requires has_inplace_to_bit_uint
    {
        static_assert(std::has_unique_object_representations_v<decltype(m_storage.limbs)>,
                      "Bit-casting doesn't work when there is padding.");
        // Use `inplace_bits + 1` to avoid signed overflow when negating a value
        // with the high bit set.
        const auto mag =
            static_cast<_BitInt(inplace_bits + 1)>(std::bit_cast<unsigned _BitInt(inplace_bits)>(m_storage.limbs));
        return is_negative() ? -mag : mag;
    }
#else
        = delete;
#endif // BEMAN_BIG_INT_HAS_BITINT
};

// =============================================================================
// Out-of-class definitions
// =============================================================================

// Internal accessors

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::is_storage_static() const noexcept {
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
    if (is_negative()) {
        return false;
    }
    for (const uint_multiprecision_t limb : representation()) {
        if (limb != 0) {
            return false;
        }
    }
    return true;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::set_limb_count(std::uint32_t n) noexcept {
    m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | n;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::set_sign(bool s) noexcept {
    m_size_and_sign = (m_size_and_sign & 0x7FFF'FFFFU) | (static_cast<std::uint32_t>(s) << 31);
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::negate() noexcept {
    if (!is_zero()) {
        m_size_and_sign ^= 0x8000'0000U;
    }
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::limb_ptr() noexcept -> limb_type* {
    return is_storage_static() ? m_storage.limbs : m_storage.data;
}

template <std::size_t b, class A>
constexpr const basic_big_int<b, A>::limb_type* basic_big_int<b, A>::limb_ptr() const noexcept {
    return is_storage_static() ? m_storage.limbs : m_storage.data;
}

// [big.int.cons] — constructors

template <std::size_t b, class A>
constexpr basic_big_int<b, A>::basic_big_int(const basic_big_int& x)
    : m_capacity{0}, m_size_and_sign{x.m_size_and_sign}, m_storage{}, m_alloc{x.m_alloc} {
    if (x.limb_count() <= inplace_capacity) {
        if (x.is_storage_static()) {
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
    if (x.is_storage_static()) {
        for (size_type i = 0; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = x.m_storage.limbs[i];
        }
    } else {
        m_storage.data    = x.m_storage.data;
        x.m_capacity      = 0;
        x.m_size_and_sign = 1;
    }
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>& basic_big_int<b, A>::operator=(const basic_big_int& x) {
    if (this == &x) {
        return *this;
    }

    free_storage();
    m_size_and_sign = x.m_size_and_sign;
    m_alloc         = x.m_alloc;

    if (x.is_storage_static()) {
        m_capacity = 0;
        for (size_type i = 0; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = x.m_storage.limbs[i];
        }
    } else {
        const alloc_result allocation = alloc_limbs(x.limb_count());
        copy_n_to_allocation(x.m_storage.data, x.limb_count(), allocation);
        m_capacity     = static_cast<std::uint32_t>(allocation.count);
        m_storage.data = allocation.ptr;
    }

    return *this;
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>& basic_big_int<b, A>::operator=(basic_big_int&& x) noexcept {
    if (this == &x) {
        return *this;
    }

    free_storage();
    m_size_and_sign = x.m_size_and_sign;
    m_alloc         = std::move(x.m_alloc);

    if (x.is_storage_static()) {
        m_capacity = 0;
        for (size_type i = 0; i < inplace_capacity; ++i) {
            m_storage.limbs[i] = x.m_storage.limbs[i];
        }
    } else {
        m_capacity        = x.m_capacity;
        m_storage.data    = x.m_storage.data;
        x.m_capacity      = 0;
        x.m_size_and_sign = 1;
    }

    return *this;
}

template <std::size_t b, class A>
template <detail::arbitrary_arithmetic T>
constexpr basic_big_int<b, A>::basic_big_int(const T& value, const allocator_type& a) noexcept(
    detail::no_alloc_constructible_from<inplace_bits, T>)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {
    if constexpr (std::is_floating_point_v<std::remove_cvref_t<T>>) {
#ifdef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
        static_assert(!std::is_same_v<std::remove_cvref_t<T>, long double>,
                      "long double is not supported on this platform");
#endif
        assign_from_float(static_cast<std::remove_cvref_t<T>>(value));
    } else {
        if constexpr (std::is_signed_v<std::remove_cvref_t<T>>) {
            set_sign(value < std::remove_cvref_t<T>{0});
            using U = std::make_unsigned_t<std::remove_cvref_t<T>>;

#ifdef BEMAN_BIG_INT_MSVC
    #pragma warning(push)
    #pragma warning(disable : 4146) // unary minus on unsigned is intentional
#endif
            assign_magnitude(is_negative() ? static_cast<U>(U{0} - static_cast<U>(value)) : static_cast<U>(value));
#ifdef BEMAN_BIG_INT_MSVC
    #pragma warning(pop)
#endif
        } else {
            assign_magnitude(value);
        }
    }
}

#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
template <std::size_t b, class A>
template <std::ranges::input_range R>
    requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
constexpr basic_big_int<b, A>::basic_big_int(std::from_range_t, R&& r, const allocator_type& a)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {

    if constexpr (std::ranges::sized_range<R>) {
        reserve(std::ranges::size(r));
        std::size_t i   = 0;
        auto* const dst = limb_ptr();
        for (auto&& elem : r) {
            using U = std::make_unsigned_t<std::ranges::range_value_t<R>>;
            std::construct_at(dst + i++, static_cast<limb_type>(static_cast<U>(elem)));
        }
        set_limb_count(static_cast<std::uint32_t>(i == 0 ? 1 : i));
        while (limb_count() > 1 && dst[limb_count() - 1] == 0) {
            set_limb_count(limb_count() - 1);
        }
    } else {
        for (auto&& elem : r) {
            using U = std::make_unsigned_t<std::ranges::range_value_t<R>>;
            push_back_limb(static_cast<limb_type>(static_cast<U>(elem)));
        }
        while (limb_count() > 1 && limb_ptr()[limb_count() - 1] == 0) {
            set_limb_count(limb_count() - 1);
        }
    }
}
#endif

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
        set_limb_count(limb_count() + 1);
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
        set_limb_count(1);
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

    // TODO(eisenwave): This is pessimistic and assumes that bit-shifting will require an extra limb,
    //                  but that may not be the case.
    //                  It depends on the bits of the uppermost limb.
    reserve(limb_count() + shifted_limbs + size_type(shifted_bits != 0));
    limb_type* const limbs = limb_ptr();

    if (shifted_limbs != 0) {
        const auto current_count = limb_count();
        std::copy_backward(limbs, limbs + current_count, limbs + current_count + shifted_limbs);
        std::fill_n(limbs, static_cast<std::ptrdiff_t>(shifted_limbs), limb_type{0});
        set_limb_count(static_cast<std::uint32_t>(current_count + shifted_limbs));
    }
    if (shifted_bits != 0) {
        const auto current_count = limb_count();
        limbs[current_count]     = 0;
        for (shift_type i = current_count; i-- > shifted_limbs;) {
            const detail::wide<limb_type> all_bits{.low_bits = limbs[i], .high_bits = limbs[i + 1]};
            limbs[i + 1] = detail::funnel_shl(all_bits, static_cast<unsigned int>(shifted_bits));
        }
        limbs[shifted_limbs] <<= shifted_bits;
        set_limb_count(static_cast<std::uint32_t>(current_count + std::uint32_t(limbs[current_count] != 0)));
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
            set_limb_count(1);
        } else {
            std::shift_left(limbs, limbs + current_count, static_cast<std::ptrdiff_t>(shifted_limbs));
            set_limb_count(static_cast<std::uint32_t>(current_count - shifted_limbs));
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
    while (limb_count() > 1 && limbs[limb_count() - 1] == 0) {
        set_limb_count(limb_count() - 1);
    }
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

    return (count - 1) * bits_per_limb + (bits_per_limb - static_cast<size_type>(std::countl_zero(top)) - 1);
}

template <std::size_t b, class A>
constexpr std::span<const uint_multiprecision_t> basic_big_int<b, A>::representation() const noexcept {
    return {limb_ptr(), limb_count()};
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A>::allocator_type basic_big_int<b, A>::get_allocator() const noexcept {
    return m_alloc;
}

template <std::size_t b, class A>
constexpr std::size_t basic_big_int<b, A>::size() const noexcept {
    if (is_storage_static()) {
        return inplace_capacity;
    } else {
        return limb_count();
    }
}

template <std::size_t b, class A>
constexpr std::size_t basic_big_int<b, A>::max_size() noexcept {
    // We use the high bit to encode the sign, so we are limited to 2^31
    return std::numeric_limits<std::uint32_t>::max() >> 1U;
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

    if (is_storage_static() || m_capacity <= count) {
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
constexpr basic_big_int<b, A> basic_big_int<b, A>::operator+() const& {
    return *this;
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A> basic_big_int<b, A>::operator+() && noexcept {
    return std::move(*this);
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A> basic_big_int<b, A>::operator-() const& {
    auto copy = *this;
    copy.negate();
    return copy;
}

template <std::size_t b, class A>
constexpr basic_big_int<b, A> basic_big_int<b, A>::operator-() && noexcept {
    auto copy = std::move(*this);
    copy.negate();
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator++() & -> basic_big_int& {
    if (is_negative()) {
        unchecked_decrement_magnitude();
        if (limb_count() == 1 && limb_ptr()[0] == 0) {
            set_sign(false);
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
            set_sign(true);
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
template <class T>
    requires detail::cv_unqualified_arithmetic<T>
constexpr basic_big_int<b, A>::operator T() const noexcept {
    if constexpr (std::is_same_v<T, bool>) {
        return !is_zero();
    } else if constexpr (std::is_floating_point_v<T>) {
#ifdef BEMAN_BIG_INT_HAS_BITINT
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if (is_storage_static()) {
                return static_cast<T>(inplace_to_sbit_int());
            }
        }
#endif
        // If the value exceeds the maximum finite value of T, return infinity.
        // See P3899R1.
        if (width_mag() >= static_cast<std::size_t>(std::numeric_limits<T>::max_exponent)) {
            return is_negative() ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
        }
        constexpr T       two64 = static_cast<T>(1ULL << 32) * static_cast<T>(1ULL << 32);
        T                 result{0};
        const auto* const limbs = limb_ptr();
        for (std::size_t i = limb_count(); i != 0;) {
            --i;
            result = result * two64 + static_cast<T>(limbs[i]);
        }
        return is_negative() ? -result : result;
    } else {
#ifdef BEMAN_BIG_INT_HAS_BITINT
        if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
            if (is_storage_static()) {
                return static_cast<T>(inplace_to_sbit_int());
            }
        }
#endif
        using U = std::make_unsigned_t<T>;
        U                 mag{0};
        constexpr auto    n     = detail::div_to_pos_inf(sizeof(U), sizeof(limb_type));
        const auto* const limbs = limb_ptr();
        for (std::size_t i = 0; i < std::min(n, static_cast<std::size_t>(limb_count())); ++i) {
            mag |= static_cast<U>(limbs[i]) << (i * bits_per_limb);
        }
        return static_cast<T>(is_negative() ? ~mag + U{1} : mag);
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

} // namespace detail

// [big.int.binary]
template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator+(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    using LT     = std::remove_cvref_t<L>;
    using RT     = std::remove_cvref_t<R>;

    if constexpr (detail::is_basic_big_int_v<LT> && detail::is_basic_big_int_v<RT>) {
        return Result::make_sum_of_limbs(x.representation(),
                                         x.is_negative(), //
                                         y.representation(),
                                         y.is_negative());
    } else if constexpr (detail::is_basic_big_int_v<LT>) {
        // `y` is a primitive integer; materialize it as a fixed-extent limb array
        // so the span we pass to the helper stays valid for the call.
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        return Result::make_sum_of_limbs(x.representation(),
                                         x.is_negative(), //
                                         detail::to_fixed_span(y_limbs),
                                         detail::integer_signbit(y));
    } else {
        // `x` is a primitive integer
        static_assert(detail::is_basic_big_int_v<RT>);
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        return Result::make_sum_of_limbs(detail::to_fixed_span(x_limbs),
                                         detail::integer_signbit(x), //
                                         y.representation(),
                                         y.is_negative());
    }
}

// `x - y` is implemented as `x + (-y)`: we flip the sign of the right-hand operand
// (without materializing a negated value) and dispatch through the same magnitude
// add/subtract path as `operator+`.
template <class L, class R>
constexpr detail::common_big_int_type<L, R> operator-(L&& x, R&& y) {
    using Result = detail::common_big_int_type<L, R>;
    using LT     = std::remove_cvref_t<L>;
    using RT     = std::remove_cvref_t<R>;

    if constexpr (detail::is_basic_big_int_v<LT> && detail::is_basic_big_int_v<RT>) {
        return Result::make_sum_of_limbs(x.representation(),
                                         x.is_negative(), //
                                         y.representation(),
                                         !y.is_negative());
    } else if constexpr (detail::is_basic_big_int_v<LT>) {
        // `y` is a primitive integer
        const auto y_limbs = detail::to_limbs(detail::uabs(y));
        return Result::make_sum_of_limbs(x.representation(),
                                         x.is_negative(), //
                                         detail::to_fixed_span(y_limbs),
                                         !detail::integer_signbit(y));
    } else {
        // `x` is a primitive integer
        static_assert(detail::is_basic_big_int_v<RT>);
        const auto x_limbs = detail::to_limbs(detail::uabs(x));
        return Result::make_sum_of_limbs(detail::to_fixed_span(x_limbs),
                                         detail::integer_signbit(x), //
                                         y.representation(),
                                         !y.is_negative());
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
            if (is_storage_static()) {
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
            if (is_storage_static()) {
                return inplace_to_bit_uint() <=> x;
            }
        }
        return compare_limbs(detail::to_fixed_span(detail::to_limbs(x)), false);
    } else {
        if constexpr (has_inplace_to_bit_uint) {
            if (is_storage_static()) {
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

// Builds the sum or difference of two limb spans and returns it as a fresh `basic_big_int`.
template <std::size_t b, class A>
template <std::size_t extent_a, std::size_t extent_b>
constexpr basic_big_int<b, A>
basic_big_int<b, A>::make_sum_of_limbs(const std::span<const uint_multiprecision_t, extent_a> lhs,
                                       const bool                                             lhs_neg,
                                       const std::span<const uint_multiprecision_t, extent_b> rhs,
                                       const bool                                             rhs_neg) {
    basic_big_int result;

    if (lhs_neg == rhs_neg) {
        // Same sign: add magnitudes limb-by-limb using carrying_add.
        const std::size_t big = std::max(lhs.size(), rhs.size());

        // `grow(big)` only allocates if `big > inplace_limbs`; otherwise it's a no-op
        // and we stay in inline storage.
        result.grow(big);
        limb_type* const limbs = result.limb_ptr();

        bool carry = false;
        for (std::size_t i = 0; i < big; ++i) {
            const limb_type li            = i < lhs.size() ? lhs[i] : limb_type{0};
            const limb_type ri            = i < rhs.size() ? rhs[i] : limb_type{0};
            const auto [r_value, r_carry] = detail::carrying_add(li, ri, carry);
            limbs[i]                      = r_value;
            carry                         = r_carry;
        }
        result.set_limb_count(static_cast<std::uint32_t>(big));

        // If the ripple carry has actually produced an out-of-range carry, we allocate the extra limb.
        // We want to avoid allocation or leaving inline storage as much as possible
        if (carry) {
            result.grow(big + 1);
            result.limb_ptr()[big] = limb_type{1};
            result.set_limb_count(static_cast<std::uint32_t>(big + 1));
        }

        // Preserve the "no negative zero" invariant
        if (!result.is_zero()) {
            result.set_sign(lhs_neg);
        }
        return result;
    }

    // Differing signs: subtract the smaller magnitude from the larger,
    // and take the sign of the larger-magnitude operand.
    const auto magnitude_order = detail::compare_limb_magnitudes(lhs, rhs);

    // When `lhs >= rhs` (in magnitude) compute `lhs - rhs` and take sign `lhs_neg`; otherwise
    // compute `rhs - lhs` with sign `rhs_neg`. Equal magnitudes fall into the first branch
    // and produce a normalized `+0`.
    const std::span<const uint_multiprecision_t> larger      = std::is_gteq(magnitude_order)
                                                                   ? std::span<const uint_multiprecision_t>(lhs)
                                                                   : std::span<const uint_multiprecision_t>(rhs);
    const std::span<const uint_multiprecision_t> smaller     = std::is_gteq(magnitude_order)
                                                                   ? std::span<const uint_multiprecision_t>(rhs)
                                                                   : std::span<const uint_multiprecision_t>(lhs);
    const bool                                   result_sign = std::is_gteq(magnitude_order) ? lhs_neg : rhs_neg;

    const std::size_t n = larger.size();
    // Subtraction can never produce more limbs than the larger operand, so this grow is tight.
    result.grow(n);
    limb_type* const limbs = result.limb_ptr();

    bool borrow = false;
    for (std::size_t i = 0; i < n; ++i) {
        const limb_type li             = larger[i];
        const limb_type si             = i < smaller.size() ? smaller[i] : limb_type{0};
        const auto [r_value, r_borrow] = detail::borrowing_sub(li, si, borrow);
        limbs[i]                       = r_value;
        borrow                         = r_borrow;
    }
    // Having picked `larger` correctly, the final borrow must be zero.
    BEMAN_BIG_INT_DEBUG_ASSERT(!borrow);
    result.set_limb_count(static_cast<std::uint32_t>(n));

    // Trim leading zero limbs to maintain the "top limb non-zero unless value is zero" invariant.
    while (result.limb_count() > 1 && limbs[result.limb_count() - 1] == 0) {
        result.set_limb_count(result.limb_count() - 1);
    }

    if (!result.is_zero()) {
        result.set_sign(result_sign);
    }
    return result;
}

// private helpers

template <std::size_t b, class A>
template <detail::unsigned_integer T>
constexpr void basic_big_int<b, A>::assign_magnitude(T value) noexcept {
    if constexpr (sizeof(T) <= sizeof(limb_type)) {
        limb_ptr()[0] = static_cast<limb_type>(value);
        set_limb_count(1);
    } else {
        constexpr std::size_t n   = sizeof(T) / sizeof(limb_type);
        auto*                 dst = limb_ptr();
        for (std::size_t i = 0; i < n; ++i) {
            dst[i] = static_cast<limb_type>(value);
            value >>= bits_per_limb;
        }
        set_limb_count(static_cast<std::uint32_t>(n));
        while (limb_count() > 1 && dst[limb_count() - 1] == 0) {
            set_limb_count(limb_count() - 1);
        }
    }
}

template <std::size_t b, class A>
template <detail::cv_unqualified_floating_point F>
constexpr void basic_big_int<b, A>::assign_from_float(const F value) noexcept {
    // MSVC STL's `std::isfinite` is not `constexpr` in C++20.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        BEMAN_BIG_INT_ASSERT(std::isfinite(value));
    }

    using traits = detail::ieee_traits<F>;
    using bits_t = typename traits::bits_type;

    constexpr int mb   = traits::mantissa_bits;
    constexpr int bias = traits::bias;

    bits_t        bits;
    std::uint32_t ieee_exp;
    std::uint64_t ieee_mantissa;

#if __LDBL_MANT_DIG__ == 64 && __LDBL_MAX_EXP__ == 16384
    if constexpr (std::is_same_v<bits_t, detail::long_double_bits>) {
        // UB on x86 due to 6 padding bytes.
        if BEMAN_BIG_INT_IS_CONSTEVAL {
            return;
        }
        __builtin_memcpy(&bits, &value, sizeof(bits));
        set_sign(static_cast<bool>(bits.sign));
        ieee_exp      = static_cast<std::uint32_t>(bits.exponent);
        ieee_mantissa = bits.mantissa;
    } else {
        bits = std::bit_cast<bits_t>(value);
        set_sign(static_cast<bool>((bits >> (mb + traits::exponent_bits)) & 1));
        ieee_exp      = static_cast<std::uint32_t>((bits >> mb) & ((bits_t{1} << traits::exponent_bits) - 1));
        ieee_mantissa = static_cast<std::uint64_t>(bits & ((bits_t{1} << mb) - 1));
    }
#else
    {
        bits = std::bit_cast<bits_t>(value);
        set_sign(static_cast<bool>((bits >> (mb + traits::exponent_bits)) & 1));
        ieee_exp      = static_cast<std::uint32_t>((bits >> mb) & ((bits_t{1} << traits::exponent_bits) - 1));
        ieee_mantissa = static_cast<std::uint64_t>(bits & ((bits_t{1} << mb) - 1));
    }
#endif

    std::int32_t  e2;
    std::uint64_t m2;

    if (ieee_exp == 0) {
        e2 = 1 - bias - mb;
        m2 = ieee_mantissa;
    } else {
        e2 = static_cast<std::int32_t>(ieee_exp) - bias - mb;
        if constexpr (traits::explicit_int_bit) {
            m2 = ieee_mantissa;
        } else {
            m2 = ieee_mantissa | (std::uint64_t{1} << mb);
        }
    }

    if (e2 < -mb) {
        return;
    }

    if (e2 < 0) {
        assign_magnitude(m2 >> static_cast<unsigned>(-e2));
        return;
    }

    const auto assign_general = [&]() {
        const auto limb_idx     = static_cast<unsigned>(e2) / bits_per_limb;
        const auto bit_off      = static_cast<int>(static_cast<unsigned>(e2) % bits_per_limb);
        const auto limbs_needed = limb_idx + 1 + (bit_off > 0 ? 1 : 0);
        grow(limbs_needed);
        auto* const dst = limb_ptr();
        dst[limb_idx] |= m2 << bit_off;
        if (bit_off > 0) {
            dst[limb_idx + 1] |= m2 >> (static_cast<int>(bits_per_limb) - bit_off);
        }
        auto count = static_cast<std::uint32_t>(limb_idx + 1);
        if (bit_off > 0 && dst[limb_idx + 1] != 0) {
            count = static_cast<std::uint32_t>(limb_idx + 2);
        }
        set_limb_count(count);
        while (limb_count() > 1 && dst[limb_count() - 1] == 0) {
            set_limb_count(limb_count() - 1);
        }
    };

#ifdef BEMAN_BIG_INT_HAS_INT128
    constexpr bool is_float32 = std::is_same_v<F, float>
    #ifdef __STDCPP_FLOAT32_T__
                                || std::is_same_v<F, std::float32_t>
    #endif
        ;
    constexpr bool is_float64 = std::is_same_v<F, double>
    #ifdef __STDCPP_FLOAT64_T__
                                || std::is_same_v<F, std::float64_t>
    #endif
        ;

    const auto assign_via_uint128 = [&](auto v) {
        grow(2);
        const auto abs_v = v < decltype(v){0} ? -v : v;
    #ifdef BEMAN_BIG_INT_MSVC
        // note: MSVC's _Unsigned128 has no conversion constructor from FP.
        // So we need to use div/mod to extract high and low 64-bit halves by 2^64.
        constexpr auto two_64 = static_cast<decltype(abs_v)>(1ULL << 32) * static_cast<decltype(abs_v)>(1ULL << 32);
        const auto     hi     = static_cast<limb_type>(abs_v / two_64);
        const auto     lo     = static_cast<limb_type>(abs_v - static_cast<decltype(abs_v)>(hi) * two_64);
    #else
        const auto mag = static_cast<detail::uint128_t>(abs_v);
        const auto hi  = static_cast<limb_type>(mag >> bits_per_limb);
        const auto lo  = static_cast<limb_type>(mag);
    #endif
        limb_ptr()[0] = lo;
        limb_ptr()[1] = hi;
        set_limb_count(hi != 0 ? 2u : 1u);
    };

    if constexpr (is_float32) {
        assign_via_uint128(value);
        return;
    } else if constexpr (is_float64) {
        if (e2 < static_cast<int>(bits_per_limb)) {
            assign_via_uint128(value);
        } else {
            assign_general();
        }
    } else {
        assign_general();
    }
#else
    assign_general();
#endif
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
    if (!is_storage_static()) {
        free_limbs(m_storage.data, m_capacity);
    }
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::grow(const size_type limbs_needed) {
    const size_type current_cap = is_storage_static() ? inplace_capacity : m_capacity;
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
    if !consteval {
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
    if (count >= (is_storage_static() ? inplace_capacity : m_capacity)) {
        grow(count + 1); // exponential growth
    }
    limb_ptr()[count] = limb;
    set_limb_count(static_cast<std::uint32_t>(count + 1));
}

// Standard public alias for defaulted type
using big_int = basic_big_int<64, std::allocator<uint_multiprecision_t>>;

// [big.int.literal]
namespace detail {

// Like `std::from_chars`, but detects the base automatically
// based on the `0x`, `0b`, or `0` prefix.
template <unsigned_integer T>
[[nodiscard]] constexpr std::from_chars_result
from_chars_auto_base(const char* const begin, const char* const end, T& out) {
    if (begin == end) {
        return {end, std::errc::invalid_argument};
    }
    if (*begin != '0' || end - begin <= 1) {
        return std::from_chars(begin, end, out);
    }
    switch (begin[1]) {
    case 'b':
    case 'B':
        return std::from_chars(begin + 2, end, out, 2);
    // In the future, this will also have a case for 'o'
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p0085r3.html
    case 'x':
    case 'X':
        return std::from_chars(begin + 2, end, out, 16);
    default:
        break;
    }
    // This case (leading zero for octal) is deprecated,
    // but we have no real way to communicate that and raise a warning here.
    return std::from_chars(begin, end, out, 8);
}

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")
struct big_int_and_errc {
    big_int   value;
    std::errc ec;
};
BEMAN_BIG_INT_DIAGNOSTIC_POP()

[[nodiscard]] constexpr big_int_and_errc parse_non_allocating(const char* const begin, const char* const end) {
    // TODO(eisenwave): This should support more than a single limb.
    uint_multiprecision_t parsed_limb = 0;
    const auto [p, ec]                = detail::from_chars_auto_base(begin, end, parsed_limb);
    if (ec != std::errc{}) {
        return {0, ec};
    }
    if (p != end) {
        return {0, std::errc::invalid_argument};
    }
    return {.value = parsed_limb, .ec = {}};
}

// Helper variable template which prevents multiple constant evaluations of parse_non_allocating,
// in case compilers don't memoize.
// It also provides some convenience.
template <char... digits>
inline constexpr big_int_and_errc parse_non_allocating_v = [] {
    static constexpr char buffer[]{digits...};
    return parse_non_allocating(buffer, buffer + sizeof(buffer));
}();

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
  noexcept(detail::parse_non_allocating_v<digits...>.ec == std::errc{}) {
    // clang-format on

    // For this user-defined literal, there are two radically distinct situations.
    // We are either able to fit the parsed value into the inplace storage,
    // in which case `operator""n()` simply copies the resulting `big_int`
    // out of a `constexpr` variable;
    // or the parsed value doesn't fit and needs to be dynamically allocated on the fly.
    //
    // This weirdness is caused only by the fact that we don't have non-transient allocations,
    // meaning that we cannot create a `constexpr` variable that holds an allocation.
    // This also prevents us from making `operator""n()` `consteval` rather than `constexpr`.
    // Because it is only `constexpr`, it is important to handle the "small case" specially,
    // so that no runtime parsing takes place.
    if constexpr (detail::parse_non_allocating_v<digits...>.ec == std::errc{}) {
        return detail::parse_non_allocating_v<digits...>.value;
    } else if constexpr (detail::parse_non_allocating_v<digits...>.ec == std::errc::invalid_argument) {
        static_assert(false,
                      "The given literal is not a valid integer-literal. "
                      "This should not even be possible "
                      "without explicitly providing template arguments to the user-defined literal.");
    } else {
        static_assert(detail::parse_non_allocating_v<digits...>.ec == std::errc::result_out_of_range);
        static_assert(false, "Sorry, allocating literals are not supported yet.");
        // TODO: 1. Create a pre-computed constexpr limb array and sign bit.
        //       2. At runtime, create a `big_int` using the constructor taking a limb array.
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
