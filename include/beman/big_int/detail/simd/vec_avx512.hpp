// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SIMD_VEC_AVX512_HPP
#define BEMAN_BIG_INT_SIMD_VEC_AVX512_HPP

#include <cstddef>

#include <immintrin.h>

// The x86-64 AVX-512F (width-8) vector type. __m512d + _mm512_fmsub_pd /
// _mm512_fnmadd_pd (single-rounded FMA) and _mm512_roundscale_pd (round to
// nearest, ties to even) produce results bit-identical to vec1d. This header is
// compiled ONLY into src/ntt_fp_avx512.cpp (built with -mavx512f); the dispatcher
// selects it at runtime only after confirming AVX-512F support and that the OS has
// enabled ZMM state, so its instructions never execute on a CPU/OS lacking them.
// Only AVX-512F is used (no VL/DQ/BW), so -mavx512f alone suffices.

namespace beman::big_int::detail {

struct vec8d {
    inline static constexpr std::size_t width = 8;
    __m512d                             v;

    [[nodiscard]] static vec8d loadu(const double* const p) noexcept { return vec8d{_mm512_loadu_pd(p)}; }
    void                       storeu(double* const p) const noexcept { _mm512_storeu_pd(p, v); }
    [[nodiscard]] static vec8d splat(const double x) noexcept { return vec8d{_mm512_set1_pd(x)}; }
};

[[nodiscard]] inline vec8d operator+(const vec8d a, const vec8d b) noexcept { return vec8d{_mm512_add_pd(a.v, b.v)}; }
[[nodiscard]] inline vec8d operator-(const vec8d a, const vec8d b) noexcept { return vec8d{_mm512_sub_pd(a.v, b.v)}; }
[[nodiscard]] inline vec8d vmul(const vec8d a, const vec8d b) noexcept { return vec8d{_mm512_mul_pd(a.v, b.v)}; }
// a*b - c, single-rounded.
[[nodiscard]] inline vec8d vfmsub(const vec8d a, const vec8d b, const vec8d c) noexcept {
    return vec8d{_mm512_fmsub_pd(a.v, b.v, c.v)};
}
// c - a*b, single-rounded.
[[nodiscard]] inline vec8d vfnmadd(const vec8d a, const vec8d b, const vec8d c) noexcept {
    return vec8d{_mm512_fnmadd_pd(a.v, b.v, c.v)};
}
[[nodiscard]] inline vec8d vround(const vec8d a) noexcept {
    return vec8d{_mm512_roundscale_pd(a.v, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)};
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SIMD_VEC_AVX512_HPP
