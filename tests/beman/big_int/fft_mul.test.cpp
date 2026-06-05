// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

#include "boost_mp_testing.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

using ::beman::big_int::uint_multiprecision_t;
using ::beman::big_int::detail::fft_choose_coeff_bits;
using ::beman::big_int::detail::fft_mul_storage_size;
using ::beman::big_int::detail::multiply_fft;
using ::beman::big_int::detail::square_fft;
using ::beman::big_int::detail::square_fft_storage_size;
using ::boost::multiprecision::cpp_int;

inline constexpr unsigned limb_bits = std::numeric_limits<uint_multiprecision_t>::digits;

// Adaptive coefficient-bit selection: the common FFT range picks the maximum
// b = 50; the smaller operand bounds the coefficient overlap (so a tiny operand
// still allows b=50); and for astronomically large operands b drops to keep the
// two-prime CRT exact. (Validated on the 64-bit-limb build.)
#if BEMAN_BIG_INT_LIMB_WIDTH == 64
static_assert(fft_choose_coeff_bits(1000, 1000) == 50);
static_assert(fft_choose_coeff_bits(100000, 100000) == 50);
static_assert(fft_choose_coeff_bits(1, 1000000) == 50);
static_assert(fft_choose_coeff_bits(50000000, 50000000) == 48);
#endif

// Random limb vector with a non-zero top limb (so the trimmed size is exactly n).
[[nodiscard]] std::vector<uint_multiprecision_t> random_limbs(std::mt19937_64& rng, const std::size_t n) {
    std::uniform_int_distribution<uint_multiprecision_t> dist;
    std::vector<uint_multiprecision_t>                   v(n);
    for (auto& x : v) {
        x = dist(rng);
    }
    if (v.back() == 0) {
        v.back() = 1;
    }
    return v;
}

[[nodiscard]] cpp_int from_limbs(const std::span<const uint_multiprecision_t> limbs) {
    cpp_int v = 0;
    for (std::size_t i = limbs.size(); i-- > 0;) {
        v <<= limb_bits;
        v += limbs[i];
    }
    return v;
}

[[nodiscard]] ::testing::AssertionResult check_multiply(std::mt19937_64& rng, const std::size_t na, const std::size_t nb) {
    const std::vector<uint_multiprecision_t> a = random_limbs(rng, na);
    const std::vector<uint_multiprecision_t> b = random_limbs(rng, nb);
    std::vector<uint_multiprecision_t>       result(na + nb, uint_multiprecision_t{0});
    std::vector<std::uint64_t>               workspace(fft_mul_storage_size(na, nb));

    multiply_fft(result, a, b, workspace);

    const cpp_int expected = from_limbs(a) * from_limbs(b);
    const cpp_int got      = from_limbs(result);
    if (got != expected) {
        return ::testing::AssertionFailure() << "multiply mismatch at na=" << na << " nb=" << nb;
    }
    return ::testing::AssertionSuccess();
}

[[nodiscard]] ::testing::AssertionResult check_square(std::mt19937_64& rng, const std::size_t na) {
    const std::vector<uint_multiprecision_t> a = random_limbs(rng, na);
    std::vector<uint_multiprecision_t>       result(2 * na, uint_multiprecision_t{0});
    std::vector<std::uint64_t>               workspace(square_fft_storage_size(na));

    square_fft(result, a, workspace);

    const cpp_int base     = from_limbs(a);
    const cpp_int expected = base * base;
    const cpp_int got      = from_limbs(result);
    if (got != expected) {
        return ::testing::AssertionFailure() << "square mismatch at na=" << na;
    }
    return ::testing::AssertionSuccess();
}

// Sizes spanning tiny, around powers of two, odd, and large enough that
// convolution coefficients use the high limb of the CRT value and grow the
// recomposition accumulator.
constexpr std::size_t sizes[] = {1,   2,   3,   4,    5,    7,    8,    9,    15,   16,   17,
                                 31,  32,  33,  63,   64,   65,   100,  127,  128,  129,  256,
                                 511, 512, 513, 1000, 1024, 2000, 2048, 3000, 4096, 5000};

TEST(FftMul, MultiplyEqualSizesDifferential) {
    std::mt19937_64 rng{0xf17u};
    for (const std::size_t n : sizes) {
        ASSERT_TRUE(check_multiply(rng, n, n));
    }
}

TEST(FftMul, MultiplyUnequalSizesDifferential) {
    std::mt19937_64 rng{0xf18u};
    for (const std::size_t n : sizes) {
        // A handful of unequal pairings, including very lopsided ones.
        ASSERT_TRUE(check_multiply(rng, n, n + 1));
        ASSERT_TRUE(check_multiply(rng, n + 1, n));
        ASSERT_TRUE(check_multiply(rng, n, (n / 2) + 1));
        ASSERT_TRUE(check_multiply(rng, 1, n));
    }
}

TEST(FftMul, SquareDifferential) {
    std::mt19937_64 rng{0xf19u};
    for (const std::size_t n : sizes) {
        ASSERT_TRUE(check_square(rng, n));
    }
}

// End-to-end through the public API: operands sized just above the cutoff route
// through the FFT branch of multiply_dispatch / square_dispatch. Tied to the
// cutoff constants so they keep exercising FFT after the cutoffs are tuned.
namespace bmp = ::beman::big_int::boost_mp_testing;

TEST(FftMul, DispatchIntegrationMultiply) {
    const std::size_t bits = (::beman::big_int::detail::fft_mul_cutoff + 1) * limb_bits;
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, bmp::random_big_int(bits), bmp::random_big_int(bits)));
}

TEST(FftMul, DispatchIntegrationSquare) {
    const std::size_t bits = (::beman::big_int::detail::square_fft_cutoff + 1) * limb_bits;
    // A single object squared (v * v) takes the square_dispatch path.
    EXPECT_TRUE(bmp::check_cpp_int_equal_unary([](const auto& v) { return v * v; }, bmp::random_big_int(bits)));
}

} // namespace
