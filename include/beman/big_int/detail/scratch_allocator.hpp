// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP
#define BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP

#include <beman/big_int/detail/config.hpp>

#include <memory>
#include <span>

namespace beman::big_int::detail {

// Scratchpad bump allocator used by the multiplication and division
// algorithms. Allocates LIFO from a pre-sized buffer; "deallocation" simply
// rewinds the bump pointer so sibling recursive branches reuse the same memory.
//
// The base class holds the bump-pointer state and the type-erased
// allocate/deallocate interface that the algorithms call per recursive level.
// The templated derived class owns the user-provided Allocator and performs
// the initial heap allocation / final deallocation. Splitting in this way lets
// the algorithm implementations live in non-template .cpp files: they only
// touch the base, so a single compiled definition serves every allocator type.
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

struct scratch_allocator_base {
    uint_multiprecision_t* m_base     = nullptr;
    std::size_t            m_capacity = 0;
    std::size_t            m_offset   = 0;

#ifdef BEMAN_BIG_INT_INSTRUMENT
    // Running high-water mark of `m_offset` across the lifetime of this allocator.
    // Used by tests/benchmarks to size _storage_size heuristics; cost is one
    // compare-and-store per allocate(), which is amortized over a whole recursive
    // multiplication or division and so is well below measurement noise.
    std::size_t m_peak = 0;
#endif

    constexpr scratch_allocator_base() noexcept = default;
    constexpr scratch_allocator_base(uint_multiprecision_t* base, const std::size_t capacity) noexcept
        : m_base(base), m_capacity(capacity) {}

    scratch_allocator_base(const scratch_allocator_base&)            = delete;
    scratch_allocator_base& operator=(const scratch_allocator_base&) = delete;

    // Bump-allocate `n` limbs from the workspace, returned as a mutable span.
    constexpr std::span<uint_multiprecision_t> allocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(m_offset + n <= m_capacity);
        auto* p = m_base + m_offset;
        m_offset += n;

#ifdef BEMAN_BIG_INT_INSTRUMENT
        if (m_offset > m_peak) {
            m_peak = m_offset;
        }
#endif

        return {p, n};
    }

    // LIFO release of the last `n` limbs so sibling branches can reuse them.
    constexpr void deallocate(const std::size_t n) noexcept {
        BEMAN_BIG_INT_DEBUG_ASSERT(n <= m_offset);
        m_offset -= n;
    }

#ifdef BEMAN_BIG_INT_INSTRUMENT
    // Highest value `m_offset` has reached since construction. Used to tune the
    // _storage_size heuristics in mul_impl.hpp.
    [[nodiscard]] constexpr std::size_t peak() const noexcept { return m_peak; }
#endif
};

template <class Allocator>
struct scratch_allocator : scratch_allocator_base {
    using alloc_traits = std::allocator_traits<Allocator>;
    using pointer      = typename alloc_traits::pointer;

    // NOTE: deliberately plain [[no_unique_address]], NOT BEMAN_BIG_INT_NO_UNIQUE_ADDRESS.
    // BEMAN_BIG_INT_NO_UNIQUE_ADDRESS expands to [[msvc::no_unique_address]] which miscompiles leading to segfault
    // MSVC ignores plain [[no_unique_address]] which works fine (and correctly) with GCC and Clang
    [[no_unique_address]] Allocator m_alloc;
    pointer                         m_owned_pointer = pointer{};
    bool                            m_owns;

    // Wrap an existing stack buffer — no ownership.
    constexpr scratch_allocator(pointer buf, const std::size_t cap, const Allocator& alloc) noexcept
        : scratch_allocator_base(std::to_address(buf), cap), m_alloc(alloc), m_owns(false) {}

    // Heap-allocate at least `cap` limbs using the provided allocator.
    constexpr scratch_allocator(std::size_t cap, const Allocator& alloc) : m_alloc(alloc), m_owns(true) {
#if defined(__cpp_lib_allocate_at_least) && __cpp_lib_allocate_at_least >= 202302L
        if constexpr (traits_has_allocate_at_least<alloc_traits, Allocator>) {
            auto result     = alloc_traits::allocate_at_least(m_alloc, cap);
            m_owned_pointer = result.ptr;
            m_capacity      = result.count;
        } else {
            m_owned_pointer = alloc_traits::allocate(m_alloc, cap);
            m_capacity      = cap;
        }
#else
        m_owned_pointer = alloc_traits::allocate(m_alloc, cap);
        m_capacity      = cap;
#endif
        m_base = std::to_address(m_owned_pointer);

        // At constant evaluation the allocated limbs' lifetimes have not
        // begun, so plain assignment into them (std::ranges::copy/fill in the
        // algorithms) is ill-formed; start them here. Free at runtime.
        if BEMAN_BIG_INT_IS_CONSTEVAL {
            for (std::size_t i = 0; i < m_capacity; ++i) {
                std::construct_at(m_base + i, uint_multiprecision_t{0});
            }
        }
    }

    constexpr ~scratch_allocator() {
        if (m_owns) {
            alloc_traits::deallocate(m_alloc, m_owned_pointer, m_capacity);
        }
    }

    scratch_allocator(const scratch_allocator&)            = delete;
    scratch_allocator& operator=(const scratch_allocator&) = delete;
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SCRATCH_ALLOCATOR_HPP
