// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <array>
#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>

auto main() -> int {
    using beman::big_int::big_int;

    // 1. Parse a value far too large for any built-in integer type from its
    //    decimal text using from_chars. The default base is 10.
    constexpr std::string_view decimal{"340282366920938463463374607431768211457"}; // 2^128 + 1
    big_int                    value;
    const auto                 parse     = from_chars(decimal.data(), decimal.data() + decimal.size(), value);
    const bool                 parsed_ok = parse.ec == std::errc{} && parse.ptr == decimal.data() + decimal.size();

    // 2. Render the value back to text in base 16 with to_chars. The buffer
    //    must be large enough; otherwise to_chars reports value_too_large.
    std::array<char, 64>   buffer{};
    const auto             print = to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
    const std::string_view hex{buffer.data(), static_cast<std::size_t>(print.ptr - buffer.data())};
    const bool             printed_ok = print.ec == std::errc{};

    // 3. Round-trip: parse the hexadecimal text back and confirm the value is
    //    recovered exactly.
    big_int    recovered;
    const auto reparse       = from_chars(hex.data(), hex.data() + hex.size(), recovered, 16);
    const bool round_trip_ok = reparse.ec == std::errc{} && recovered == value;

    const bool result_is_ok = parsed_ok && printed_ok && round_trip_ok;

    std::cout << "decimal: " << decimal << "\n"
              << "hex:     " << hex << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
