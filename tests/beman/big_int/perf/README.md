# Performance Measurements

## Simulated real-world case ECDSA sign/verify in elliptic curve space

One of our performance tests provides an intuitive view on elliptic-curve algebra
using big integer mathematics in the curved space. It depicts a well-known
cryptographic key-gen/sign/verify method ($256$-bit ECDSA, secp256k1).
This provides an overall performance indication using modest integer widths.
The test performs round-trip key generation, sign and verify with one
trial using random (but pre-defined) seeds, and $32$ trials using random seeds.

This is not intended to be a highly optimized ECDSA implementation, but
rather provide an overall performance indication due to the wide range of binary
and unary operations, and allocations used within it. The test is timed.
Runs with `beman.big_int`, `boost.cpp_int`, and `boost.gmp_int` are compared.
The code for this comparison can be found in [./elliptic_ecc.perf.cpp](./elliptic_ecc.perf.cpp).

| integer type     | time [s]   | relative |
|------------------|------------|----------|
| `beman.big_int`  | 2.0        | 1.3      |
| `boost.cpp_int`  | 1.7        | 1.1      |
| `boost.gmp_int`  | 1.6        | 1.0      |

## Multiplication

Multiplication graduates to successively _higher_ algorithms as the limb count grows.
For small limb counts under $48$ limbs, we use schoolbook multiplication.
Above $48$ limbs. the library crosses over to Karatsuba multiplication.
Successive Toom-Cook orders of $3$, $4$, $6.5$ and $8.5$ cross over
at $400$, $1,600$, $2,400$ and $15,000$ limbs, respectively for the non-SIMD path.
FFT multiplication crosses over at $24,000$ limbs for the non-SIMD path
and $4,500$ limbs for the SIMD path, thereby not using Toom-Cook $8.5$ in
the SIMD path at all. See the "Optional: SIMD-accelerated multiplication"
note in the top-level README.

When measuring, localize the time of `big_int` multiplication only, running a chrono
stopwatch just before and after the multiplication operation, summing and averaging the times.

Various runs use limb counts to emphasize crossover points such as the limb-cutoff
where Karatsuba --> Toom-Cook. Additional runs use higher limb counts, landing directly
in one of the ranges of testing an algoritm's complexity in as much of an isolated
way as possible. The runs involve quite high limb counts. By manipulating the constant-programmed
cutoffs, some indivicual measurements were performed to temporarily exclude certain algorithms,
then successively add, for instance, schoolbook only, then Karatsuba only and then
Karatsuba + Toom-Cook 3, and so forth.

- All the runs were numerically correct.
- We see that right around $300-400$ limbs, Toom-Cook 3 and 4 become quite beneficial.
- Higher Toom-Cook orders show similar trends. These are not explicitly tabulated, but rather summarized via their empirical timing results below.

### `beman.big_int` relative timings

| 64-bit limbs   | binary digit width   | schoolbook           |  with Karatsuba          | up to Toom-Cook 3     | up to Toom-Cook 4 | up to Toom-Cook 6.5 |
|----------------|----------------------|--------------------- |--------------------------|-----------------------|-------------------|---------------------|
| 700-1,400      | 44,800-89,600        |   1,270us per mul    |  380us    per mul        |   370us   per mul     | ---               | ---                 |
| 6,000-8,000    | 384,000-512,000      |   ---                |  8,220us  per mul        |   6,240us per mul     | ---               | ---                 |
| 10,000-12,000  | 640,000-768,000      |   ---                |  15,700us per mul        |  11,800us per mul     | ---               | ---                 |
| 21,000-23,000  | 1,344,000-1,472,000  |   ---                |  ---                     |  33,100us per mul     |  27,800us per mul | ---                 |
| 43,000-45,000  | 2,752,000-2,880,000  |   ---                |  ---                     |  ---                  |  72,300us per mul |  59,700us per mul   |

#### Toom-Cook 3 computational complexity

From the final two Toom-Cook 3 points, we seek the order of complexity, $x$

$$
2^x = \dfrac{33.1}{11.8}
$$

resulting in

$$
x{\approx}1.49
$$

The theoretical order of complexity is:

$$
\log_3(5) \approx 1.465{\text{,}}
$$

in good agreement with the measurement.

#### Toom-Cook 4 computational complexity
From the final two Toom-Cook 4 points, we seek the order of complexity, $x$

$$
2^x = \dfrac{72.3}{27.8}
$$

resulting in

$$
x{\approx}1.39
$$

The theoretical order of complexity is:

$$
\log_4(7) \approx 1.404{\text{,}}
$$

in good agreement with the measurement.

For a graphical representation of the progression of multiplication coplexity,
see also the plot below. It depicts the complexity of multiplication,
in relation to the operand limb count for many orders of increasing limb count.
Optimal cutoff points for crossing over from one multiplication scheme
to the next have been judiciously selected according to these and other related
data. This effort has been undertaken in order to optimize multiplication speed
across all orders of limb count.

![](./crossover_main.png)

### Compare multiplication timing `beman.big_int`, `boost.cpp_int`, `boost.gmp_int`

Detailed measurements comparing the multiplication performance of `big_int`
with those of `boost.cpp_int` and GMP (wrapped by `boost.gmp_int`) are presented
in the table below. The result of multiplication adds the widths of its operands.
So multiplying two big integers produces a result having double the width
of its operands.

The time and the relative time compared with the best (GMP) time (in square
brackets) are shown for each big-integer type at each width. These were measured
on an x86-64 machine with the SIMD multiplication path enabled (configured with
`-DBEMAN_BIG_INT_SIMD_MUL=ON`, so the FFT tier uses the AVX2 kernel); see the
"Optional: SIMD-accelerated multiplication" note in the top-level README.

| 64-bit limbs   | bit width    | approx base-10 digits | us per mul  `big_int`  | us per mul  `cpp_int`  | us per mul  `gmp_int` |
|----------------|--------------|-----------------------|------------------------|------------------------|-----------------------|
| 128            | 8,192        | 2,500                 |   9.8   [2.2]          |   8.2   [1.9]          |   4.4   [1.0]         |
| 512            | 32,768       | 9,900                 |   67    [2.5]          |   64    [2.4]          |   27    [1.0]         |
| 1,024          | 65,536       | 20,000                |   199   [2.7]          |   190   [2.6]          |   73    [1.0]         |
| 2,048          | 131,072      | 39,000                |   564   [3.0]          |   580   [3.0]          |   190   [1.0]         |
| 8,192          | 524,288      | 160,000               |   4,100 [3.2]          |   5,200 [4.1], see (2) |   1,300 [1.0]         |
| 32,768         | 2,097,152    | 631,000               |  17,000 [3.2]          |  47,000 [8.8]          |   5,300 [1.0]         |
| 131,072        | 8,388,608    | 2,525,000             |  72,000 [2.7], see (1) | 420,000 [15.7]         |  27,000 [1.0]         |

GMP is written in hand-coded assembly in its hot spots and is the _industry_
_standard_ of performance. With its FFT (small-prime NTT) tier, `beman.big_int`
now stays within roughly 3x of GMP across the entire range -- including the
largest sizes, where it previously fell behind (see (1)) -- which is quite
respectable for a portable C++ design. The code used for this comparison can be
found in [mul_big_int_vs_gmp_cpp.perf.cpp](./mul_big_int_vs_gmp_cpp.perf.cpp);
pass an operand width in limbs (and optionally a trial count) on the command line
to reproduce a row.

(1) `beman.big_int` now has FFT multiplication (a small-prime NTT, vectorized via
the optional SIMD path). Earlier it topped out at Toom-Cook, so `gmp_int` pulled
far ahead at very high digit counts (about $5.3x$ at $131,072$ limbs); the FFT brings
that back to about $2.7x$, tracking GMP's asymptotic complexity.

(2) `cpp_int` only reaches _as_ _high_ as Karatsuba multiplication, so both
`big_int` and `gmp_int` pull ahead at medium-high digit counts -- `big_int`'s lead
over `cpp_int` grows to roughly $6x$ by $131,072$ limbs.

Performance measurements of `big_int` versus `gmp_int` multiplication with mixed limb counts
ranging from $4{\ldots}30,000$ result in similar relative timings, with `big_int` staying
within roughly $2x{\ldots}3x$ of GMP across the entire range.
These measurements have been accomplished with the
program [mul_big_int_vs_gmp_cpp.limbs.cpp](./mul_big_int_vs_gmp_cpp.limbs.cpp).
In testing, many millions of trials have been performed with checks on
both performance testing as well as numerical correctness.

## Division

Division uses a combination of Knuth long division for small limb counts
and crosses over to the Burnikel-Ziegler algorithm somewhere between $40$ and $100$ limbs.
The performance of division relies predominantly on the speed of the underlying
multiplication algorithms as the limb count grows.

### Compare division timing `beman.big_int`, `boost.cpp_int`, `boost.gmp_int`

Detailed measurements comparing the division performance of `big_int`
with those of `boost.cpp_int` and GMP (wrapped by `boost.gmp_int`) are presented
in the table below. The division is setup to divide an $N$-digit numerator
by an $N/2$-digit denominator, producing an $N/2$-digit integer result.

When measuring, localize the time of `big_int` division only, running a chrono
stopwatch just before and after the multiplication operation, summing and averaging the times.

The time and the relative time compared with the best (GMP) time (in square
brackets) are shown for each big-integer type at each width. These were measured
on an x86-64 machine with the SIMD multiplication path enabled (configured with
`-DBEMAN_BIG_INT_SIMD_MUL=ON`, so the FFT tier uses the AVX2 kernel); see the
"Optional: SIMD-accelerated multiplication" note in the top-level README.
The performance of the underlying multiplication is propagated to division.

| 64-bit limbs   | bit width    | approx base-10 digits | us per div  `big_int`  | us per div  `cpp_int`  | us per div  `gmp_int` |
|----------------|--------------|-----------------------|------------------------|------------------------|-----------------------|
| 128            | 8,192        | 2,500                 |   4.2   [2.5]          |  10.4   [6.1]          |   1.7   [1.0]         |
| 512            | 32,768       | 9,900                 |   32    [1.7]          |  120    [6.7]          |   18    [1.0]         |
| 1,024          | 65,536       | 20,000                |   120   [2.3]          |   420   [7.9]          |   53    [1.0]         |
| 2,048          | 131,072      | 39,000                |   360   [3.0]          |  1,500  [13], see (1)  |   120   [1.0]         |
| 8,192          | 524,288      | 160,000               |   2,800 [2.9]          |  23,000 [24]           |   950   [1.0]         |
| 32,768         | 2,097,152    | 631,000               |  23,000 [3.8]          | 370,000 [62]           |   6,000 [1.0]         |

The code used for this comparison can be
found in [div_big_int_vs_gmp_cpp.perf.cpp](./div_big_int_vs_gmp_cpp.perf.cpp);
pass an operand width in limbs (and optionally a trial count) on the command line
to reproduce a row.

(1) `cpp_int` uses a variation of Knuth long division for all limb counts.
This algorithm has quadratic complexity . `big_int` and `gmp_int` pull ahead
rapidly at medium-high digit counts -- `big_int`'s lead
over `cpp_int` grows to roughly $8x$ by $8,192$ limbs.

Performance measurements of `big_int` versus `gmp_int` division with mixed limb counts
ranging from $4{\ldots}30,000$ result in similar relative timings, with `big_int` staying
within roughly $2x{\ldots}4x$ of GMP across the entire range.
These measurements have been carried out with the
program [div_big_int_vs_gmp_cpp.limbs.cpp](./div_big_int_vs_gmp_cpp.limbs.cpp).
In testing, many millions of trials have been performed with checks on
both performance testing as well as numerical correctness.

## Base-conversion and conversion to and from string representations

Sub-quadratic base-conversion ties directly into the library's `<charconv>` support,
itself built on the `to_chars` and `from_chars` functions, and the primitives
used therein. This provides a convenient interface for converting to and from
string representations of big integers. The performance of these operations
is influenced primarily by the efficiency of the underlying multiplication
and division algorithms, which are described in detail in the paragraphs above.

## Order-$N$ algorithms

Order-$N$ algorithms include addition, subtraction, bit-shift,
comparison, hashing and so forth. These are not explicitly tabulated
nor analyzed here. Their computational complexity is linear,
in relation to the limb count.

## Summary of performance measurement source files

| Source file                        | Description                           |
|------------------------------------|---------------------------------------|
| div_big_int_vs_gmp_cpp.limbs.cpp   | Division performance with fixed limbs and timing |
| div_big_int_vs_gmp_cpp.perf.cpp    | Division performance with mixed limbs, timing and numerical correctness |
| elliptic_ecc.perf.cpp              | Elliptic curve cryptography overall performance gauge at modest limb counts |
| mul_big_int_vs_gmp_cpp.limbs.cpp   | Multiplication performance with fixed limbs and timing |
| mul_big_int_vs_gmp_cpp.perf.cpp    | Multiplication performance with mixed limbs, timing and numerical correctness |
