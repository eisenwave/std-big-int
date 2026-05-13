// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP
#define BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP

#include <beman/big_int/detail/config.hpp>
#include <span>

namespace beman::big_int::detail {

// Scratchpad using the provided allocator from basic_big_int
// This is a bump allocator using LIFO storage
//
// Multiplication and division algorithms allocate temporaries from this,
// then "deallocates" after each level returns so sibling branches reuse the same memory.
// This deallocation is simply moving the pointer back
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

template <class Allocator>
struct scratch_allocator {
    using alloc_traits = std::allocator_traits<Allocator>;
    using pointer      = typename alloc_traits::pointer;

    BEMAN_BIG_INT_NO_UNIQUE_ADDRESS Allocator m_alloc;
    pointer                                   m_base;
    std::size_t                               m_capacity;
    std::size_t                               m_offset = 0;
    bool                                      m_owns;

    // Wrap an existing stack buffer — no ownership.
    constexpr scratch_allocator(pointer buf, const std::size_t cap, const Allocator& alloc) noexcept
        : m_alloc(alloc), m_base(buf), m_capacity(cap), m_owns(false) {}

    // Heap-allocate at least `cap` limbs using the provided allocator.
    constexpr scratch_allocator(std::size_t cap, const Allocator& alloc)
        : m_alloc(alloc), m_base(nullptr), m_capacity(0), m_owns(true) {
#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
        if constexpr (traits_has_allocate_at_least<alloc_traits, Allocator>) {
            auto result = alloc_traits::allocate_at_least(m_alloc, cap);
            m_base      = result.ptr;
            m_capacity  = result.count;
        } else {
            m_base     = alloc_traits::allocate(m_alloc, cap);
            m_capacity = cap;
        }
#else
        m_base     = alloc_traits::allocate(m_alloc, cap);
        m_capacity = cap;
#endif
    }

    constexpr ~scratch_allocator() {
        if (m_owns) {
            alloc_traits::deallocate(m_alloc, m_base, m_capacity);
        }
    }

    scratch_allocator(const scratch_allocator&)            = delete;
    scratch_allocator& operator=(const scratch_allocator&) = delete;

    // Bump-allocate `n` limbs from the workspace, returned as a mutable span.
    constexpr std::span<uint_multiprecision_t> allocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(m_offset + n <= m_capacity);
        auto* p = std::to_address(m_base) + m_offset;
        m_offset += n;
        return {p, n};
    }

    // LIFO release of the last `n` limbs so sibling branches can reuse them.
    constexpr void deallocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(n <= m_offset);
        m_offset -= n;
    }
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP
