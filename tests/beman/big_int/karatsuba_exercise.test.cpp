// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//

#include "boost_mp_testing.hpp"

#include <gtest/gtest.h>

namespace bmp = ::beman::big_int::boost_mp_testing;

namespace local {

namespace detail {

using random_engine_length_type =
    ::std::linear_congruential_engine<::std::uint32_t, UINT32_C(48271), UINT32_C(0), UINT32_C(2147483647)>;

random_engine_length_type generator_length{detail::time_point<typename random_engine_length_type::result_type>()};

std::uniform_int_distribution distribution_length{std::size_t{UINT8_C(48)}, std::size_t{UINT8_C(128)}};

} // namespace detail

auto test_one_multiplication() -> void {
    std::size_t len_a{detail::distribution_length(detail::generator_length)};
    std::size_t len_b{detail::distribution_length(detail::generator_length)};

    std::string str_a{random_big_int(len_a)};
    std::string str_b{random_big_int(len_b)};

    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, str_a, str_b));
}

} // namespace local

TEST(Multiplication, KaratsubaExercise01) {
    // For future reference, include a sample Mathematica session that
    // performs a big_int multiplication. It uses hexadecimal inputs
    // and also presents the result in hexadecimal format.

    // Mathematica:
    //
    // a = FromDigits["cb2cf1421408c408b8e1c6841d3bbb12", 16]
    // b = FromDigits["d35253e128902afb74af7110e78c0a4e388181820f00a5b6", 16]
    // a b
    // IntegerString[%, 16]
    //

    constexpr unsigned trials{256U};

    for (unsigned index{0U}; index < trials; ++index) {
        static_cast<void>(index);

        const bool result_one_multiplication_is_ok{local::test_one_multiplication()};
    }
}
