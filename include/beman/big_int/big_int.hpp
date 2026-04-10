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
class BEMAN_BIG_INT_TRIVIAL_ABI basic_big_int {

    using limb_type               = uint_multiprecision_t;
    using double_limb_type        = detail::uint128_t;
    using signed_limb_type        = std::make_signed_t<limb_type>;
    using signed_double_limb_type = detail::int128_t;

    using allocator_type     = std::allocator_traits<Allocator>::template rebind_alloc<limb_type>;
    using alloc_traits       = std::allocator_traits<allocator_type>;
    using limb_pointer       = std::allocator_traits<allocator_type>::pointer;
    using const_limb_pointer = std::allocator_traits<allocator_type>::const_pointer;

    static constexpr std::size_t bits_per_limb = sizeof(limb_type) * CHAR_BIT;

    static constexpr std::size_t inplace_limbs = []() constexpr {
        constexpr std::size_t from_bits = (inplace_bits + bits_per_limb - 1) / bits_per_limb;
        // never fewer limbs than would fit in the pointer footprint
        // of the union, so the union doesn't waste space
        constexpr std::size_t from_pointer = (sizeof(limb_type*) + sizeof(limb_type) - 1) / sizeof(limb_type);
        return from_bits > from_pointer ? from_bits : from_pointer;
    }();

    static_assert(inplace_bits > 0, "inplace_bits must be positive");

    union data_type {
        limb_type* data;
        limb_type  limbs[inplace_limbs];

        constexpr data_type() noexcept : limbs{} {}
    };

    std::uint32_t                        m_capacity;      // 0 = static storage, >0 = heap capacity
    std::uint32_t                        m_size_and_sign; // bit 31 = sign, bits 0-30 = limb count
    data_type                            m_storage;
    [[no_unique_address]] allocator_type m_alloc;

    // Internal accessors for the packed representation
    [[nodiscard]] constexpr bool             is_storage_static() const noexcept;
    [[nodiscard]] constexpr std::uint32_t    limb_count() const noexcept;
    [[nodiscard]] constexpr bool             is_negative() const noexcept;
    constexpr void                           set_limb_count(std::uint32_t n) noexcept;
    constexpr void                           set_sign(bool s) noexcept;
    [[nodiscard]] constexpr limb_type*       limb_ptr() noexcept;
    [[nodiscard]] constexpr const limb_type* limb_ptr() const noexcept;

  public:
    // [big.int.cons], construct/copy/destroy
    constexpr basic_big_int() noexcept(noexcept(Allocator())) : m_capacity{0}, m_size_and_sign{1}, m_storage{} {}
    constexpr explicit basic_big_int(const Allocator& a) noexcept
        : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {}
    constexpr basic_big_int(const basic_big_int& x)     = default;
    constexpr basic_big_int(basic_big_int&& x) noexcept = default;

    // Defined inline: MSVC cannot match out-of-line definitions
    // of constructors with conditional explicit + requires.
    template <detail::arbitrary_arithmetic T>
        requires(!std::same_as<std::remove_cvref_t<T>, basic_big_int>)
    constexpr explicit(!detail::is_implicit_constructible_from<inplace_bits, Allocator, T>)
        basic_big_int(T&& value) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>)
        : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{} {
        if constexpr (std::is_floating_point_v<T>) {
            // TODO: Implement this
            // I think we can go down the RYU route to separate a floating point value into significant, exponent,
            // sign. Regardless of method, each of the STLs has a method of accomplishing this already as an
            // implementation detail to <charconv>
            static_assert(false, "This has not been implemented yet");
        } else {
            if constexpr (std::is_signed_v<T>) {
                set_sign(value < T{0});
                using U = std::make_unsigned_t<T>;
                assign_magnitude(is_negative() ? static_cast<U>(-(static_cast<U>(value))) : static_cast<U>(value));
            } else {
                assign_magnitude(value);
            }
        }
    }

    template <detail::arbitrary_arithmetic T>
    constexpr basic_big_int(const T&         value,
                            const Allocator& a) noexcept(detail::no_alloc_constructible_from<inplace_bits, T>);

    template <std::ranges::input_range R>
        requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
    constexpr explicit basic_big_int(std::from_range_t, R&& r, const Allocator& a = Allocator());

    // TODO(mborland): Add additional constructors from P4444

    constexpr ~basic_big_int();

    // [big.int.ops]
    [[nodiscard]] constexpr        std::size_t                            width_mag() const noexcept;
    [[nodiscard]] constexpr        std::span<const uint_multiprecision_t> representation() const noexcept;
    [[nodiscard]] constexpr        allocator_type                         get_allocator() const noexcept;
    [[nodiscard]] constexpr        std::size_t                            size() const noexcept;
    [[nodiscard]] static constexpr std::size_t                            max_size() noexcept;
    constexpr void                                                        reserve(std::size_t n);
    [[nodiscard]] constexpr        std::size_t                            capacity() const noexcept;
    constexpr void                                                        shrink_to_fit();

    // [big.int.unary]
    [[nodiscard]] constexpr basic_big_int operator+() const&;
    [[nodiscard]] constexpr basic_big_int operator+() && noexcept;
    [[nodiscard]] constexpr basic_big_int operator-() const&;
    [[nodiscard]] constexpr basic_big_int operator-() && noexcept;

  private:
    template <std::unsigned_integral T>
    constexpr void assign_magnitude(T value) noexcept;

    [[nodiscard]] constexpr limb_pointer       alloc_limbs(std::size_t n);
    constexpr void                             free_limbs(limb_pointer p, std::size_t n);
    constexpr void                             free_storage();
    constexpr void                             grow(std::size_t limbs_needed);
};

// =============================================================================
// Out-of-class definitions
// =============================================================================

// Internal accessors

template <std::size_t inplace_bits, class Allocator>
constexpr bool basic_big_int<inplace_bits, Allocator>::is_storage_static() const noexcept {
    return m_capacity == 0;
}

template <std::size_t inplace_bits, class Allocator>
constexpr std::uint32_t basic_big_int<inplace_bits, Allocator>::limb_count() const noexcept {
    return m_size_and_sign & 0x7FFF'FFFFU;
}

template <std::size_t inplace_bits, class Allocator>
constexpr bool basic_big_int<inplace_bits, Allocator>::is_negative() const noexcept {
    return (m_size_and_sign & 0x8000'0000U) != 0;
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::set_limb_count(std::uint32_t n) noexcept {
    m_size_and_sign = (m_size_and_sign & 0x8000'0000U) | n;
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::set_sign(bool s) noexcept {
    m_size_and_sign = (m_size_and_sign & 0x7FFF'FFFFU) | (static_cast<std::uint32_t>(s) << 31);
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator>::limb_type*
basic_big_int<inplace_bits, Allocator>::limb_ptr() noexcept {
    return is_storage_static() ? m_storage.limbs : m_storage.data;
}

template <std::size_t inplace_bits, class Allocator>
constexpr const basic_big_int<inplace_bits, Allocator>::limb_type*
basic_big_int<inplace_bits, Allocator>::limb_ptr() const noexcept {
    return is_storage_static() ? m_storage.limbs : m_storage.data;
}

// [big.int.cons] — constructors

template <std::size_t inplace_bits, class Allocator>
template <detail::arbitrary_arithmetic T>
constexpr basic_big_int<inplace_bits, Allocator>::basic_big_int(const T& value, const Allocator& a) noexcept(
    detail::no_alloc_constructible_from<inplace_bits, T>)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {
    if constexpr (std::is_floating_point_v<T>) {
        // TODO: Implement this
        // See implementation note above
        static_assert(false, "This has not been implemented yet");
    } else {
        if constexpr (std::is_signed_v<T>) {
            set_sign(value < T{0});
            using U = std::make_unsigned_t<T>;
            assign_magnitude(is_negative() ? static_cast<U>(-(static_cast<U>(value))) : static_cast<U>(value));
        } else {
            assign_magnitude(value);
        }
    }
}

template <std::size_t inplace_bits, class Allocator>
template <std::ranges::input_range R>
    requires detail::signed_or_unsigned<std::ranges::range_value_t<R>>
constexpr basic_big_int<inplace_bits, Allocator>::basic_big_int(std::from_range_t, R&& r, const Allocator& a)
    : m_capacity{0}, m_size_and_sign{1}, m_storage{}, m_alloc{a} {
    std::size_t i = 0;

    if constexpr (std::ranges::sized_range<R>) {
        const auto count = std::ranges::size(r);
        if (count > inplace_limbs) {
            m_capacity     = static_cast<std::uint32_t>(count);
            m_storage.data = alloc_traits::allocate(m_alloc, count);
        }
    }

    auto* dst = limb_ptr();
    for (auto&& elem : r) {
        using U  = std::make_unsigned_t<std::ranges::range_value_t<R>>;
        dst[i++] = static_cast<limb_type>(static_cast<U>(elem));
    }
    set_limb_count(static_cast<std::uint32_t>(i == 0 ? 1 : i));
    while (limb_count() > 1 && dst[limb_count() - 1] == 0) {
        set_limb_count(limb_count() - 1);
    }
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator>::~basic_big_int() {
    free_storage();
}

// [big.int.ops]

template <std::size_t inplace_bits, class Allocator>
constexpr std::size_t basic_big_int<inplace_bits, Allocator>::width_mag() const noexcept {
    const auto count = limb_count();
    const auto top   = limb_ptr()[count - 1];
    if (top == 0) {
        return 0;
    }

    return (count - 1) * bits_per_limb + (bits_per_limb - static_cast<std::size_t>(std::countl_zero(top)) - 1);
}

template <std::size_t inplace_bits, class Allocator>
constexpr std::span<const uint_multiprecision_t>
basic_big_int<inplace_bits, Allocator>::representation() const noexcept {
    return {limb_ptr(), limb_count()};
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator>::allocator_type
basic_big_int<inplace_bits, Allocator>::get_allocator() const noexcept {
    return m_alloc;
}

template <std::size_t inplace_bits, class Allocator>
constexpr std::size_t basic_big_int<inplace_bits, Allocator>::size() const noexcept {
    if (is_storage_static()) {
        return inplace_limbs;
    } else {
        return limb_count();
    }
}

template <std::size_t inplace_bits, class Allocator>
constexpr std::size_t basic_big_int<inplace_bits, Allocator>::max_size() noexcept {
    // We use the high bit to encode the sign, so we are limited to 2^31
    return std::numeric_limits<std::uint32_t>::max() >> 1U;
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::reserve(const std::size_t n) {
    grow(n);
}

template <std::size_t inplace_bits, class Allocator>
constexpr std::size_t basic_big_int<inplace_bits, Allocator>::capacity() const noexcept {
    return m_capacity;
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::shrink_to_fit() {
    const auto count = limb_count();

    if (is_storage_static() || m_capacity <= count) {
        return;
    }

    if (count <= inplace_limbs) {
        // Move back to inline storage
        limb_pointer old_data = m_storage.data;
        const auto   old_cap  = m_capacity;
        std::copy_n(old_data, count, m_storage.limbs);
        m_capacity = 0;
        free_limbs(old_data, old_cap);
    } else {
        // Reallocate to a smaller heap buffer
        limb_pointer new_data = alloc_limbs(count);
        std::copy_n(m_storage.data, count, new_data);
        free_limbs(m_storage.data, m_capacity);
        m_storage.data = new_data;
        m_capacity     = static_cast<std::uint32_t>(count);
    }
}

// [big.int.unary]

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator> basic_big_int<inplace_bits, Allocator>::operator+() const& {
    return *this;
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator> basic_big_int<inplace_bits, Allocator>::operator+() && noexcept {
    return std::move(*this);
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator> basic_big_int<inplace_bits, Allocator>::operator-() const& {
    auto copy = *this;
    copy.set_sign(!copy.is_negative());
    return copy;
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator> basic_big_int<inplace_bits, Allocator>::operator-() && noexcept {
    auto copy = std::move(*this);
    copy.set_sign(!copy.is_negative());
    return copy;
}

// private helpers

template <std::size_t inplace_bits, class Allocator>
template <std::unsigned_integral T>
constexpr void basic_big_int<inplace_bits, Allocator>::assign_magnitude(T value) noexcept {
    if constexpr (sizeof(T) <= sizeof(limb_type)) {
        m_storage.limbs[0] = static_cast<limb_type>(value);
        set_limb_count(1);
    } else {
        constexpr std::size_t n = sizeof(T) / sizeof(limb_type);
        for (std::size_t i = 0; i < n; ++i) {
            m_storage.limbs[i] = static_cast<limb_type>(value);
            value >>= bits_per_limb;
        }
        set_limb_count(static_cast<std::uint32_t>(n));
        while (limb_count() > 1 && m_storage.limbs[limb_count() - 1] == 0) {
            set_limb_count(limb_count() - 1);
        }
    }
}

template <std::size_t inplace_bits, class Allocator>
constexpr basic_big_int<inplace_bits, Allocator>::limb_pointer
basic_big_int<inplace_bits, Allocator>::alloc_limbs(const std::size_t n) {
    if consteval {
        return new limb_type[n]{};
    } else {
        return alloc_traits::allocate(m_alloc, n);
    }
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::free_limbs(limb_pointer p, const std::size_t n) {
    if consteval {
        delete[] p;
    } else {
        alloc_traits::deallocate(m_alloc, p, n);
    }
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::free_storage() {
    if (!is_storage_static()) {
        free_limbs(m_storage.data, m_capacity);
    }
}

template <std::size_t inplace_bits, class Allocator>
constexpr void basic_big_int<inplace_bits, Allocator>::grow(const std::size_t limbs_needed) {
    const std::size_t current_cap = is_storage_static() ? inplace_limbs : m_capacity;
    if (limbs_needed <= current_cap) {
        return;
    }

    // libstdc++ and libc++ normally double storage each allocation
    // MSVC does 1.5x instead of 2x
    const std::size_t new_cap  = std::max(limbs_needed, 2 * current_cap);
    limb_pointer      new_data = alloc_limbs(new_cap);

    std::copy_n(limb_ptr(), limb_count(), new_data);
    free_storage();

    m_storage.data = new_data;
    m_capacity     = static_cast<std::uint32_t>(new_cap);
}

// Standard public alias for defaulted type
using big_int = basic_big_int<128U, std::allocator<uint_multiprecision_t>>;

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_BIG_INT_HPP
