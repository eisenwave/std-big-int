#ifndef BEMAN_BIG_INT_TEST_HPP
#define BEMAN_BIG_INT_TEST_HPP

#include <ostream>

#include <beman/big_int/big_int.hpp>

namespace beman::big_int {

template <std::size_t b, class A>
std::ostream& operator<<(std::ostream& out, const basic_big_int<b, A>& x) {
    return out << to_string(x);
}

template <class T>
std::ostream& operator<<(std::ostream& out, const div_result<T>& x) {
    return out << "{.quotient = " << x.quotient << ", .remainder = " << x.remainder << '}';
}

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_TEST_HPP
