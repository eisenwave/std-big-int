// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_BIG_INT_HPP
#define BEMAN_BIG_INT_BIG_INT_HPP

#include <algorithm>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <span>
#include <concepts>
#include <ranges>

#include <beman/big_int/config.hpp>
#include <beman/big_int/wide_ops.hpp>

namespace beman::big_int {

// alias uint_multiprecision_t
using beman::big_int::uint_multiprecision_t;

// Forward decl so that we can define our concepts
template <std::size_t inplace_bits, class Allocator = std::allocator<uint_multiprecision_t>>
class basic_big_int;

namespace detail {

template <class>
struct is_basic_big_int : std::false_type {};

template <std::size_t inplace_bits, class Allocator>
struct is_basic_big_int<basic_big_int<inplace_bits, Allocator>> : std::true_type {};

template <class T>
inline constexpr bool is_basic_big_int_v = is_basic_big_int<std::remove_cvref_t<T>>::value;

// [big.ing.expos]
template <class T>
concept arbitrary_integer = signed_or_unsigned<std::remove_cvref_t<T>> || detail::is_basic_big_int_v<T>;

template <class T>
concept arbitrary_arithmetic = std::is_floating_point_v<T> || arbitrary_integer<T>;

template <std::size_t inplace_bits, class T>
inline constexpr bool no_alloc_constructible_from = width_v<std::remove_cvref_t<T>> <= inplace_bits;

template <std::size_t inplace_bits, class Allocator, class T>
inline constexpr bool is_implicit_constructible_from =
    detail::signed_or_unsigned<std::remove_cvref_t<T>> ||
    std::is_same_v<std::remove_cvref_t<T>, basic_big_int<inplace_bits, Allocator>>;
} // namespace detail

// [big.int.class], class template basic_big_int
//  template<size_t inplace_bits, class Allocator = allocator<uint_multiprecision_t>>
//    class basic_big_int;
template <std::size_t inplace_bits, class Allocator>
class basic_big_int {

    using limb_type        = uint_multiprecision_t;
    using signed_limb_type = std::make_signed_t<limb_type>;

#ifdef _MSC_VER

    using double_limb_type        = std::_Unsigned128;
    using signed_double_limb_type = std::_Signed128;

#else

    using double_limb_type        = unsigned __int128;
    using signed_double_limb_type = __int128;

#endif

    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<limb_type>;

    using alloc_traits       = std::allocator_traits<allocator_type>;
    using limb_pointer       = typename std::allocator_traits<allocator_type>::pointer;
    using const_limb_pointer = typename std::allocator_traits<allocator_type>::const_pointer;

    static constexpr std::size_t bits_per_limb = sizeof(limb_type) * CHAR_BIT;

    static constexpr std::size_t inplace_limbs = []() constexpr {
        constexpr std::size_t from_bits = (inplace_bits + bits_per_limb - 1) / bits_per_limb;
        // never fewer limbs than would fit in the pointer+size_t footprint
        // of limb_data, so the union doesn't waste space
        constexpr std::size_t from_struct =
            (sizeof(limb_pointer) + sizeof(std::size_t) + sizeof(limb_type) - 1) / sizeof(limb_type);
        return from_bits > from_struct ? from_bits : from_struct;
    }();

    static_assert(inplace_bits > 0, "inplace_bits must be positive");

    struct limb_data {
        std::size_t  capacity;
        limb_pointer data;
    };

    union data_type {
        limb_type la[inplace_bits];
        limb_data ld;

        constexpr data_type() noexcept : la{} {}
        explicit constexpr data_type(const limb_type i) noexcept : la{} { la[0] = i; }
        constexpr data_type(limb_type* limbs, std::size_t len) noexcept : ld{len, limbs} {}
    };

    data_type                            m_data;
    std::size_t                          m_limbs;    // number of active limbs
    bool                                 m_sign;     // true = negative (sign + magnitude)
    bool                                 m_internal; // true = using la[], false = using ld
    [[no_unique_address]] allocator_type m_alloc;

  public:
    // [big.int.cons], construct/copy/destroy
    constexpr basic_big_int() noexcept(noexcept(Allocator())) : m_data{}, m_limbs{1}, m_sign{}, m_internal{true} {}
    constexpr explicit basic_big_int(const Allocator& a) noexcept
        : m_data{}, m_limbs{1}, m_sign{}, m_internal{true}, m_alloc{a} {}
    constexpr basic_big_int(const basic_big_int& x)     = default;
    constexpr basic_big_int(basic_big_int&& x) noexcept = default;

    template <detail::arbitrary_arithmetic T>
        requires(!std::same_as<std::remove_cvref_t<T>, basic_big_int>)
    constexpr explicit(!detail::is_implicit_constructible_from<inplace_bits, Allocator, T>)
        basic_big_int(T&& value) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>)
        : m_data{}, m_limbs{1}, m_sign{false}, m_internal{true}, m_alloc{} {
        if constexpr (std::is_floating_point_v<T>) {
            // TODO: Implement this
            // I think we can go down the RYU route to separate a floating point value into significant, exponent, sign
            // Regardless of method, each of the STLs has a method of accomplishing this already as an implementation
            // detail to <charconv>
            static_assert(false, "This has not been implemented yet");
        } else {
            if constexpr (std::is_signed_v<T>) {
                m_sign  = value < T{0};
                using U = std::make_unsigned_t<T>;
                assign_magnitude(m_sign ? static_cast<U>(-(static_cast<U>(value))) : static_cast<U>(value));
            } else {
                assign_magnitude(value);
            }
        }
    }

    template <detail::arbitrary_arithmetic T>
    constexpr basic_big_int(const T&         value,
                            const Allocator& a) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>)
        : m_data{}, m_limbs{1}, m_sign{false}, m_internal{true}, m_alloc{a} {
        if constexpr (std::is_floating_point_v<T>) {
            // TODO: Implement this
            // See implementation note above
            static_assert(false, "This has not been implemented yet");
        } else {
            if constexpr (std::is_signed_v<T>) {
                m_sign  = value < T{0};
                using U = std::make_unsigned_t<T>;
                assign_magnitude(m_sign ? static_cast<U>(-(static_cast<U>(value))) : static_cast<U>(value));
            } else {
                assign_magnitude(value);
            }
        }
    }

    template <std::ranges::input_range R>
        requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
    constexpr explicit basic_big_int(std::from_range_t, R&& r, const Allocator& a = Allocator())
        : m_data{}, m_limbs{1}, m_sign{false}, m_internal{true}, m_alloc{a} {
        std::size_t i = 0;
        for (auto&& elem : r) {
            using U        = std::make_unsigned_t<std::ranges::range_value_t<R>>;
            m_data.la[i++] = static_cast<limb_type>(static_cast<U>(elem));
        }
        m_limbs = i == 0 ? 1 : i;
        while (m_limbs > 1 && m_data.la[m_limbs - 1] == 0) {
            --m_limbs;
        }
    }

    // TODO(mborland): Add additional constructors from P4444

    constexpr ~basic_big_int() {
        // TODO(mborland): This will probably need to get sliced out into a free_storage() function
        // Good enough to run basic construction tests for now
        if (!m_internal) {
            alloc_traits::deallocate(m_alloc, m_data.ld.data, m_data.ld.capacity);
        }
    }

    // [big.int.ops]
    [[nodiscard]] constexpr std::size_t width_mag() const noexcept {
        const auto top = m_internal ? m_data.la[m_limbs - 1] : m_data.ld.data[m_limbs - 1];
        if (top == 0) {
            return 0;
        }

        return (m_limbs - 1) * bits_per_limb + (bits_per_limb - static_cast<std::size_t>(std::countl_zero(top)) - 1);
    }

    [[nodiscard]] constexpr std::span<const uint_multiprecision_t> representation() const noexcept {
        if (m_internal) {
            return {m_data.la, m_limbs};
        }

        return {m_data.ld.data, m_limbs};
    }

    [[nodiscard]] constexpr allocator_type get_allocator() const noexcept { return m_alloc; }

    // constexpr void shrink_to_fit()

    // [big.int.unary]
    [[nodiscard]] constexpr basic_big_int operator+() const& { return *this; }
    [[nodiscard]] constexpr basic_big_int operator+() && noexcept { return std::move(*this); }

    [[nodiscard]] constexpr basic_big_int operator-() const& {
        auto copy   = *this;
        copy.m_sign = !copy.m_sign;
        return copy;
    }
    [[nodiscard]] constexpr basic_big_int operator-() && noexcept {
        auto copy   = std::move(*this);
        copy.m_sign = !copy.m_sign;
        return copy;
    }

  private:
    template <std::unsigned_integral T>
    constexpr void assign_magnitude(T value) noexcept {
        if constexpr (sizeof(T) <= sizeof(limb_type)) {
            m_data.la[0] = static_cast<limb_type>(value);
            m_limbs      = 1;
        } else {
            constexpr std::size_t n = sizeof(T) / sizeof(limb_type);
            for (std::size_t i = 0; i < n; ++i) {
                m_data.la[i] = static_cast<limb_type>(value);
                value >>= bits_per_limb;
            }
            m_limbs = n;
            while (m_limbs > 1 && m_data.la[m_limbs - 1] == 0) {
                --m_limbs;
            }
        }
    }
};

using big_int = basic_big_int<128U, std::allocator<uint_multiprecision_t>>;

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_BIG_INT_HPP
