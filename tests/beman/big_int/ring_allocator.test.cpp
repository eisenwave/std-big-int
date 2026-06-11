// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//
// A user-defined ring allocator driven through every division method, at
// sizes that route every dispatch tier (schoolbook, divide-and-conquer with
// the divappr quotient-only path, and the Barrett driver with its Newton
// reciprocal and wrapped products). The point of the exercise: division's
// compiled-once kernels reach all of their workspaces -- limb scratch, the
// internal products, and the FFT tiers -- through the type-erased heap
// hooks, so a custom allocator must work out of the box and observe every
// allocation through its own rebinds. Results are cross-checked against
// std::allocator big_ints, which would catch any corruption from the ring's
// wraparound reuse.

#include <beman/big_int/big_int.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using beman::big_int::big_int;
using beman::big_int::div_rem_to_zero;
using beman::big_int::to_string;
using uint_t = beman::big_int::uint_multiprecision_t;

// A fixed-capacity ring: allocations bump a head offset, aligned per type,
// and wrap to the start when the tail is too small. Deallocation only
// retires bookkeeping (a ring reclaims by overwrite), so the test relies on
// the result cross-checks to prove no live data was ever overwritten, and
// on the outstanding counter to prove the hooks release everything.
struct ring_arena {
    explicit ring_arena(const std::size_t capacity) : buffer(capacity) {}

    std::vector<std::byte> buffer;
    std::size_t            head             = 0;
    std::size_t            outstanding      = 0;
    std::size_t            peak_outstanding = 0;
    std::size_t            allocations      = 0;
};

template <class T>
struct ring_allocator {
    using value_type = T;

    ring_arena* arena = nullptr;

    ring_allocator() = default;
    explicit ring_allocator(ring_arena* a) : arena(a) {}
    template <class U>
    ring_allocator(const ring_allocator<U>& other) : arena(other.arena) {}

    T* allocate(const std::size_t n) {
        const std::size_t bytes = n * sizeof(T);
        std::size_t       at    = (arena->head + alignof(T) - 1) & ~(alignof(T) - 1);
        if (at + bytes > arena->buffer.size()) {
            at = 0; // wrap
            if (bytes > arena->buffer.size()) {
                throw std::bad_alloc{};
            }
        }
        arena->head = at + bytes;
        arena->outstanding += bytes;
        arena->peak_outstanding = std::max(arena->peak_outstanding, arena->outstanding);
        ++arena->allocations;
        return reinterpret_cast<T*>(arena->buffer.data() + at);
    }

    void deallocate(T*, const std::size_t n) noexcept { arena->outstanding -= n * sizeof(T); }

    template <class U>
    bool operator==(const ring_allocator<U>& other) const {
        return arena == other.arena;
    }
};

using ring_big_int = beman::big_int::basic_big_int<big_int::inplace_bits, ring_allocator<uint_t>>;

big_int random_value(const std::size_t limbs, std::mt19937_64& rng, const bool negative) {
    big_int x = 1;
    for (std::size_t i = 0; i < limbs; ++i) {
        x <<= 64;
        x += rng();
    }
    return negative ? -x : x;
}

// Runs every division method through the ring at one shape and cross-checks
// against the std::allocator reference results.
void check_division_methods(const big_int& ref_a, const big_int& ref_b, ring_arena& arena) {
    const ring_allocator<uint_t> alloc(&arena);

    const ring_big_int a{ref_a, alloc};
    const ring_big_int b{ref_b, alloc};

    const big_int ref_q = ref_a / ref_b;
    const big_int ref_r = ref_a % ref_b;

    // operator/ (the quotient-only divappr path in the dc band).
    EXPECT_EQ(to_string(a / b), to_string(ref_q));
    // operator%.
    EXPECT_EQ(to_string(a % b), to_string(ref_r));
    // div_rem_to_zero (the exact divmod path).
    const auto qr = div_rem_to_zero(a, b);
    EXPECT_EQ(to_string(qr.quotient), to_string(ref_q));
    EXPECT_EQ(to_string(qr.remainder), to_string(ref_r));
    // Compound assignments divide through *this.
    {
        ring_big_int q{a};
        q /= b;
        EXPECT_EQ(to_string(q), to_string(ref_q));
    }
    {
        ring_big_int r{a};
        r %= b;
        EXPECT_EQ(to_string(r), to_string(ref_r));
    }
}

TEST(RingAllocator, AllDivisionMethodsThroughEveryTier) {
    // 64 MiB ring: comfortably above any single shape's live footprint, so
    // wraparound reuse never lands on live data (the cross-checks would
    // catch it if it did).
    ring_arena      arena(std::size_t{64} << 20);
    std::mt19937_64 rng{0x816a110cu};

    for (const bool a_neg : {false, true}) {
        for (const bool b_neg : {false, true}) {
            // Schoolbook tier (below every gate).
            check_division_methods(random_value(12, rng, a_neg), random_value(5, rng, b_neg), arena);
            // Divide-and-conquer band on every architecture (past the
            // x86-64 gates of 160/64): bz for divmod, divappr for /.
            check_division_methods(random_value(620, rng, a_neg), random_value(200, rng, b_neg), arena);
        }
    }

    // Barrett march (s >= 512, m >= 16 s): the Newton reciprocal and the
    // wrapped block products run inside, all through the ring's rebinds.
    check_division_methods(random_value(8400, rng, false), random_value(512, rng, true), arena);

    // Exact multiple at dc sizes: drives divide_quotient's verify branch.
    {
        const big_int d = random_value(220, rng, false);
        const big_int q = random_value(330, rng, false);
        check_division_methods(q * d, d, arena);
    }

    EXPECT_GT(arena.allocations, 0u);
    EXPECT_GT(arena.peak_outstanding, 0u);
    // Every ring object was scoped inside check_division_methods, so every
    // allocation that flowed through the hooks and containers must have
    // been released back through deallocate.
    EXPECT_EQ(arena.outstanding, 0u);
}

} // namespace
