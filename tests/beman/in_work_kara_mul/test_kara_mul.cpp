
// cd C:\Users\ckorm\Documents\Ks\PC_Software\Test
// set GCC=C:\Users\ckorm\Documents\Ks\uC_Software\Boards\real-time-cpp\ref_app\tools\Util\msys64\usr\local\gcc-13.2.0-x86_64-w64-mingw32\bin\g++.exe
// %GCC% -std=c++20 -Wall -Wextra -O3 -IC:/ChrisGitRepos/std-big-int/include -IC:/boost/boost_1_90_0 ./test.cpp -o test.exe
// .\test.exe

#include <beman/big_int/big_int.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <utility>

namespace local {

namespace detail {

template<typename IntegralTimePointType,
          typename ClockType = std::chrono::high_resolution_clock>
auto time_point() -> IntegralTimePointType;

auto make_from_limbs(std::string* p_str_a = nullptr, std::string* p_str_b = nullptr) -> std::pair<beman::big_int::big_int, beman::big_int::big_int>;

using random_engine_limb_type   = ::std::mt19937_64;
using random_engine_length_type = ::std::linear_congruential_engine<::std::uint32_t, UINT32_C(48271), UINT32_C(0), UINT32_C(2147483647)>;

random_engine_limb_type generator_limb { detail::time_point<typename random_engine_limb_type::result_type>() };
random_engine_length_type generator_length { detail::time_point<typename random_engine_length_type::result_type>() };

std::uniform_int_distribution distribution_limb { UINT64_C(0x1000000000000000), UINT64_C(0xFFFFFFFFFFFFFFFF) };
std::uniform_int_distribution distribution_length { std::size_t { UINT8_C(48) }, std::size_t { UINT8_C(128) } };

template<typename IntegralTimePointType,
          typename ClockType>
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

// Build a big_int from a little-endian array of 64-bit limbs without relying on
// std::from_range, which is not yet available on every toolchain in CI.
auto make_from_limbs(std::string* p_str_a, std::string* p_str_b) -> std::pair<beman::big_int::big_int, beman::big_int::big_int>
{
  using local_big_int_type = beman::big_int::big_int;

  local_big_int_type a { 0 };
  local_big_int_type b { 0 };

  std::size_t len_a { distribution_length(generator_length) };
  std::size_t len_b { distribution_length(generator_length) };

  if(p_str_a != nullptr) { *p_str_a = ""; };
  if(p_str_b != nullptr) { *p_str_b = ""; };

  for (std::size_t i = len_a; i > 0; --i)
  {
    const beman::big_int::uint_multiprecision_t next_limb { distribution_limb(generator_limb) };

    a <<= 64;
    a = a + next_limb;

    if(p_str_a != nullptr)
    {
      std::stringstream strm { };

      strm << std::setw(static_cast<std::streamsize>(std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits / 4))
           << std::setfill('0')
           << std::hex
           << std::right
           << next_limb;

      *p_str_a += strm.str();
    }
  }

  for (std::size_t i = len_b; i > 0; --i)
  {
    const beman::big_int::uint_multiprecision_t next_limb { distribution_limb(generator_limb) };

    b <<= 64;
    b = b + next_limb;

    if(p_str_b != nullptr)
    {
      std::stringstream strm { };

      strm << std::setw(static_cast<std::streamsize>(std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits / 4))
           << std::setfill('0')
           << std::hex
           << std::right
           << next_limb;

      *p_str_b += strm.str();
    }
  }

  return { a, b };
}

} // namespace detail

auto test_one_multiplication() -> bool
{
  using local_big_int_type = beman::big_int::big_int;

  static unsigned seed_prescaler { 0U };

  if((++seed_prescaler % 1024U) == 0U)
  {
    detail::generator_limb.seed(detail::time_point<typename detail::random_engine_limb_type::result_type>());
    detail::generator_length.seed(detail::time_point<typename detail::random_engine_length_type::result_type>());
  }

  beman::big_int::big_int a { };
  beman::big_int::big_int b { };

  std::string str_a { };
  std::string str_b { };

  const auto ab_pair { detail::make_from_limbs(&str_a, &str_b) };

  const local_big_int_type c { ab_pair.first * ab_pair.second };

  const auto& c_rep { c.representation() };

  std::string str_c { };

  for(const auto& next_limb : c_rep)
  {
    std::stringstream strm { };

    strm << std::setw(static_cast<std::streamsize>(std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits / 4))
         << std::setfill('0')
         << std::hex
         << std::right
         << next_limb;

    str_c.insert(std::string::size_type { UINT8_C(0) }, strm.str());
  }

  // Sample Mathematica session.
  // a = FromDigits["cb2cf1421408c408b8e1c6841d3bbb12", 16]
  // b = FromDigits["d35253e128902afb74af7110e78c0a4e388181820f00a5b6", 16]
  // a b
  // IntegerString[%, 16]

  using local_cpp_int_type = boost::multiprecision::cpp_int;

  while(str_a.front() == '0') { str_a.erase(str_a.begin(), str_a.begin() + std::size_t { UINT8_C(1) }); }
  while(str_b.front() == '0') { str_b.erase(str_b.begin(), str_b.begin() + std::size_t { UINT8_C(1) }); }
  while(str_c.front() == '0') { str_c.erase(str_c.begin(), str_c.begin() + std::size_t { UINT8_C(1) }); }

  local_cpp_int_type cpp_int_a { "0x" + str_a };
  local_cpp_int_type cpp_int_b { "0x" + str_b };

  local_cpp_int_type cpp_int_c { cpp_int_a * cpp_int_b };

  std::string str_ctrl { };

  {
    std::stringstream strm { };

    strm << std::hex << cpp_int_c;

    str_ctrl = strm.str();
  }

  const bool result_is_ok { str_c == str_ctrl };

  return result_is_ok;
}

} /// namespace local

auto main() -> int;

auto main() -> int
{
  bool result_is_ok { true };

  constexpr unsigned trials { 0x100000U };

  unsigned index { 0U };

  for( ; index < trials && result_is_ok; ++index)
  {
    static_cast<void>(index);

    const bool result_one_multiplication_is_ok { local::test_one_multiplication() };

    result_is_ok = (result_one_multiplication_is_ok && result_is_ok);

    static unsigned print_count_prescaler { 0U };

    if((++print_count_prescaler % 1000U) == 0U)
    {
      std::cout << "trials: " << (index + 1U) << std::endl;
    }
  }

  std::stringstream strm { };

  strm << std::boolalpha << "trials: " << index << ", result_is_ok: " << result_is_ok;

  std::cout << strm.str() << std::endl;
}
