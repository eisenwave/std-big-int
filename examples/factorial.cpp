// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/big_int/big_int.hpp>

#include <iomanip>
#include <iostream>
#include <string>

template <class BigIntType>
constexpr auto factorial(unsigned int n) -> BigIntType {
    return (n <= 1) ? 1 : n * factorial<BigIntType>(n - 1);
}

auto main() -> int {
    using beman::big_int::big_int;

    // TODO(ckormanyos) Consistently use constexpr if (and only if)
    //                  constexpr std::string is available (C++?).
    //                  This example is probably low enough in complexity
    //                  for constexpr calculation.

    // Compute the 100th Factorial number.
    const big_int fact_100{factorial<big_int>(100)};

    const std::string str_fact_100{to_string(fact_100)};

    const std::string str_ctrl{"9332621544394415268169923885626670049071596826438162146859296389"
                               "5217599993229915608941463976156518286253697920827223758251185210"
                               "916864000000000000000000000000"};

    const bool result_is_ok{str_fact_100 == str_ctrl};

    std::cout << "fact_100:\n" << str_fact_100 << "\n\nresult_is_ok:\n" << std::boolalpha << result_is_ok << std::endl;
}
