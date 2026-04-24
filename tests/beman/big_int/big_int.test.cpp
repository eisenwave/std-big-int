#include <type_traits>
#include <cstddef>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

namespace beman::big_int {

template class basic_big_int<big_int::inplace_bits, big_int::allocator_type>;

static_assert(basic_big_int<128, big_int::allocator_type>::inplace_bits == 128);
static_assert(basic_big_int<127, big_int::allocator_type>::inplace_bits == 128,
              "inplace_bits was expected to be rounded to the next multiple of 32.");

template <class T>
struct is_exact_big_int;

template <std::size_t b, class A>
struct is_exact_big_int<basic_big_int<b, A>> : std::bool_constant<b == basic_big_int<b, A>::inplace_bits> {};

static_assert(is_exact_big_int<big_int>::value,
              "The min_inplace_bits should match inplace_bits exactly for big_int, "
              "though it can differ for other specializations.");
static_assert(sizeof(void*) != 8 || sizeof(big_int) == 16, "On 64-bit, big_int must be two pointers large.");

static_assert(detail::common_big_int_type_with<big_int, big_int>);
static_assert(detail::common_big_int_type_with<big_int, int>);
static_assert(detail::common_big_int_type_with<big_int, unsigned>);
static_assert(!detail::common_big_int_type_with<big_int, basic_big_int<1, std::allocator<uint_multiprecision_t>>>,
              "There must be no common type for mixed allocators.");
static_assert(!detail::common_big_int_type_with<big_int, float>,
              "There must be no common type between big_int and floating-point types.");
static_assert(!detail::common_big_int_type_with<int, int>,
              "There must be no common big_int type between two fundamental types.");

using namespace beman::big_int::literals;

static_assert(0_n == 0_n);
static_assert(1_n != 0_n);
static_assert(0xff_n == 255_n);
static_assert(0XFF_n == 255_n);
static_assert(0b1111_n == 15_n);
static_assert(0B1111_n == 15_n);

static_assert(1000_n == 1'0'00_n);
static_assert(1'000'000'000'000'000'000'000'000'000_n == 0x33b'2e3c'9fd0'803c'e800'0000_n);

} // namespace beman::big_int
