// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/big_int/big_int.hpp>

#include <memory>
#include <iomanip>
#include <iostream>

template <typename BigIntType>
auto fibonacci(unsigned int n) -> BigIntType {
    if (n == 0)
        return BigIntType(0);
    if (n == 1)
        return BigIntType(1);

    BigIntType a{0};
    BigIntType b{1};

    // Perform the big-integer Fibonacci interations.
    for (unsigned int i = 2; i <= n; ++i) {
        BigIntType next = a + b;
        a               = b;
        b               = next;
    }

    return b;
}

auto main() -> int {
    using big_int_type = beman::big_int::big_int;

    // Compute the 1,000th Fibonacci number.
    const big_int_type big_int_fibonacci{fibonacci<big_int_type>(1000)};

    constexpr std::size_t fib_buffer_size{256U};

    char* p_str_fib = new char[fib_buffer_size];

    std::fill(p_str_fib, p_str_fib + fib_buffer_size, '\0');

    // TODO(ckormanyos): In the future, all of this printing can be accomplished with
    //                   I/O-streaming and (potential) support for std::format-like
    //                   mechanisms.

    to_chars(p_str_fib, p_str_fib + fib_buffer_size, big_int_fibonacci);

    const std::string str_ctrl{
        "4346655768693745643568852767504062580256466051737178040248172908"
        "9536555417949051890403879840079255169295922593080322634775209689"
        "6232398733224711616429964409065331879382989696499285160037044761"
        "37795166849228875"};

    const bool result_is_ok{std::string{p_str_fib} == str_ctrl};

    std::cout << "big_int_fibonacci:\n" << p_str_fib << "\n\nresult_is_ok:\n" << std::boolalpha << result_is_ok << std::endl;

    delete [] p_str_fib;
}
