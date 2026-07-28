#ifndef BEMAN_BIG_INT_TEST_HPP
#define BEMAN_BIG_INT_TEST_HPP

#include <cstddef>
#include <ostream>
#include <span>

#include <beman/big_int.hpp>

namespace beman::big_int {

template <std::size_t b, class A, class Limb>
std::ostream& operator<<(std::ostream& out, const basic_big_int<b, A, Limb>& x) {
    return out << to_string(x);
}

template <class T>
std::ostream& operator<<(std::ostream& out, const div_result<T>& x) {
    return out << "{.quotient = " << x.quotient << ", .remainder = " << x.remainder << '}';
}

// Square-and-multiply integer exponentiation. Used by Mersenne and benchmark
// tests to compute b^p in big_int / cpp_int types without dragging in
// boost::multiprecision::pow.
template <class IntegralType>
[[nodiscard]] constexpr IntegralType pow(const IntegralType& b, unsigned p) {
    IntegralType result{1};
    IntegralType y{b};
    while (p != 0U) {
        if ((p & 1U) != 0U) {
            result *= y;
        }
        y *= y;
        p >>= 1U;
    }
    return result;
}

// True when x keeps its magnitude in the in-place (small-object) buffer rather than in
// dynamically-allocated storage. With the bit-based capacity()/limb-based
// representation_capacity() model, an in-place value has representation_capacity() equal to
// inplace_capacity (capacity() reports inplace_bits); a heap-allocated value exceeds it.
// Found by ADL from the tests because basic_big_int lives in this namespace.
template <std::size_t b, class A, class Limb>
[[nodiscard]] constexpr bool is_inplace(const basic_big_int<b, A, Limb>& x) noexcept {
    return x.representation_capacity() == basic_big_int<b, A, Limb>::inplace_capacity;
}

// Returns the number of bytes before the trailing run of zero bytes.
// Used to compare big_int representations against boost::multiprecision::cpp_int
// limb arrays, where padding limbs may differ but the significant bytes match.
[[nodiscard]] inline std::size_t significant_byte_len(const std::span<const std::byte> bytes) noexcept {
    std::size_t n = bytes.size();
    while (n > 0 && bytes[n - 1] == std::byte{0}) {
        --n;
    }
    return n;
}

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_TEST_HPP
