# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

target_sources(
    beman.big_int
    PUBLIC
        FILE_SET HEADERS
            FILES
                big_int.hpp
                charconv.hpp
                string.hpp
                literals.hpp
                format.hpp
                numeric.hpp
                detail/base_conversion.hpp
                detail/base_tables.hpp
                detail/config.hpp
                detail/div_impl.hpp
                detail/floats.hpp
                detail/gcd_impl.hpp
                detail/mul_impl.hpp
                detail/scratch_allocator.hpp
                detail/siphash.hpp
                detail/span_ops.hpp
                detail/wide_ops.hpp
)
