
#include <beman/big_int/big_int.hpp>

#include <gtest/gtest.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <array>
#include <span>

namespace local {

template <class IntegralType>
constexpr auto pow(const IntegralType& b, unsigned p) -> IntegralType {
    using local_integer_type = IntegralType;

    // Calculate (b ^ p).

    local_integer_type result{1};

    local_integer_type y{b};

    while (p != 0U) {
        if ((p & 1U) != 0U) {
            result *= y;
        }

        y = y *= y;

        p >>= 1U;
    }

    return result;
}

// Returns the number of bytes before the trailing run of zero bytes.
[[nodiscard]] inline auto significant_byte_len(const std::span<const std::byte> bytes) noexcept -> std::size_t {
    std::size_t n = bytes.size();
    while (n > 0 && bytes[n - 1] == std::byte{0}) {
        --n;
    }
    return n;
}

auto run_one_mersenne(const unsigned p2) -> void {
    using cpp_int_type = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;
    using big_int_type = beman::big_int::big_int;

    // Mersenne prime (2^1398269 - 1) approx. 8.147175644125731*10^420920

    // Mathematica:
    //
    // 2^1398269 - 1
    // N[%]

    const cpp_int_type cpp_int_two{2};
    const cpp_int_type cpp_int_mersenne{cpp_int_type{local::pow(cpp_int_two, p2)} - 1};

    const big_int_type big_int_two{2};
    const big_int_type big_int_mersenne{big_int_type{local::pow(big_int_two, p2)} - 1};

    const std::span<const ::boost::multiprecision::limb_type> cpp_int_rep{cpp_int_mersenne.backend().limbs(), cpp_int_mersenne.backend().size()};
    const auto                                                big_int_bytes = std::as_bytes(big_int_mersenne.representation());
    const auto                                                cpp_int_bytes = std::as_bytes(cpp_int_rep);

    const auto big_int_sig = local::significant_byte_len(big_int_bytes);
    const auto cpp_int_sig = local::significant_byte_len(cpp_int_bytes);

    const bool result_length_is_ok{big_int_sig == cpp_int_sig};

    EXPECT_EQ(result_length_is_ok, true);

    bool result_is_ok{result_length_is_ok};

    bool result_bytes_same_is_ok{false};

    if (result_is_ok) {
        for (std::size_t i = 0; i < big_int_sig; ++i) {
            if (big_int_bytes[i] != cpp_int_bytes[i]) {
                result_bytes_same_is_ok = false;
                break;
            } else {
                result_bytes_same_is_ok = true;
            }
        }
    }

    EXPECT_EQ(result_bytes_same_is_ok, true);

    result_is_ok = (result_bytes_same_is_ok && result_is_ok);

    EXPECT_EQ(result_is_ok, true);
}

// Note: We test millons of decimal digits, since
// Mathematica:
//   N[2^20996011 - 1]
//   1.259768954503301*10^6320429

constexpr std::array<unsigned, std::size_t{7}> my_mersenne_powers_of_two{
    1257787U, 1398269U, 2976221U, 3021377U, 6972593U, 13466917U, 20996011U};
} // namespace local

// See also https://oeis.org/A057429 sequencs of Mersenne primes.

TEST(Multiplication, KaratsubaMersenne00) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{0}]);
}

TEST(Multiplication, KaratsubaMersenne01) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{1}]);
}

TEST(Multiplication, KaratsubaMersenne02) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{2}]);
}

TEST(Multiplication, KaratsubaMersenne03) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{3}]);
}

TEST(Multiplication, KaratsubaMersenne04) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{4}]);
}

TEST(Multiplication, KaratsubaMersenne05) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{5}]);
}

TEST(Multiplication, KaratsubaMersenne06) {
    local::run_one_mersenne(local::my_mersenne_powers_of_two[std::size_t{6}]);
}
