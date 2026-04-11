#include <beman/big_int/big_int.hpp>

namespace beman::big_int {

// Explicit instantiation for testing purposes.
template class basic_big_int<big_int::inplace_bits, big_int::allocator_type>;

static_assert(basic_big_int<128, big_int::allocator_type>::inplace_bits == 128);
static_assert(basic_big_int<127, big_int::allocator_type>::inplace_bits == 128,
              "inplace_bits was expected to be rounded to the next multiple of 32.");

} // namespace beman::big_int
