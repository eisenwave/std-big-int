// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_NUMERIC_HPP
#define BEMAN_BIG_INT_NUMERIC_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <beman/big_int/big_int.hpp>
#include <beman/big_int/detail/gcd_impl.hpp>

namespace beman::big_int {

// Returns the absolute value (magnitude) of `x`.
// This cannot overflow since `basic_big_int` is unbounded
template <class T>
    requires detail::is_basic_big_int_v<T>
constexpr std::remove_cvref_t<T> abs(T&& x) {
    std::remove_cvref_t<T> result(std::forward<T>(x));
    result.unchecked_set_sign(false);
    return result;
}

// [numeric.sat.cast]
// Casts `x` to `R`, clamping to the range of `R`. If the integer value of `x`
// is representable as `R`, that value is returned; otherwise the largest or
// smallest representable value of `R`, whichever is closer to `x`.
template <class R, std::size_t b, class L, class A>
    requires detail::signed_or_unsigned<R>
constexpr R saturating_cast(const basic_big_int<b, L, A>& x) noexcept {
    using U = detail::make_unsigned_t<R>;

    constexpr std::size_t width = detail::width_v<R>;
    if constexpr (detail::unsigned_integer<R>) {
        constexpr R hi = static_cast<R>(~U{0});

        if (x < R{0}) {
            return R{0};
        }
        if (x > hi) {
            return hi;
        }
    } else {
        constexpr R lo = static_cast<R>(U{1} << (width - 1));
        constexpr R hi = static_cast<R>((U{1} << (width - 1)) - U{1});

        if (x < lo) {
            return lo;
        }
        if (x > hi) {
            return hi;
        }
    }

    return static_cast<R>(x);
}

namespace detail {

// A fundamental-integer operand has to have its magnitude spelled out into limbs
// before the span algorithms can see it; a `basic_big_int` already stores limbs,
// so it needs no storage at all.
template <class T>
[[nodiscard]] constexpr auto gcd_operand_limbs(const T& x) noexcept {
    if constexpr (is_basic_big_int_v<T>) {
        return std::array<uint_multiprecision_t, 0>{};
    } else {
        return to_limbs(uabs(x));
    }
}

// The magnitude of an operand as a limb span, paired with the storage handed out
// by `gcd_operand_limbs` for the fundamental-integer case.
template <class T, std::size_t n>
[[nodiscard]] constexpr std::span<const uint_multiprecision_t>
gcd_operand_magnitude(const T& x, const std::array<uint_multiprecision_t, n>& limbs) noexcept {
    if constexpr (is_basic_big_int_v<T>) {
        return x.representation();
    } else {
        return std::span<const uint_multiprecision_t>{limbs.data(), n};
    }
}

} // namespace detail

// [numeric.gcd]
// Computes the greatest common divisor of integers m and n
// If either `M` or `N` is not an integer type, or if either is
// (possibly cv-qualified) `bool` the program is ill-formed
// If either |M| or |N| is not representable as a value of type
// `std::common_type<M, N>`, the behavior is undefined
//
// The common type here is always a `basic_big_int`, which is unbounded, so no
// argument magnitude can fail to be representable. The result is never negative:
// gcd(m, n) == gcd(|m|, |n|), and gcd(0, 0) is 0.
//
// The parameters are taken by value, matching `std::gcd`: the signature makes this
// overload the more constrained of the two, so an unqualified call with a
// `basic_big_int` argument is not ambiguous with `std::gcd`. Pass an rvalue to hand
// a big_int argument's storage over to the algorithm.
template <class M, class N>
    requires detail::common_big_int_type_with<M, N>
[[nodiscard]] constexpr detail::common_big_int_type<M, N> gcd(M m, N n) {
    using Result     = detail::common_big_int_type<M, N>;
    using const_span = std::span<const uint_multiprecision_t>;

    // Every value the function creates -- including the one it returns -- uses the
    // allocator of the big_int argument, which is also what `abs` does.
    const auto alloc = [&] {
        if constexpr (detail::is_basic_big_int_v<M>) {
            return m.get_allocator();
        } else {
            return n.get_allocator();
        }
    }();

    // |value| as a result the algorithm below may consume in place. A big_int
    // argument was already copied into the parameter by the call, so its storage
    // is handed over rather than copied again.
    const auto magnitude_of = [&alloc]<class T>(T& value) -> Result {
        if constexpr (detail::is_basic_big_int_v<T>) {
            Result r{std::move(value)};
            r.unchecked_set_sign(false);
            return r;
        } else {
            return Result{detail::uabs(value), alloc};
        }
    };

    // Both operands are viewed as bare magnitudes: the signs are dropped here and
    // never looked at again.
    const auto       m_store = detail::gcd_operand_limbs(m);
    const auto       n_store = detail::gcd_operand_limbs(n);
    const const_span m_mag   = detail::gcd_operand_magnitude(m, m_store);
    const const_span n_mag   = detail::gcd_operand_magnitude(n, n_store);
    const const_span m_trim  = m_mag.first(detail::trimmed_size_span(m_mag));
    const const_span n_trim  = n_mag.first(detail::trimmed_size_span(n_mag));

    // gcd(x, 0) == |x|, and so gcd(0, 0) == 0.
    if (detail::is_span_zero(n_trim)) {
        return magnitude_of(m);
    }
    if (detail::is_span_zero(m_trim)) {
        return magnitude_of(n);
    }

    // Small operands never allocate: a magnitude that fits a limb is handled by
    // the scalar algorithm, and a single-limb operand reduces the other one with
    // a short division rather than a copy.
    if (m_trim.size() == 1 && n_trim.size() == 1) {
        return Result{detail::gcd_limbs(m_trim[0], n_trim[0]), alloc};
    }
    if (n_trim.size() == 1) {
        return Result{detail::gcd_short(m_trim, n_trim[0]), alloc};
    }
    if (m_trim.size() == 1) {
        return Result{detail::gcd_short(n_trim, m_trim[0]), alloc};
    }

    // Both magnitudes span several limbs, so the reduction needs writable copies
    // of them. Everything past this point works on `a` and `b`; the views above
    // are not touched again.
    Result a = magnitude_of(m);
    Result b = magnitude_of(n);

    // Euclidean steps while the operands are far apart in size: one division wipes
    // out a bit-length gap that the reduction below would otherwise grind away a
    // bit at a time. Both branches leave the remainder in `b`, and each shrinks
    // the larger operand below the smaller, so a gap this wide is gone after a
    // step or two. A single-limb operand is left to the reduction itself, which
    // finishes those with a short division.
    constexpr std::size_t gap = detail::width_v<uint_multiprecision_t> / 2;
    for (;;) {
        if (detail::trimmed_size_span(a.representation()) == 1 || detail::trimmed_size_span(b.representation()) == 1) {
            break;
        }
        const std::size_t a_bits = a.width_mag();
        const std::size_t b_bits = b.width_mag();
        if (a_bits > b_bits + gap) {
            Result r = a % b;
            a        = std::move(b);
            b        = std::move(r);
        } else if (b_bits > a_bits + gap) {
            b = b % a;
        } else {
            break;
        }
        if (detail::is_span_zero(b.representation())) {
            return a; // `a` divides the other operand exactly.
        }
    }

    const std::size_t na    = detail::trimmed_size_span(a.representation());
    const std::size_t nb    = detail::trimmed_size_span(b.representation());
    const std::size_t width = std::max(na, nb) + 1;

    // Lehmer's reduction needs a third buffer and a limb of headroom in each
    // operand. Narrower operands are reduced by binary steps alone, which need
    // neither, so nothing extra is allocated for them.
    const bool wide = std::max(na, nb) >= 3;
    Result     t{alloc};
    if (wide) {
        a.reserve_representation(width);
        b.reserve_representation(width);
        t.reserve_representation(width);
    }

    const std::size_t size =
        detail::gcd_unsigned_spans(std::span<uint_multiprecision_t>{a.limb_ptr(), a.representation_capacity()},
                                   na,
                                   std::span<uint_multiprecision_t>{b.limb_ptr(), b.representation_capacity()},
                                   nb,
                                   wide ? std::span<uint_multiprecision_t>{t.limb_ptr(), t.representation_capacity()}
                                        : std::span<uint_multiprecision_t>{});

    a.unchecked_set_limb_count(static_cast<std::uint32_t>(size));
    if (a.is_representation_inplace()) {
        // Restore the "limbs above the limb count are zero" invariant that inline
        // storage carries.
        uint_multiprecision_t* const limbs = a.limb_ptr();
        for (std::size_t i = size; i < Result::inplace_capacity; ++i) {
            limbs[i] = 0;
        }
    }
    return a;
}

} // namespace beman::big_int

#endif // BEMAN_BIG_INT_NUMERIC_HPP
