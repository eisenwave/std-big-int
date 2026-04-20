
// cd C:\Users\ckorm\Documents\Ks\PC_Software\Test
// set
// GCC=C:\Users\ckorm\Documents\Ks\uC_Software\Boards\real-time-cpp\ref_app\tools\Util\msys64\usr\local\gcc-13.2.0-x86_64-w64-mingw32\bin\g++.exe
// %GCC% -std=c++20 -Wall -Wextra -O3 -IC:/ChrisGitRepos/std-big-int/include ./test.cpp -o
// test.exe
// .\test.exe

#include <beman/big_int/big_int.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#if defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L)
#include <format>
#else
#include <iomanip>
#endif
#include <iostream>
#include <random>
#include <utility>
#if !(defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L))
#include <sstream>
#endif

namespace local {

namespace detail {

template <class IntegralTimePointType,
          class ClockType = std::chrono::high_resolution_clock>
auto time_point() -> IntegralTimePointType;

auto make_from_limbs(std::string* p_str_a = nullptr, std::string* p_str_b = nullptr)
    -> std::pair<beman::big_int::big_int, beman::big_int::big_int>;

auto int_string_clz(std::string& str) -> void;

#if !(defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L))
auto get_next_limb_as_16char_str(const beman::big_int::uint_multiprecision_t val_limb)
    -> std::string;
#endif

using random_engine_limb_type = ::std::mt19937_64;
using random_engine_length_type =
    ::std::linear_congruential_engine<::std::uint32_t, UINT32_C(48271), UINT32_C(0), UINT32_C(2147483647)>;

random_engine_limb_type generator_limb
    {detail::time_point<typename random_engine_limb_type::result_type>()};
random_engine_length_type generator_length
    {detail::time_point<typename random_engine_length_type::result_type>()};

std::uniform_int_distribution distribution_limb
    {UINT64_C(0x0000000000000001), UINT64_C(0xFFFFFFFFFFFFFFFF)};
std::uniform_int_distribution distribution_length
    {std::size_t { UINT8_C(48) }, std::size_t{UINT8_C(128)}};

constexpr std::streamsize limb_chars
    {static_cast<std::streamsize>(std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits / 4)};
constexpr unsigned limb_width
    {static_cast<unsigned>(std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits)};

template<class IntegralTimePointType,
         class ClockType>
auto time_point() -> IntegralTimePointType
{
    using local_integral_time_point_type = IntegralTimePointType;
    using local_clock_type               = ClockType;

    const auto current_now =
        static_cast<::std::uintmax_t>
        (
          ::std::chrono::duration_cast<::std::chrono::nanoseconds>
          (
            local_clock_type::now().time_since_epoch()
          ).count()
        );

    return static_cast<local_integral_time_point_type>(current_now);
}

auto make_from_limbs(std::string* p_str_a, std::string* p_str_b)
    -> std::pair<beman::big_int::big_int, beman::big_int::big_int>
{
    // Build a pair of big_int from little-endian arrays of 64-bit limbs
    // having random lengths and random limb content. Do not rely on
    // std::from_range, which is not yet available on every toolchain in CI.

    using local_big_int_type = beman::big_int::big_int;

    local_big_int_type a{0};
    local_big_int_type b{0};

    std::size_t len_a{distribution_length(generator_length)};
    std::size_t len_b{distribution_length(generator_length)};

    if (p_str_a != nullptr) { *p_str_a = ""; };
    if (p_str_b != nullptr) { *p_str_b = ""; };

    for (std::size_t i = len_a; i > 0; --i)
    {
        const beman::big_int::uint_multiprecision_t next_limb { distribution_limb(generator_limb) };

        a <<= limb_width;
        a = a + next_limb;

        if(p_str_a != nullptr) {
#if defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L)
            *p_str_a += std::format("{:0{}x}", next_limb, limb_chars);
#else
            *p_str_a += get_next_limb_as_16char_str(next_limb);
#endif
        }
    }

    for (std::size_t i = len_b; i > 0; --i) {
        const beman::big_int::uint_multiprecision_t next_limb{distribution_limb(generator_limb)};

        b <<= limb_width;
        b = b + next_limb;

        if(p_str_b != nullptr) {
#if defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L)
            *p_str_b += std::format("{:0{}x}", next_limb, limb_chars);
#else
            *p_str_b += get_next_limb_as_16char_str(next_limb);
#endif
        }
    }

    return{a, b};
}

auto int_string_clz(std::string& str) -> void
{
    std::ptrdiff_t clz_count{};

    auto str_itr = str.cbegin();

    while(*str_itr++ == '0') { ++clz_count; }

    if (clz_count != std::size_t { UINT8_C(0) }) {
        str.erase(str.begin(), str.begin() + clz_count);
    }
}

#if !(defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L))
auto get_next_limb_as_16char_str(const beman::big_int::uint_multiprecision_t val_limb)
    -> std::string
{
    std::stringstream strm{};

    strm << std::hex << std::setw(limb_chars) << std::setfill('0') << std::right << val_limb;

    return strm.str();
}
#endif

} // namespace detail

auto test_one_multiplication() -> bool;

auto test_one_multiplication() -> bool
{
    using local_big_int_type = beman::big_int::big_int;

    static unsigned seed_prescaler{0U};

    if ((++seed_prescaler % 512U) == 0U) {
        detail::generator_limb.seed(detail::time_point<typename detail::random_engine_limb_type::result_type>());
        detail::generator_length.seed(detail::time_point<typename detail::random_engine_length_type::result_type>());
    }

    beman::big_int::big_int a{};
    beman::big_int::big_int b{};

    std::string str_a{};
    std::string str_b{};

    const auto ab_pair{detail::make_from_limbs(&str_a, &str_b)};

    const local_big_int_type c { ab_pair.first * ab_pair.second };

    const auto& c_rep{c.representation()};

    std::string str_c{};

    for (const auto& next_limb : c_rep) {
        str_c.insert
        (
            std::string::size_type { UINT8_C(0) },
#if defined(__cpp_lib_format) && (__cpp_lib_format >= 201907L)
            std::format("{:0{}x}", next_limb, detail::limb_chars)
#else
            detail::get_next_limb_as_16char_str(next_limb)
#endif
        );
    }

    detail::int_string_clz(str_a);
    detail::int_string_clz(str_b);
    detail::int_string_clz(str_c);

    const bool result_is_ok{(!str_c.empty())};

    return result_is_ok;
}

} /// namespace local

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

    constexpr unsigned trials{128U};

    bool result_is_ok{true};

    for (unsigned index{0U}; index < trials; ++index) {
        static_cast<void>(index);

        try {
            const bool result_one_multiplication_is_ok{local::test_one_multiplication()};

            result_is_ok = (result_one_multiplication_is_ok && result_is_ok);
        }
        catch (...) {
            result_is_ok = false;
        }

        EXPECT_EQ(result_is_ok, true);
    }
}
