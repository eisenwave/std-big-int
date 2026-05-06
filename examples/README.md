# Examples

In this folder, we implement and describe several motivational, practical,
real-world examples making use of and showing the features and interfaces of
`beman.big_int`.

## Factorial

In [factorial.cpp](./factorial.cpp), we compute and verify the value of $100!$.

The factorial $n!$ is defined by

$$
n!=n{\times}(n-1){\times}(n-2){\times}(n-3){\times}{\cdots}{\times}3{\times}2{\times}1{\text{.}}
$$

The factorial of $1$ is $1$ and, by convention, the factorial of $0$ is also $1$.

Even though factorials have such a simple recurrence, they
can be found in many areas of physics, engineering and mathematics
such as combinatorics, probability, and analysis.

Factorials grow roughly exponentially, approximately according to

$$
n!\sim\sqrt{2 {\pi} n}\left(\dfrac{n}{e}\right)^{n}{\text{,}}
$$

where $e{\approx}2.7182818$ is Euler's constant.

The factorial can be extended to the real-valued gamma function
${\Gamma}(x)$ (and more generally the complex-valued one) via

$$
n! = {\Gamma\left(n+1\right)}{\text{,}}
$$

and in our example $100!={\Gamma(101)}{\approx}9.3326215{\times}10^{157}$.

## Fibonacci

In [fibonacci.cpp](./fibonacci.cpp), we compute and verify
the $1,000^{th}$ Fibonacci number.

Fibonacci numbers are defined by the recursion

$$
F_{n+1}=F_{n}+F_{n-1}{\text{.}}
$$

Fibonacci numbers are fascinating because their simple recurrence
is deeply connected to rich structures that can be found everywhere
in nature and mathematics.

They grow roughly exponentially, approximately according to

$$
F_{n}\sim\dfrac{{\varphi}^{n}}{\sqrt{5}}{\text{,}}
$$

where ${\varphi}{\approx}1.6180340$ is the golden ratio.

## Primes

A more in-depth example can be found in [primes.cpp](./primes.cpp).
Here, we compute several pseudo-random $512$-bit unsigned integer primes.
For primality testing, we use the well-known Miller-Rabin test.

In this example, the basic number-theoretical function `powm(b, p, m)` is needed.
It has been implemented locally. This function computes $b^p{\text{mod}}m$.

In the delivery state of this example, fixed, non-random seeds are used.
In this configuration, the values of the calculated primes are deterministic
and the same for each run. The default delivery seeks in total, $1024$ pseudo-random
$512$-bit primes.

In default mode, the final, $1024^{th}$ prime found is

```c++
11467539641765088900397755761948757342740339393956341880365809291331468652813173081510423427126578730302881069436310288474659389618552505369342345643941077
```

Optionally near the top of the example's source file, define `BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY`
to use psudo-random system entropy. Then the calculated primes appear more random and are
different in each program run.

It is also possible Increase the number of `max_prime_count` to obtain more primes.

To independently check the primality (or lack thereof), a quick query can be made at
[Wolfram Alpha(R)](https://www.wolframalpha.com/). This is shown, for example, in
[this link](https://www.wolframalpha.com/input?i=PrimeQ%5B11467539641765088900397755761948757342740339393956341880365809291331468652813173081510423427126578730302881069436310288474659389618552505369342345643941077%5D)
and listed below. The response contains the expected text _this_ _is_ _a_ _prime_ _number_.

```
PrimeQ[11467539641765088900397755761948757342740339393956341880365809291331468652813173081510423427126578730302881069436310288474659389618552505369342345643941077]
```

### Check the prime number theorem

The program also calculates the prime density found at the end of its short run.
In this fixed-seed configuration, we find $1/346.0$ primes found
per candidate tested. The theoretical value from the bold conjecture
of the prime number theorem is $1/355.1$.
Our empirical result is somewhat close to the theoretical value.
To get an accurate estimate of the prime density,
very many more trials - resulting in millions of found primes in fact - must be run.
