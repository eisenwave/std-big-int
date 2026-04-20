// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP
#define BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP

#define BOOST_MP_STANDALONE 1

#include <gtest/gtest.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <beman/big_int/big_int.hpp>

#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace beman::big_int::boost_mp_testing {

namespace detail {

using cpp_int = ::boost::multiprecision::cpp_int;

// The limb-level comparison assumes both libraries store magnitude in
// same-width limbs. Both default to 64-bit on platforms with __int128; on a
// 32-bit-limb build of beman, boost cpp_int would still be 64-bit, and the
// limb arrays wouldn't line up.
static_assert(sizeof(uint_multiprecision_t) == sizeof(::boost::multiprecision::limb_type),
              "Matching limb widths required between beman::big_int and boost::cpp_int for this test helper.");

// Generates a random hex string for a size of exactly `bits` bits,
// meaning the MSB is 1 (so the value is in [2^(bits-1), 2^bits)).
// `bits == 0` returns "0".
inline std::string random_hex_of_bits(std::mt19937_64& rng, std::size_t bits) {
    static constexpr char table[] = "0123456789abcdef";
    if (bits == 0) {
        return std::string{"0"};
    }

    const std::size_t num_hex  = (bits + 3) / 4;
    const std::size_t top_bits = bits - (num_hex - 1) * 4; // 1..4

    std::string s;
    s.reserve(num_hex);

    const unsigned                          top_high = 1u << (top_bits - 1);
    std::uniform_int_distribution<unsigned> top_rand(0, top_high - 1);
    s.push_back(table[top_high | top_rand(rng)]);

    std::uniform_int_distribution<unsigned> any(0, 15);
    for (std::size_t i = 1; i < num_hex; ++i) {
        s.push_back(table[any(rng)]);
    }
    return s;
}

// Parses a signed hex string (e.g. "deadbeef" or "-deadbeef"; no "0x" prefix) into a beman::big_int.
inline ::beman::big_int::big_int parse_big_int(std::string_view signed_hex) {
    ::beman::big_int::big_int out;
    const char* const         first = signed_hex.data();
    const char* const         last  = first + signed_hex.size();
    const auto [p, ec]               = ::beman::big_int::from_chars(first, last, out, 16);
    if (ec != std::errc{} || p != last) {
        throw std::runtime_error("parse_big_int: from_chars failed to parse hex string");
    }
    return out;
}

// Parses a signed hex string (e.g. "deadbeef" or "-deadbeef"; no "0x" prefix) into a boost cpp_int.
inline cpp_int parse_cpp_int(std::string_view signed_hex) {
    std::string s;
    s.reserve(signed_hex.size() + 3);
    if (!signed_hex.empty() && signed_hex.front() == '-') {
        s.push_back('-');
        signed_hex.remove_prefix(1);
    }
    s += "0x";
    s += signed_hex;
    return cpp_int{s};
}

// Trims trailing zero limbs for robust comparison.
inline std::size_t trimmed_len(const ::beman::big_int::uint_multiprecision_t* limbs, std::size_t n) noexcept {
    while (n > 1 && limbs[n - 1] == 0) {
        --n;
    }
    return n;
}

// Asserts that a beman::big_int and a boost::cpp_int represent the exact same
// integer, by comparing:
//   1) zeroness,
//   2) sign,
//   3) trimmed limb arrays.
[[nodiscard]] inline ::testing::AssertionResult same_value(const ::beman::big_int::big_int& bn, const cpp_int& cp) {
    const bool bn_zero = (bn == 0);
    const bool cp_zero = cp.is_zero();
    if (bn_zero != cp_zero) {
        return ::testing::AssertionFailure()
               << "zeroness mismatch: big_int::is_zero=" << bn_zero << " cpp_int::is_zero=" << cp_zero;
    }
    if (!bn_zero) {
        const bool bn_neg = (bn < 0);
        const bool cp_neg = cp.backend().sign();
        if (bn_neg != cp_neg) {
            return ::testing::AssertionFailure() << "sign mismatch: big_int<0=" << bn_neg << " cpp_int.sign()=" << cp_neg;
        }
    }

    const auto        bn_rep  = bn.representation();
    const auto* const cp_ptr  = cp.backend().limbs();
    const auto        bn_size = trimmed_len(bn_rep.data(), bn_rep.size());
    const auto        cp_size = trimmed_len(cp_ptr, cp.backend().size());

    if (bn_size != cp_size) {
        return ::testing::AssertionFailure()
               << "trimmed limb count mismatch: big_int=" << bn_size << " cpp_int=" << cp_size;
    }
    for (std::size_t i = 0; i < bn_size; ++i) {
        const auto bn_limb = bn_rep[i];
        const auto cp_limb = static_cast<::beman::big_int::uint_multiprecision_t>(cp_ptr[i]);
        if (bn_limb != cp_limb) {
            return ::testing::AssertionFailure()
                   << "limb[" << i << "] differs: big_int=" << std::hex << bn_limb << " cpp_int=" << cp_limb;
        }
    }
    return ::testing::AssertionSuccess();
}

} // namespace detail

// Applies `op` to each backend after parsing `lhs` and `rhs` as signed hex
// strings (leading '-' allowed; no "0x" prefix), then asserts the two results
// match bit-for-bit. `op` must be a generic callable that accepts two
// integers of either library type, e.g. `std::plus<>{}`, `std::minus<>{}`,
// `std::multiplies<>{}`, or a user lambda.
//
// Example:
//     EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "deadbeefcafebabe", "1234567890abcdef"));
template <class BinOp>
[[nodiscard]] inline ::testing::AssertionResult
check_cpp_int_equal(BinOp&& op, std::string_view lhs, std::string_view rhs) {
    const auto a_bn = detail::parse_big_int(lhs);
    const auto b_bn = detail::parse_big_int(rhs);
    const auto a_cp = detail::parse_cpp_int(lhs);
    const auto b_cp = detail::parse_cpp_int(rhs);
    return detail::same_value(op(a_bn, b_bn), op(a_cp, b_cp));
}

// Returns a signed hex string representing a random integer whose magnitude
// has exactly `bits` bits (the MSB is set).
// If `negative` is true, the returned string is prefixed with '-'. `bits == 0` returns "0".
//
// The intended use is to feed the result directly into `check_cpp_int_equal`:
//     EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, random_big_int(1024), random_big_int(1024)));
//
// Uses a function-local std::mt19937_64 seeded with the fixed value 42 so
// that the sequence is deterministic across runs.
// The RNG state advances on every call and is shared across all callers of this function.
inline std::string random_big_int(std::size_t bits, bool negative = false) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp) — deterministic seed is intentional for test reproducibility.
    static std::mt19937_64 rng{42};
    if (bits == 0) {
        return std::string{"0"};
    }
    std::string signed_hex;
    signed_hex.reserve(((bits + 3) / 4) + 1);
    if (negative) {
        signed_hex.push_back('-');
    }
    signed_hex += detail::random_hex_of_bits(rng, bits);
    return signed_hex;
}

} // namespace beman::big_int::boost_mp_testing

#endif // BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP
