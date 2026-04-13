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
#include <type_traits>
#include <span>

#include <beman/big_int/config.hpp>
#include <beman/big_int/wide_ops.hpp>

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
    using pointer        = std::allocator_traits<Allocator>::pointer;
    using const_pointer  = std::allocator_traits<Allocator>::const_pointer;
    static_assert(std::is_same_v<typename Allocator::value_type, uint_multiprecision_t>,
                  "Allocator::value_type must be uint_multiprecision_t.");

    template <std::size_t, class>
    friend class basic_big_int;

  private:
    using alloc_traits = std::allocator_traits<Allocator>;
    using alloc_result = detail::allocation_result<pointer>;

    static constexpr std::size_t bits_per_limb = detail::width_v<limb_type>;
    // Never fewer limbs than would fit in the pointer footprint  of the union,
    // so the union doesn't waste space.
    static constexpr std::size_t inplace_limbs = std::max(detail::div_to_pos_inf(min_inplace_bits, bits_per_limb),
                                                          detail::div_to_pos_inf(sizeof(pointer), sizeof(limb_type)));
    static_assert(min_inplace_bits > 0);
    static_assert(inplace_limbs > 0);

  public:
    static constexpr std::size_t inplace_bits = inplace_limbs * bits_per_limb;

  private:
    union data_type {
        pointer   data;
        limb_type limbs[inplace_limbs];

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
            for (std::size_t i = count; i < limb_count(); ++i) {
                dst[i] = 0;
            }
            set_limb_count(count);
            set_sign(x.is_negative());
        } else {
            using U                   = std::make_unsigned_t<std::remove_cvref_t<T>>;
            const bool            neg = std::is_signed_v<std::remove_cvref_t<T>> && x < std::remove_cvref_t<T>{0};
            const U               mag = neg ? static_cast<U>(U{0} - static_cast<U>(x)) : static_cast<U>(x);
            constexpr std::size_t n   = (sizeof(U) + sizeof(limb_type) - 1) / sizeof(limb_type);
            grow(n);
            const auto old_count = limb_count();
            assign_magnitude(mag);
            auto* dst = limb_ptr();
            for (std::size_t i = limb_count(); i < old_count; ++i) {
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
    [[nodiscard]] constexpr std::size_t                            width_mag() const noexcept;
    [[nodiscard]] constexpr std::span<const uint_multiprecision_t> representation() const noexcept;
    [[nodiscard]] constexpr allocator_type                         get_allocator() const noexcept;
    [[nodiscard]] constexpr std::size_t                            size() const noexcept;
    [[nodiscard]] static constexpr std::size_t                     max_size() noexcept;
    constexpr void                                                 reserve(std::size_t n);
    [[nodiscard]] constexpr std::size_t                            capacity() const noexcept;
    constexpr void                                                 shrink_to_fit();

    // [big.int.unary]
    [[nodiscard]] constexpr basic_big_int operator+() const&;
    [[nodiscard]] constexpr basic_big_int operator+() && noexcept;
    [[nodiscard]] constexpr basic_big_int operator-() const&;
    [[nodiscard]] constexpr basic_big_int operator-() && noexcept;

    constexpr basic_big_int& operator++();
    constexpr basic_big_int  operator++(int);
    constexpr basic_big_int& operator--();
    constexpr basic_big_int  operator--(int);

    // [big.int.cmp]
    template <class L, detail::common_big_int_type_with<L> R>
    friend constexpr bool operator==(const L& lhs, const R& rhs) noexcept;
    template <class L, detail::common_big_int_type_with<L> R>
    friend constexpr std::strong_ordering operator<=>(const L& lhs, const R& rhs) noexcept;

  private:
    template <detail::unsigned_integer T>
    constexpr void assign_magnitude(T value) noexcept;
    template <detail::cv_unqualified_floating_point F>
    constexpr void assign_from_float(F value) noexcept;

    [[nodiscard]] constexpr alloc_result alloc_limbs(std::size_t n);
    constexpr void                       free_limbs(pointer p, std::size_t n);
    constexpr void                       free_storage();
    constexpr void                       grow(std::size_t limbs_needed);
    constexpr void                       copy_n_to_allocation(const limb_type* p, std::size_t n, alloc_result out);

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
    [[nodiscard]] constexpr bool equals_big_int(const basic_big_int& other) const noexcept;
    template <std::size_t extent>
    [[nodiscard]] constexpr bool equals_limbs(std::span<const uint_multiprecision_t, extent> limbs,
                                              bool limbs_negative) const noexcept;

    template <detail::signed_or_unsigned Integer>
    [[nodiscard]] constexpr std::strong_ordering compare_integer(Integer x) const noexcept;
    [[nodiscard]] constexpr std::strong_ordering compare_big_int(const basic_big_int& other) const noexcept;
    template <std::size_t extent>
    [[nodiscard]] constexpr std::strong_ordering compare_limbs(std::span<const uint_multiprecision_t, extent> limbs,
                                                               bool limbs_negative) const noexcept;

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
    constexpr std::size_t negative_zero_size_and_sign = 0x8000'0000U;
    BEMAN_BIG_INT_DEBUG_ASSERT(m_size_and_sign != negative_zero_size_and_sign);
    return m_size_and_sign & 0x7FFF'FFFFU;
}

template <std::size_t b, class A>
constexpr bool basic_big_int<b, A>::is_negative() const noexcept {
    constexpr std::size_t negative_zero_size_and_sign = 0x8000'0000U;
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
    if (x.limb_count() <= inplace_limbs) {
        if (x.is_storage_static()) {
            for (std::size_t i = 0; i < inplace_limbs; ++i) {
                m_storage.limbs[i] = x.m_storage.limbs[i];
            }
        } else {
            // This case can happen if e.g. `x.reserve(100)` was called
            // but the integer value of `x` fits into inplace storage.
            for (std::size_t i = 0; i < x.limb_count(); ++i) {
                m_storage.limbs[i] = x.m_storage.data[i];
            }
            for (std::size_t i = x.limb_count(); i < inplace_limbs; ++i) {
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
        for (std::size_t i = 0; i < inplace_limbs; ++i) {
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
        for (std::size_t i = 0; i < inplace_limbs; ++i) {
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
        for (std::size_t i = 0; i < inplace_limbs; ++i) {
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
    std::size_t i = 0;

    if constexpr (std::ranges::sized_range<R>) {
        reserve(std::ranges::size(r));
    }

    // FIXME: Buffer overflow for the unsized range case.
    //        What we actually need here is some kind of push_back_limb() function.

    auto* const dst = limb_ptr();
    for (auto&& elem : r) {
        using U = std::make_unsigned_t<std::ranges::range_value_t<R>>;
        std::construct_at(dst + i++, static_cast<limb_type>(static_cast<U>(elem)));
    }
    set_limb_count(static_cast<std::uint32_t>(i == 0 ? 1 : i));
    while (limb_count() > 1 && dst[limb_count() - 1] == 0) {
        set_limb_count(limb_count() - 1);
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
    for (std::size_t i = 0; carry_in && i < limb_count(); ++i) {
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
    for (std::size_t i = 0; borrow_in && i < limb_count(); ++i) {
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
    reserve(limb_count() + shifted_limbs + std::size_t(shifted_bits != 0));
    limb_type* const limbs = limb_ptr();

    if (shifted_limbs != 0) {
        std::shift_right(limbs, limbs + limb_count(), static_cast<std::ptrdiff_t>(shifted_limbs));
        std::fill_n(limbs, static_cast<std::ptrdiff_t>(shifted_limbs), limb_type{0});
        set_limb_count(static_cast<std::uint32_t>(limb_count() + shifted_limbs));
    }
    if (shifted_bits != 0) {
        for (std::size_t i = limb_count() + 1; i-- > shifted_limbs; ++i) {
            const detail::wide<limb_type> all_bits{.low_bits = limbs[i], .high_bits = limbs[i - 1]};
            limbs[i] = detail::funnel_shl(all_bits, static_cast<unsigned int>(shifted_bits));
        }
        set_limb_count(limb_count() + 1);
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
        for (std::size_t i = 0; i < shifted_limbs; ++i) {
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
        std::shift_left(limbs, limbs + limb_count(), static_cast<std::ptrdiff_t>(shifted_limbs));
        set_limb_count(std::min(std::uint32_t{1}, static_cast<std::uint32_t>(limb_count() - shifted_limbs)));
    }
    if (shifted_bits != 0) {
        for (std::size_t i = 0; i + 1 < limb_count(); ++i) {
            const detail::wide<limb_type> all_bits{.low_bits = limbs[i], .high_bits = limbs[i] + 1};
            limbs[i] = detail::funnel_shr(all_bits, static_cast<unsigned int>(shifted_bits));
        }
        BEMAN_BIG_INT_DEBUG_ASSERT(limb_count() != 0);
        limbs[limb_count() - 1] >>= shifted_bits;
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

    return (count - 1) * bits_per_limb + (bits_per_limb - static_cast<std::size_t>(std::countl_zero(top)) - 1);
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
        return inplace_limbs;
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
constexpr void basic_big_int<b, A>::reserve(const std::size_t n) {
    grow(n);
}

template <std::size_t b, class A>
constexpr std::size_t basic_big_int<b, A>::capacity() const noexcept {
    return m_capacity;
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::shrink_to_fit() {
    const auto count = limb_count();

    if (is_storage_static() || m_capacity <= count) {
        return;
    }

    if (count <= inplace_limbs) {
        // Move back to inline storage
        // We need a manual loop to switch the active union member in consteval context
        // At runtime this should become equivalent to std::uninitialized_copy_n
        pointer    old_data = m_storage.data;
        const auto old_cap  = m_capacity;
        m_capacity          = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            m_storage.limbs[i] = old_data[i];
        }
        for (std::size_t i = count; i < inplace_limbs; ++i) {
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
constexpr auto basic_big_int<b, A>::operator++() -> basic_big_int& {
    if (is_negative()) {
        if (unchecked_decrement_magnitude()) {
            set_sign(false);
        }
    } else {
        unchecked_increment_magnitude();
    }
    return *this;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator++(int) -> basic_big_int {
    auto copy = *this;
    ++(*this);
    return copy;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator--() -> basic_big_int& {
    if (is_negative()) {
        unchecked_increment_magnitude();
    } else {
        unchecked_decrement_magnitude();
    }
    return *this;
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::operator--(int) -> basic_big_int {
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
        static_assert((0 <=> std::strong_ordering::less) == std::strong_ordering::greater,
                      "This trick to flip the ordering should work.");
        return 0 <=> rhs.compare_integer(lhs);
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

} // namespace detail

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

    std::size_t       i    = 0;
    const auto* const self = limb_ptr();
    // In the limbs that are common,
    // there can be no mismatch.
    for (; i < limbs.size() && i < limb_count(); ++i) {
        if (self[i] != limbs[i]) {
            return false;
        }
    }
    // The provided limbs can have additional ignored zeroes.
    for (; i < limbs.size(); ++i) {
        if (limbs[i] != 0) {
            return false;
        }
    }
    // Our own limbs can have additional ignored zeroes.
    for (; i < limb_count(); ++i) {
        if (self[i] != 0) {
            return false;
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
                if (sign_compare != 0) {
                    return sign_compare;
                }
                return inplace_to_bit_uint() <=> detail::uabs(x);
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
    if (sign_compare != 0) {
        return sign_compare;
    }
    const auto rep = representation();

    // If there are more significant nonzero digits in limbs, this integer is lower.
    // Decimal example: 23 < 123
    for (std::size_t i = limbs.size(); i-- > rep.size();) {
        if (limbs[i] != 0) {
            return std::strong_ordering::less;
        }
    }
    // If there are more significant nonzero digits in this integer, it is greater.
    // Decimal example: 123 > 23
    for (std::size_t i = rep.size(); i-- > limbs.size();) {
        if (rep[i] != 0) {
            return std::strong_ordering::greater;
        }
    }
    // Otherwise, we nee need to compare the common digits to one another,
    // from most significant to least significant.
    for (std::size_t i = std::min(limbs.size(), rep.size()); i-- > 0;) {
        const auto result = rep[i] <=> limbs[i];
        if (result != 0) {
            return result;
        }
    }
    // Having eliminated any possible mismatch, the two sides are equal.
    return std::strong_ordering::equal;
}

// private helpers

template <std::size_t b, class A>
template <detail::unsigned_integer T>
constexpr void basic_big_int<b, A>::assign_magnitude(const T value) noexcept {
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
    BEMAN_BIG_INT_ASSERT(std::isfinite(value));

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

    const auto limb_idx = static_cast<unsigned>(e2) / bits_per_limb;
    // TODO(alcxpr): Only grow if actually needed.
    //               This hotfix was needed to prevent stack buffer overflow in tests.
    grow(limb_idx + 2);

    const auto  bit_off = static_cast<int>(static_cast<unsigned>(e2) % bits_per_limb);
    auto* const dst     = limb_ptr();

    dst[limb_idx] |= m2 << bit_off;
    if (bit_off > 0 && limb_idx + 1 < inplace_limbs) {
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
}

template <std::size_t b, class A>
constexpr auto basic_big_int<b, A>::alloc_limbs(const std::size_t n) -> alloc_result {
    BEMAN_BIG_INT_ASSERT(n != 0);
#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
    return alloc_traits::allocate_at_least(m_alloc, n);
#else
    return {.ptr = alloc_traits::allocate(m_alloc, n), .count = n};
#endif
}

template <std::size_t b, class A>
constexpr void basic_big_int<b, A>::free_limbs(pointer p, const std::size_t n) {
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
constexpr void basic_big_int<b, A>::grow(const std::size_t limbs_needed) {
    const std::size_t current_cap = is_storage_static() ? inplace_limbs : m_capacity;
    if (limbs_needed <= current_cap) {
        return;
    }

    // libstdc++ and libc++ normally double storage each allocation
    // MSVC does 1.5x instead of 2x
    const std::size_t  new_cap    = std::max(limbs_needed, 2 * current_cap);
    const alloc_result allocation = alloc_limbs(new_cap);
    copy_n_to_allocation(limb_ptr(), limb_count(), allocation);

    free_storage();

    m_storage.data = allocation.ptr;
    m_capacity     = static_cast<std::uint32_t>(allocation.count);
}

template <std::size_t b, class A>
constexpr void
basic_big_int<b, A>::copy_n_to_allocation(const limb_type* const p, const std::size_t n, const alloc_result out) {
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
        for (std::size_t i = 0; i < n; ++i) {
            std::construct_at(out.ptr + i, p[i]);
        }
        for (std::size_t i = n; i < out.count; ++i) {
            std::construct_at(out.ptr + i);
        }
    }
#endif
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

#endif // BEMAN_BIG_INT_BIG_INT_HPP
