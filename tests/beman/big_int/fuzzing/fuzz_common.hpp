// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_TESTS_FUZZING_FUZZ_COMMON_HPP
#define BEMAN_BIG_INT_TESTS_FUZZING_FUZZ_COMMON_HPP

#include "../boost_mp_testing.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace beman::big_int::fuzz {

// Encode a byte buffer as a signed hex string accepted by both
// beman::big_int::from_chars and boost::cpp_int's string ctor.
//
// Layout: top bit of byte 0 = sign; the remaining 7 bits of byte 0 plus
// bytes 1..n-1 are the magnitude bytes, big-endian. Empty input maps to "0"
// to avoid producing an invalid (empty) hex literal.
[[nodiscard]] inline std::string bytes_to_signed_hex(const std::uint8_t* data, const std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    if (size == 0) {
        return std::string{"0"};
    }
    const bool  negative = (data[0] & 0x80U) != 0U;
    std::string out;
    out.reserve(1 + size * 2);
    if (negative) {
        out.push_back('-');
    }
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint8_t b = (i == 0) ? static_cast<std::uint8_t>(data[0] & 0x7FU) : data[i];
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0FU]);
    }
    return out;
}

// True iff bytes_to_signed_hex(data, size) would denote zero.
[[nodiscard]] inline bool encodes_zero(const std::uint8_t* data, const std::size_t size) noexcept {
    if (size == 0) {
        return true;
    }
    if ((data[0] & 0x7FU) != 0U) {
        return false;
    }
    for (std::size_t i = 1; i < size; ++i) {
        if (data[i] != 0U) {
            return false;
        }
    }
    return true;
}

// Run one fuzz iteration: split `data` in half, encode each half, and assert parity.
// `skip_zero_rhs` is set by the division harness to drop divide-by-zero inputs
// libFuzzer treats a -1 return as "uninteresting; do not add to corpus".
template <class BinOp>
int run(BinOp&& op, const std::uint8_t* data, const std::size_t size, const bool skip_zero_rhs = false) {
    const std::size_t       split    = size / 2;
    const std::uint8_t*     lhs_data = data;
    const std::size_t       lhs_size = split;
    const std::uint8_t*     rhs_data = data + split;
    const std::size_t       rhs_size = size - split;

    if (skip_zero_rhs && encodes_zero(rhs_data, rhs_size)) {
        return -1;
    }

    const std::string lhs = bytes_to_signed_hex(lhs_data, lhs_size);
    const std::string rhs = bytes_to_signed_hex(rhs_data, rhs_size);

    const auto result = ::beman::big_int::boost_mp_testing::check_cpp_int_equal(
        std::forward<BinOp>(op), std::string_view{lhs}, std::string_view{rhs});
    if (!result) {
        std::fprintf(stderr, "beman::big_int parity mismatch\n  lhs = %s\n  rhs = %s\n  %s\n",
                     lhs.c_str(), rhs.c_str(), result.message());
        std::abort();
    }
    return 0;
}

} // namespace beman::big_int::fuzz

#endif // BEMAN_BIG_INT_TESTS_FUZZING_FUZZ_COMMON_HPP
