// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0
//

#include <gtest/gtest.h>

#include "boost_mp_testing.hpp"

#include "testing.hpp"

#include <chrono>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace bmp = ::beman::big_int::boost_mp_testing;

namespace local {

namespace detail {

using vector_str_pair_type = std::vector<std::pair<std::string, std::string>>;
using str_pair_type = typename vector_str_pair_type::value_type;

using random_engine_length_type = std::minstd_rand;

random_engine_length_type generator_limb_length{
    static_cast<typename random_engine_length_type::result_type>(42)};

inline constexpr std::size_t limb_bits{static_cast<std::size_t>(
    std::numeric_limits<::beman::big_int::uint_multiprecision_t>::digits)};

std::uniform_int_distribution distribution_limb_length{
    std::size_t{4U} * static_cast<std::size_t>(limb_bits),
    std::size_t{96U} * static_cast<std::size_t>(limb_bits)};

auto get_hex_string_pair() -> std::pair<std::string, std::string>;

auto get_hex_string_pair() -> std::pair<std::string, std::string> {

  std::size_t len_a_in_bits{};
  std::size_t len_b_in_bits{};

  len_a_in_bits =
      detail::distribution_limb_length(detail::generator_limb_length);
  len_b_in_bits =
      detail::distribution_limb_length(detail::generator_limb_length);

  const std::string str_a{bmp::random_big_int(len_a_in_bits)};
  const std::string str_b{bmp::random_big_int(len_b_in_bits)};

  if (str_b.length() < str_a.length()) {
    return {str_a, str_b};
  } else if (str_b.length() > str_a.length()) {
    return {str_b, str_a};
  } else {
    return (str_b < str_a) ? std::pair<std::string, std::string>{str_a, str_b}
                           : std::pair<std::string, std::string>{str_b, str_a};
  }
}

} // namespace detail

namespace concurrency {

template <class ClockType = std::chrono::high_resolution_clock>
struct stopwatch {
public:
  using time_point_type = std::uint64_t;

  auto reset() -> void { m_start = now(); }

  template <class RepresentationRequestedTimeType>
  static auto elapsed_time(const stopwatch &my_stopwatch) noexcept
      -> RepresentationRequestedTimeType {
    using local_time_type = RepresentationRequestedTimeType;

    return local_time_type{
        static_cast<local_time_type>(my_stopwatch.elapsed()) /
        local_time_type{UINTMAX_C(1000000000)}};
  }

private:
  time_point_type m_start{now()};

  [[nodiscard]] static auto now() -> time_point_type {
    using local_clock_type = ClockType;

    const auto current_now = static_cast<std::uintmax_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            local_clock_type::now().time_since_epoch())
            .count());

    return static_cast<time_point_type>(current_now);
  }

  [[nodiscard]] auto elapsed() const -> time_point_type {
    const time_point_type stop{now()};

    const time_point_type elapsed_ns{stop - m_start};

    return elapsed_ns;
  }
};

} // namespace concurrency

using detail::str_pair_type;
using detail::vector_str_pair_type;

auto fill_str_pairs(vector_str_pair_type &str_pairs, const std::size_t trials)
    -> void;

auto fill_str_pairs(vector_str_pair_type &str_pairs, const std::size_t trials)
    -> void {
  str_pairs.clear();

  str_pairs.reserve(trials);

  for (auto index{std::size_t{0U}}; index < trials; ++index) {
    str_pairs.push_back(local::detail::get_hex_string_pair());
  }
}

template <class BinOp>
auto time_divisions_all(BinOp op, const vector_str_pair_type &str_pairs)
    -> void;

template <class BinOp>
auto time_divisions_all(BinOp op, const vector_str_pair_type &str_pairs)
    -> void {

  // Prepare all the divisions for an upcoming compariative timing analysis.
  using big_int_type = beman::big_int::big_int;
  using cpp_int_type =
      boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>,
                                    boost::multiprecision::et_off>;

  std::vector<std::pair<big_int_type, big_int_type>> big_int_pairs;
  big_int_pairs.reserve(str_pairs.size());

  std::vector<std::pair<cpp_int_type, cpp_int_type>> cpp_int_pairs;
  cpp_int_pairs.reserve(str_pairs.size());

  for (const auto &next_str_pair : str_pairs) {
    big_int_type next_big_int_a{};
    big_int_type next_big_int_b{};

    static_cast<void>(
        from_chars(next_str_pair.first.c_str(),
                   next_str_pair.first.c_str() + next_str_pair.first.length(),
                   next_big_int_a, 16));
    static_cast<void>(
        from_chars(next_str_pair.second.c_str(),
                   next_str_pair.second.c_str() + next_str_pair.second.length(),
                   next_big_int_b, 16));

    big_int_pairs.push_back({next_big_int_a, next_big_int_b});

    cpp_int_pairs.push_back({cpp_int_type("0x" + next_str_pair.first),
                             cpp_int_type("0x" + next_str_pair.second)});
  }

  // Use a stopwatch to do a comparative timing run of division
  // big_int-versus-cpp_int.
  using local_stopwatch_type = concurrency::stopwatch<>;

  float elapsed_big_int{};
  float elapsed_cpp_int{};

  {
    local_stopwatch_type my_stopwatch{};

    for (const auto &next_big_int_pair : big_int_pairs) {
      static_cast<void>(op(next_big_int_pair.first, next_big_int_pair.second));
    }

    elapsed_big_int = local_stopwatch_type::elapsed_time<float>(my_stopwatch);
  }

  {
    local_stopwatch_type my_stopwatch{};

    for (const auto &next_cpp_int_pair : cpp_int_pairs) {
      static_cast<void>(op(next_cpp_int_pair.first, next_cpp_int_pair.second));
    }

    elapsed_cpp_int = local_stopwatch_type::elapsed_time<float>(my_stopwatch);
  }

    EXPECT_TRUE(elapsed_big_int > 0.001F);
    EXPECT_TRUE(elapsed_cpp_int > 0.001F);

    // Optionally print the timing results to the console.

    // Multiplication
    // elapsed_big_int: 0.0322567
    // elapsed_cpp_int: 0.0357098

    // Division
    // elapsed_big_int: 0.097041
    // elapsed_cpp_int: 0.0754829

    // std::cout << "elapsed_big_int: " << elapsed_big_int << std::endl;
    // std::cout << "elapsed_cpp_int: " << elapsed_cpp_int << std::endl;
}

template <class BinOp>
auto test_divisions_all(BinOp op, const vector_str_pair_type &str_pairs)
    -> void;

template <class BinOp>
auto test_divisions_all(BinOp op, const vector_str_pair_type &str_pairs)
    -> void {

    // Verify all division results big_int-versus-cpp_int.
    for (const auto& next_str_pair : str_pairs) {
        EXPECT_TRUE(bmp::check_cpp_int_equal(std::forward<BinOp>(op), next_str_pair.first, next_str_pair.second));
    }
}

} // namespace local

TEST(BinaryOperations, BinOpsTiming01) {
    local::vector_str_pair_type str_pairs{};

    constexpr std::size_t trials{16384U};
    local::fill_str_pairs(str_pairs, trials);

    // Use std::plus{}, std::multiplies{}, std::divides{}, etc.
    local::time_divisions_all(std::divides{}, str_pairs);

    // Use std::plus{}, std::multiplies{}, std::divides{}, etc.
    local::test_divisions_all(std::divides{}, str_pairs);

    // TODO(ckormanyos): Formulate this example as a binary-ops perf checker
    //                   that generates a table of pref results featuring
    //                   std-big-int-versus-cpp_int.
}
