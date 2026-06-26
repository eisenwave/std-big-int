// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>
#include <string>

auto main() -> int {
    using namespace beman::big_int::literals;

    // A value far larger than any built-in integer type.
    const auto value = 1_n << 128; // 2^128

    // 1. to_string returns a std::string holding the value in a chosen base.
    //    The default is base 10; digit values of ten or more use the lowercase
    //    letters a-z. Unlike to_chars, it allocates the result for you.
    const std::string decimal = to_string(value);
    const std::string hex     = to_string(value, 16);

    // 2. A negative value is rendered with a single leading '-'.
    const std::string negative = to_string(-value, 16);

    // 3. to_wstring is the wchar_t twin of to_string: the same digits, returned
    //    as a std::wstring for use with wide-character APIs. Widening the narrow
    //    result confirms the two agree character for character.
    const std::wstring wide_hex = to_wstring(value, 16);
    const bool         wide_ok  = wide_hex == std::wstring(hex.begin(), hex.end());

    const bool result_is_ok = decimal == "340282366920938463463374607431768211456" && // 2^128
                              hex == "1" + std::string(32, '0') &&                    // 2^128 == 16^32
                              negative == "-" + hex && wide_ok;

    std::cout << "decimal:  " << decimal << "\n"
              << "hex:      " << hex << "\n"
              << "negative: " << negative << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
