#include <type_traits>
#include <cstddef>

#include <beman/big_int/big_int.hpp>

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

} // namespace beman::big_int
