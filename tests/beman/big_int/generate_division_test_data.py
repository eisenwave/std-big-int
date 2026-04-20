#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
#
# Generates test vectors for division and modulus of huge big_int values.
# Uses Python's arbitrary-precision integers as the reference implementation.
#
# Usage: python3 generate_division_test_data.py > division_huge.test.cpp

import random

LIMB_BITS = 64
LIMB_MAX = (1 << LIMB_BITS) - 1


def random_n_limb(n: int, rng: random.Random) -> int:
    """Generate a random positive integer with exactly n limbs (top limb nonzero)."""
    value = 0
    for i in range(n - 1):
        value |= rng.getrandbits(LIMB_BITS) << (i * LIMB_BITS)
    top = rng.randint(1, LIMB_MAX)
    value |= top << ((n - 1) * LIMB_BITS)
    return value


def trunc_div(a: int, b: int) -> int:
    """C++ truncated division (rounds toward zero)."""
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def trunc_mod(a: int, b: int) -> int:
    """C++ truncated modulus (remainder sign matches dividend)."""
    return a - trunc_div(a, b) * b


def emit_div_case(suite: str, name: str, a: int, b: int) -> str:
    q = trunc_div(a, b)
    r = trunc_mod(a, b)
    lines = []
    lines.append(f"TEST({suite}, {name}) {{")
    lines.append(f"    const big_int a = {a}_n;")
    lines.append(f"    const big_int b = {b}_n;")
    lines.append(f"    const big_int expected_q = {q}_n;")
    lines.append(f"    const big_int expected_r = {r}_n;")
    lines.append("")
    lines.append("    EXPECT_EQ(a / b, expected_q);")
    lines.append("    EXPECT_EQ(a % b, expected_r);")
    lines.append("    EXPECT_EQ((a / b) * b + (a % b), a);")
    lines.append("}")
    return "\n".join(lines)


def emit_invariant_case(suite: str, name: str, a: int, b: int) -> str:
    """Emit a test that only checks the division invariant, not exact values."""
    lines = []
    lines.append(f"TEST({suite}, {name}) {{")
    lines.append(f"    const big_int a = {a}_n;")
    lines.append(f"    const big_int b = {b}_n;")
    lines.append("")
    lines.append("    const big_int q = a / b;")
    lines.append("    const big_int r = a % b;")
    lines.append("    EXPECT_EQ(q * b + r, a);")
    if b > 0:
        lines.append("    EXPECT_TRUE(r >= 0_n);")
        lines.append(f"    EXPECT_TRUE(r < {b}_n);")
    lines.append("}")
    return "\n".join(lines)


def main():
    rng = random.Random(73)
    cases = []

    # ---- 1. Multi-limb / multi-limb with exact quotient and remainder ----
    # 10-limb / 5-limb
    a = random_n_limb(10, rng)
    b = random_n_limb(5, rng)
    cases.append(emit_div_case("DivisionHuge", "TenByFiveLimbs", a, b))

    # 20-limb / 10-limb
    a = random_n_limb(20, rng)
    b = random_n_limb(10, rng)
    cases.append(emit_div_case("DivisionHuge", "TwentyByTenLimbs", a, b))

    # 8-limb / 3-limb
    a = random_n_limb(8, rng)
    b = random_n_limb(3, rng)
    cases.append(emit_div_case("DivisionHuge", "EightByThreeLimbs", a, b))

    # ---- 2. Exact division (product / factor == factor, remainder 0) ----
    f1 = random_n_limb(5, rng)
    f2 = random_n_limb(5, rng)
    product = f1 * f2
    cases.append(emit_div_case("DivisionHuge", "ExactTenLimbProduct", product, f1))

    f1 = random_n_limb(8, rng)
    f2 = random_n_limb(4, rng)
    product = f1 * f2
    cases.append(emit_div_case("DivisionHuge", "ExactTwelveLimbProduct", product, f1))

    # ---- 3. Large dividend / single limb (short division path) ----
    a = random_n_limb(15, rng)
    b = rng.randint(2, LIMB_MAX)
    cases.append(emit_div_case("DivisionHuge", "FifteenLimbByOneLimb", a, b))

    a = random_n_limb(30, rng)
    b = rng.randint(2, LIMB_MAX)
    cases.append(emit_div_case("DivisionHuge", "ThirtyLimbByOneLimb", a, b))

    # ---- 4. Nearly equal huge numbers ----
    base = random_n_limb(10, rng)
    a = base + rng.randint(1, 1000)
    b = base
    cases.append(emit_div_case("DivisionHuge", "NearlyEqualTenLimb", a, b))

    base = random_n_limb(6, rng)
    a = base * 3 + rng.randint(1, base - 1)
    cases.append(emit_div_case("DivisionHuge", "SmallQuotientSixLimb", a, base))

    # ---- 5. Power-of-two edge cases ----
    # (2^512) / (2^256) == 2^256
    a = 1 << 512
    b = 1 << 256
    cases.append(emit_div_case("DivisionHuge", "PowerOfTwo512By256", a, b))

    # (2^1024 - 1) / (2^512 - 1)
    a = (1 << 1024) - 1
    b = (1 << 512) - 1
    cases.append(emit_div_case("DivisionHuge", "AllOnes1024By512", a, b))

    # (2^1024 - 1) / (2^512 + 1) - the +1 makes remainder interesting
    a = (1 << 1024) - 1
    b = (1 << 512) + 1
    cases.append(emit_div_case("DivisionHuge", "AllOnes1024ByPow512Plus1", a, b))

    # ---- 6. Signed huge values ----
    a = random_n_limb(10, rng)
    b = random_n_limb(4, rng)
    cases.append(emit_div_case("DivisionHugeSigned", "NegDividend", -a, b))
    cases.append(emit_div_case("DivisionHugeSigned", "NegDivisor", a, -b))
    cases.append(emit_div_case("DivisionHugeSigned", "BothNeg", -a, -b))

    # ---- 7. Divisor just barely smaller than dividend ----
    # Quotient = 2 exactly
    half = random_n_limb(8, rng)
    a = half * 2
    cases.append(emit_div_case("DivisionHuge", "QuotientExactlyTwo", a, half))

    # Quotient = max_limb (all bits set in one limb)
    b = random_n_limb(4, rng)
    a = b * LIMB_MAX + rng.randint(0, b - 1)
    cases.append(emit_div_case("DivisionHuge", "QuotientMaxLimb", a, b))

    # ---- 8. Very large: 50-limb / 25-limb ----
    a = random_n_limb(50, rng)
    b = random_n_limb(25, rng)
    cases.append(emit_div_case("DivisionHuge", "FiftyByTwentyFiveLimbs", a, b))

    # ---- 9. Division invariant stress tests with random sizes ----
    for i in range(5):
        na = rng.randint(10, 40)
        nb = rng.randint(2, na)
        a = random_n_limb(na, rng)
        b = random_n_limb(nb, rng)
        cases.append(emit_invariant_case("DivisionHugeInvariant", f"Random{i}_{na}by{nb}", a, b))

    # ---- 10. Modulus-specific: remainder should be small ----
    # Large number mod small prime
    a = random_n_limb(20, rng)
    p = 1000000007
    r = a % p
    lines = []
    lines.append("TEST(ModulusHuge, LargeModSmallPrime) {")
    lines.append(f"    const big_int a = {a}_n;")
    lines.append(f"    EXPECT_EQ(a % big_int{{{p}}}, {r});")
    lines.append("}")
    cases.append("\n".join(lines))

    # Large number mod (2^64 - 1) -- Mersenne-like
    a = random_n_limb(20, rng)
    mersenne = (1 << 64) - 1
    r = a % mersenne
    lines = []
    lines.append("TEST(ModulusHuge, LargeModMersenne64) {")
    lines.append(f"    const big_int a = {a}_n;")
    lines.append(f"    const big_int m = {mersenne}_n;")
    lines.append(f"    EXPECT_EQ(a % m, {r}_n);")
    lines.append("}")
    cases.append("\n".join(lines))

    # ---- Print ----
    print("// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception")
    print("// SPDX-License-Identifier: BSL-1.0")
    print("//")
    print("// Auto-generated by generate_division_test_data.py - do not edit manually.")
    print()
    print("#include <gtest/gtest.h>")
    print()
    print("#include <beman/big_int/big_int.hpp>")
    print()
    print("using beman::big_int::big_int;")
    print("using namespace beman::big_int::literals;")
    print()
    for case in cases:
        print(case)
        print()


if __name__ == "__main__":
    main()
