// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP
#define BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP

#define BOOST_MP_STANDALONE 1

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Warray-bounds") // This causes way too many problems.
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overflow")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wstringop-overread")

#include <boost/multiprecision/cpp_int.hpp>

#include <cstddef>
#include <format>
#include <ios>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace beman::big_int::boost_mp_testing {

namespace detail {

using cpp_int = ::boost::multiprecision::cpp_int;

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
    const auto [p, ec]              = ::beman::big_int::from_chars(first, last, out, 16);
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

// Canonical lowercase-hex rendering of a beman::big_int. Prints limbs from
// MSB to LSB, with a leading '-' for negative values. Zero renders as "0".
// Used both as a hashable canonical form for comparison and for diagnostics.
inline std::string to_hex(const ::beman::big_int::big_int& bn) {
    const auto  rep = bn.representation();
    std::size_t n   = rep.size();
    while (n > 1 && rep[n - 1] == 0) {
        --n;
    }
    std::string s;
    if (bn < 0) {
        s.push_back('-');
    }
    s += std::format("{:x}", rep[n - 1]);
    constexpr std::size_t limb_hex = sizeof(::beman::big_int::uint_multiprecision_t) * 2;
    for (std::size_t i = n - 1; i > 0; --i) {
        s += std::format("{:0{}x}", rep[i - 1], limb_hex);
    }
    return s;
}

// Asserts that a beman::big_int and a boost::cpp_int represent the exact same
// integer. Uses a canonical hex-string roundtrip so the check is independent
// of the internal limb width used by each library - boost.multiprecision uses
// 32-bit limbs on platforms without __int128 (e.g. MSVC) while beman uses
// 64-bit, so a direct limb-by-limb comparison isn't portable.
[[nodiscard]] inline ::testing::AssertionResult same_value(const ::beman::big_int::big_int& bn, const cpp_int& cp) {
    // cpp_int::str() refuses to render a negative magnitude as hex or octal
    // ("Base 8 or 16 printing of negative numbers is not supported."), so
    // print the magnitude of abs(cp) and prepend '-' ourselves when needed.
    const bool        cp_neg = !cp.is_zero() && cp.backend().sign();
    const std::string cp_mag = (cp_neg ? cpp_int{-cp} : cp).str(0, std::ios_base::hex);
    std::string       cp_hex;
    cp_hex.reserve(cp_mag.size() + 1);
    if (cp_neg) {
        cp_hex.push_back('-');
    }
    cp_hex += cp_mag;
    const auto expected_bn = parse_big_int(cp_hex);
    if (bn == expected_bn) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "value mismatch: big_int=" << to_hex(bn) << " cpp_int=" << cp_hex;
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
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp) - deterministic seed is intentional for test reproducibility.
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

BEMAN_BIG_INT_DIAGNOSTIC_POP()

#endif // BEMAN_BIG_INT_TESTS_BOOST_MP_TESTING_HPP
