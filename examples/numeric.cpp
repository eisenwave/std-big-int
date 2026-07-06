// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iostream>
#include <limits>

auto main() -> int {
    using namespace beman::big_int::literals;
    using beman::big_int::abs;
    using beman::big_int::big_int;
    using beman::big_int::saturating_cast;

    // The free numeric helpers live in <beman/big_int/numeric.hpp>.

    // 1. abs returns the magnitude of a value. Because big_int is unbounded it
    //    can never overflow, unlike std::abs on the most negative built-in.
    const big_int negative  = -170141183460469231731687303715884105728_n; // -2^127
    const big_int magnitude = abs(negative);

    // 2. saturating_cast narrows a big_int to a built-in integer, clamping to
    //    that type's range instead of wrapping or invoking undefined behavior.
    const big_int huge = 1_n << 200; // far larger than any built-in integer

    const int      clamped_hi  = saturating_cast<int>(huge);      // INT_MAX
    const int      clamped_lo  = saturating_cast<int>(-huge);     // INT_MIN
    const unsigned clamped_neg = saturating_cast<unsigned>(-1_n); // 0 (below the minimum)

    // 3. A value that already fits the destination type is returned exactly.
    const int exact = saturating_cast<int>(12345_n);

    const bool result_is_ok = magnitude == (1_n << 127) && clamped_hi == std::numeric_limits<int>::max() &&
                              clamped_lo == std::numeric_limits<int>::min() && clamped_neg == 0U && exact == 12345;

    std::cout << "magnitude:   " << to_string(magnitude) << "\n"
              << "clamped_hi:  " << clamped_hi << "\n"
              << "clamped_lo:  " << clamped_lo << "\n"
              << "clamped_neg: " << clamped_neg << "\n"
              << "exact:       " << exact << "\n\n"
              << "result_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
