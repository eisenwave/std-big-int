// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/big_int/big_int.hpp>

#include <iomanip>
#include <iostream>
#include <string>

template <typename BigIntType>
auto fibonacci(unsigned int n) -> BigIntType {
    if (n == 0)
        return BigIntType(0);
    if (n == 1)
        return BigIntType(1);

    BigIntType a{0};
    BigIntType b{1};

    // Perform the big-integer Fibonacci iterations.
    for (unsigned int i = 2; i <= n; ++i) {
        BigIntType next = a + b;
        a               = b;
        b               = next;
    }

    return b;
}

auto main() -> int {
    using beman::big_int::big_int;

    // Compute the 1,000th Fibonacci number.
    const big_int fib_1000{fibonacci<big_int>(1000)};

    const std::string str_fib_1000{to_string(fib_1000)};

    const std::string str_ctrl{"4346655768693745643568852767504062580256466051737178040248172908"
                               "9536555417949051890403879840079255169295922593080322634775209689"
                               "6232398733224711616429964409065331879382989696499285160037044761"
                               "37795166849228875"};

    const bool result_is_ok{str_fib_1000 == str_ctrl};

    std::cout << "fib_1000:\n" << str_fib_1000 << "\n\nresult_is_ok:\n" << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
