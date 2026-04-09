// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_STD_BIG_INT_STD_BIG_INT_HPP
#define BEMAN_STD_BIG_INT_STD_BIG_INT_HPP

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace beman {

// alias uint_multiprecision_t
using uint_multiprecision_t = std::uint64_t;

// [big.int.class], class template basic_big_int
//  template<size_t inplace_bits, class Allocator = allocator<uint_multiprecision_t>>
//    class basic_big_int;
template <std::size_t inplace_bits, class Allocator = std::allocator<uint_multiprecision_t>>
class basic_big_int {

public:

    using limb_type = uint_multiprecision_t;
    using double_limb_type = unsigned __int128;
    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<limb_type>;

private:

    using alloc_traits = std::allocator_traits<allocator_type>;
    using limb_pointer = typename std::allocator_traits<allocator_type>::pointer;
    using const_limb_pointer = typename std::allocator_traits<allocator_type>::const_pointer;

    static constexpr std::size_t bits_per_limb = sizeof(limb_type) * CHAR_BIT;

    static constexpr std::size_t inplace_limbs =
        []() constexpr {
            constexpr std::size_t from_bits =
                (inplace_bits + bits_per_limb - 1) / bits_per_limb;
            // never fewer limbs than would fit in the pointer+size_t footprint
            // of limb_data, so the union doesn't waste space
            constexpr std::size_t from_struct =
                (sizeof(limb_pointer) + sizeof(std::size_t)
                 + sizeof(limb_type) - 1) / sizeof(limb_type);
            return from_bits > from_struct ? from_bits : from_struct;
        }();

    static_assert(inplace_bits > 0, "inplace_bits must be positive");

    struct limb_data {
        std::size_t capacity;
        limb_pointer data;
    };

    union data_type {
        limb_type la[inplace_bits];
        limb_data ld;

        constexpr data_type() : la {} {}
    };

    data_type   m_data;
    std::size_t m_limbs;     // number of active limbs
    bool        m_sign;      // true = negative (sign + magnitude)
    bool        m_internal;  // true = using la[], false = using ld
    [[no_unique_address]] allocator_type m_alloc;
};

template <class Allocator = std::allocator<uint_multiprecision_t>>
using big_int = basic_big_int<128U, Allocator>;

} // namespace beman

#endif // BEMAN_STD_BIG_INT_STD_BIG_INT_HPP
