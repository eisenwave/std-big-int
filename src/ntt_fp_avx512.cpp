// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt_fp.hpp>

// x86-64 AVX-512F FP NTT kernels (width 8). Compiled (by CMake) only on x86-64 and
// ONLY this TU is built with -mavx512f, so AVX-512 instructions are confined here;
// the dispatcher selects these only after a runtime AVX-512F check (and that the OS
// enabled ZMM state), so they never execute on a CPU/OS that lacks them. The guard
// keeps the TU empty if it is ever compiled on another architecture.
#if BEMAN_BIG_INT_NTT_FP_X86

#include <beman/big_int/detail/simd/vec_avx512.hpp>

#include <cstddef>

namespace beman::big_int::detail {

void ntt_fp_forward_avx512(double* const         data,
                           const std::size_t     n,
                           const double* const   tw,
                           const ntt_fp_modulus& m) noexcept {
    ntt_fp_forward_impl<vec8d>(data, n, tw, m);
}

void ntt_fp_inverse_avx512(double* const         data,
                           const std::size_t     n,
                           const double* const   tw,
                           const ntt_fp_modulus& m) noexcept {
    ntt_fp_inverse_impl<vec8d>(data, n, tw, m);
}

void ntt_fp_pointwise_avx512(double* const         a,
                             const double* const   b,
                             const std::size_t     n,
                             const ntt_fp_modulus& m) noexcept {
    ntt_fp_pointwise_impl<vec8d>(a, b, n, m);
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_NTT_FP_X86
