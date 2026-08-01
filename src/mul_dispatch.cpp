// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mul_impl_runtime.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>
#include <beman/big_int/detail/span_ops.hpp>

// The runtime multiplication tier ladders, compiled once. The header
// dispatchers (multiply_dispatch / square_dispatch) keep the constexpr
// small-operand and constant-evaluation paths and forward every runtime
// multi-limb product here; kernel workspaces come from the type-erased heap
// hooks, so a single compiled definition serves every allocator.

namespace beman::big_int::detail {

std::size_t square_runtime(const std::span<uint_multiprecision_t>       result,
                           const std::span<const uint_multiprecision_t> a,
                           const scratch_heap_source&                   heap) {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t n            = a.size();
    const std::size_t result_total = 2 * n;

    // Tiny squares: plain schoolbook beats the three-pass squaring basecase.
    if (n < square_long_cutoff) {
        multiply_long(result.first(result_total), a, a);
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    // (2^k)^2 = 2^(2k): a shifted copy beats any squaring kernel.
    if (is_power_of_two_span(a)) {
        return multiply_power_of_two(result, a, a);
    }

    if (n < square_karatsuba_cutoff) {
        square_long(result, a);
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    const auto in_heap_scratch = [&](const std::size_t limbs, auto&& kernel) {
        scratch_heap_array<uint_multiprecision_t> buf(heap, limbs);
        scratch_allocator_base                    scratch(buf.data(), limbs);
        kernel(scratch);
    };

    if (n < square_toom_cook_3_cutoff) {
        in_heap_scratch(karatsuba_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_karatsuba(result.first(result_total), a, scratch);
        });
    } else if (n < square_toom_cook_4_cutoff) {
        in_heap_scratch(toom_cook_3_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_toom_cook_3(result.first(result_total), a, scratch);
        });
    } else if (n < square_toom_cook_6_5_cutoff) {
        in_heap_scratch(toom_cook_4_storage_size(n), [&](scratch_allocator_base& scratch) {
            square_toom_cook_4(result.first(result_total), a, scratch);
        });
    } else {
        // n >= square_toom_cook_6_5_cutoff: Toom-6.5 / Toom-8.5, or FFT once
        // it overtakes at square_fft_cutoff. The FFT kernel packs into 64-bit
        // words, so it is gated to 64-bit limbs; on a 32-bit build the branch
        // is discarded and execution falls through to the Toom chain.
        bool used_fft = false;
        if constexpr (width_v<uint_multiprecision_t> == 64) {
            if (n >= square_fft_cutoff) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                scratch_heap_array<double>        fp_ws(heap, square_fft_fp_storage_size(n));
                scratch_heap_array<std::uint64_t> int_ws(heap, square_fft_int_storage_size(n));
                square_fft(result.first(result_total), a, fp_ws.span(), int_ws.span());
#else
                scratch_heap_array<std::uint64_t> ws(heap, square_fft_storage_size(n));
                square_fft(result.first(result_total), a, ws.span());
#endif
                used_fft = true;
            }
        }
        if (!used_fft) {
            if (n < square_toom_cook_8_5_cutoff) {
                in_heap_scratch(toom_cook_6_5_storage_size(n), [&](scratch_allocator_base& scratch) {
                    square_toom_cook_6_5(result.first(result_total), a, scratch);
                });
            } else {
                in_heap_scratch(toom_cook_8_5_storage_size(n), [&](scratch_allocator_base& scratch) {
                    square_toom_cook_8_5(result.first(result_total), a, scratch);
                });
            }
        }
    }

    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
}

std::size_t multiply_runtime(const std::span<uint_multiprecision_t>       result,
                             const std::span<const uint_multiprecision_t> a,
                             const std::span<const uint_multiprecision_t> b,
                             const scratch_heap_source&                   heap) {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());

    // x * x and x *= x pass the same span twice, so squaring detection is
    // a pointer compare that almost always fails fast for ordinary mul.
    if (a.data() == b.data() && a.size() == b.size()) {
        return square_runtime(result, a, heap);
    }

    const std::size_t min_size = std::min(a.size(), b.size());
    if (min_size >= karatsuba_cutoff) {
        // Power-of-two operands reduce to a shifted copy of the other
        // operand. This is only worth checking if we're about to do a big
        // number mul anyway.
        if (is_power_of_two_span(b)) {
            return multiply_power_of_two(result, a, b);
        }
        if (is_power_of_two_span(a)) {
            return multiply_power_of_two(result, b, a);
        }

        const std::size_t s            = std::max(a.size(), b.size());
        const std::size_t result_total = a.size() + b.size();

        const auto in_heap_scratch = [&](const std::size_t limbs, auto&& kernel) {
            scratch_heap_array<uint_multiprecision_t> buf(heap, limbs);
            scratch_allocator_base                    scratch(buf.data(), limbs);
            kernel(scratch);
        };

        if (min_size < toom_cook_3_cutoff) {
            const std::size_t storage_size = karatsuba_storage_size(s);
            if (storage_size <= karatsuba_stack_threshold) {
                uint_multiprecision_t  stack_buf[karatsuba_stack_threshold];
                scratch_allocator_base scratch(stack_buf, karatsuba_stack_threshold);
                multiply_karatsuba(result.first(result_total), a, b, scratch);
            } else {
                in_heap_scratch(storage_size, [&](scratch_allocator_base& scratch) {
                    multiply_karatsuba(result.first(result_total), a, b, scratch);
                });
            }
        } else if (min_size < toom_cook_4_cutoff) {
            in_heap_scratch(toom_cook_3_storage_size(s), [&](scratch_allocator_base& scratch) {
                multiply_toom_cook_3(result.first(result_total), a, b, scratch);
            });
        } else if (min_size < toom_cook_6_5_cutoff) {
            in_heap_scratch(toom_cook_4_storage_size(s), [&](scratch_allocator_base& scratch) {
                multiply_toom_cook_4(result.first(result_total), a, b, scratch);
            });
        } else {
            // min_size >= toom_cook_6_5_cutoff: Toom-6.5 / Toom-8.5, or FFT
            // once it overtakes at fft_mul_cutoff. Gated to 64-bit limbs (the
            // FFT kernel packs into 64-bit words); on a 32-bit build the
            // branch is discarded and execution falls to the Toom chain.
            bool used_fft = false;
            if constexpr (width_v<uint_multiprecision_t> == 64) {
                if (min_size >= fft_mul_cutoff) {
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                    scratch_heap_array<double>        fp_ws(heap, fft_mul_fp_storage_size(a.size(), b.size()));
                    scratch_heap_array<std::uint64_t> int_ws(heap, fft_mul_int_storage_size(a.size(), b.size()));
                    multiply_fft(result.first(result_total), a, b, fp_ws.span(), int_ws.span());
#else
                    scratch_heap_array<std::uint64_t> ws(heap, fft_mul_storage_size(a.size(), b.size()));
                    multiply_fft(result.first(result_total), a, b, ws.span());
#endif
                    used_fft = true;
                }
            }
            if (!used_fft) {
                if (min_size < toom_cook_8_5_cutoff) {
                    in_heap_scratch(toom_cook_6_5_storage_size(s), [&](scratch_allocator_base& scratch) {
                        multiply_toom_cook_6_5(result.first(result_total), a, b, scratch);
                    });
                } else {
                    in_heap_scratch(toom_cook_8_5_storage_size(s), [&](scratch_allocator_base& scratch) {
                        multiply_toom_cook_8_5(result.first(result_total), a, b, scratch);
                    });
                }
            }
        }
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    // Schoolbook long multiplication runtime fallback.
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        multiply_long(result, a, b);
    } else {
        multiply_long_runtime(result.data(), a.data(), a.size(), b.data(), b.size());
    }
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size() + b.size()});
}

std::size_t multiply_runtime_any(const std::span<uint_multiprecision_t>       result,
                                 const std::span<const uint_multiprecision_t> a_untrimmed,
                                 const std::span<const uint_multiprecision_t> b_untrimmed,
                                 const scratch_heap_source&                   heap) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_untrimmed.size() + b_untrimmed.size());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    if (a.size() == 1 && b.size() == 1) {
        const auto [lo, hi] = widening_mul(a[0], b[0]);
        result[0]           = lo;
        result[1]           = hi;
        return hi != 0 ? 2 : 1;
    }
    if (a.size() == 1) {
        return multiply_single_limb(result, b, a[0]);
    }
    if (b.size() == 1) {
        return multiply_single_limb(result, a, b[0]);
    }

    return multiply_runtime(result, a, b, heap);
}

} // namespace beman::big_int::detail
