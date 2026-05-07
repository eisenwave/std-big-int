// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "fuzz_common.hpp"

#include <cstddef>
#include <cstdint>

template <class T>
struct sqrt_exec {
    [[nodiscard]] T operator()(const T& my_arg) const { return sqrt(my_arg); }
};

template <class LimbType>
static auto msb_limb(LimbType x) -> int {
    if (x == LimbType{0}) {
        return -1;
    }

    constexpr LimbType limb_mask{LimbType{1} << (std::numeric_limits<LimbType>::digits - 1)};

    int bpos{};

    while ((x & limb_mask) == LimbType{0}) {
        ++bpos;
        x <<= 1;
    }

    return (std::numeric_limits<LimbType>::digits - 1) - bpos;
}

static auto msb(const big_int& m) -> int {

    const auto hi_limb{m.representation().back()};

    int bpos{msb_limb(hi_limb)};

    bpos += static_cast<int>(m.representation().size() - 1U) *
            std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits;

    return bpos;
}

static auto sqrt(big_int m) -> big_int {
    // Calculate the square root.

    big_int s{};

    if (m <= 0) {
        s = 0;
    } else {
        // Obtain the initial guess via algorithms
        // involving the position of the msb.
        const auto msb_pos = msb(m);

        const auto msb_pos_mod_2 = msb_pos % 2;

        // Obtain the initial value.
        const auto left_shift_amount = 1 + ((msb_pos_mod_2 == 0) ? msb_pos / 2 : (msb_pos + 1) / 2);

        big_int u{big_int{1} << left_shift_amount};

        // Perform the iteration for the square root.
        // See Algorithm 1.13 SqrtInt, Sect. 1.5.1
        // in R.P. Brent and Paul Zimmermann, "Modern Computer Arithmetic",
        // Cambridge University Press, 2011.

        for (auto i{0}; i < 64; ++i) {
            static_cast<void>(i);

            s = u;

            u = (s + (m / s)) / 2;

            if (u >= s) {
                break;
            }
        }
    }

    return s;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return ::beman::big_int::fuzz::run_unary(sqrt_exec<>{}, data, size);
}
