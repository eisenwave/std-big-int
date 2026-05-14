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

where $e{\approx}2.7182818{\ldots}$ is Euler's constant.

The factorial can be extended to the real-valued gamma function
${\Gamma}(x)$ (and more generally the complex-valued one) via

$$
n! = {\Gamma\left(n+1\right)}{\text{,}}
$$

and in our example $100!={\Gamma(101)}{\approx}9.3326215{\times}10^{157}$.

## Fibonacci

In [fibonacci.cpp](./fibonacci.cpp), we compute and verify
the $10,000^{th}$ Fibonacci number.

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

where ${\varphi}{\approx}1.6180340{\ldots}$ is the golden ratio.
The number is quite long when printed out. In fact, the approximate
value of the $10,000^{th}$ Fibonacci number is
$F_{10,000}{\approx}3.3644764{\ldots}{\times}{10^{2089}}$.

## Roots

We explore integer roots in [roots.cpp](./roots.cpp). A $1,001$ decimal digit
unsigned integer is selected. Local implementations of `sqrt` and `cbrt`
are used to take the integer square root and integer cube root af this argument.
Well-known root-finding algorithms from the literature are used.

The integer results are exact and the calculated values are compared
with known control values in order to verify numerical correctness.

The $1,001$ decimal digit test argument is

```c++
31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989
```

The expected square root result is

```c++
177245385090551602729816748334114518279754945612238712821380778985291128459103218137495065673854466541622682362428257066623615286572442260252509370960278706846203769865310512284992517302895082622893209537926796280017463901535147972051670019018523401858544697449491264031392177552590621640541933250090639840761373347747515343366798978936585183640879545116516173876005906739343179133280985484624818490205465485219561325156164746751504273876105610799612710721006037204448367236529661370809432349883166842
```

The expected cube root result is

```c++
3155367569301821867326519405336421207498251961314999997901193388809739079012897744887631254739292007907947433618484758481627548989501719089220482459948775432816342410555282540612960501433021296640960423450227920209938211887719077129428385543361947758023983184697480787087966223432486682721792221472648796720202642738747343051078071241
```

## Primes

A more in-depth example can be found in [primes.cpp](./primes.cpp).
Here, we compute several pseudo-random $512$-bit unsigned integer primes.
For primality testing, we use the well-known Miller-Rabin test.

In this example, the basic number-theoretical function `powm(b, p, m)` is needed.
It has been implemented locally. This function computes $b^p{\text{mod}}m$.

In the delivery state of this example, fixed, non-random seeds are used.
In this configuration, the values of the calculated primes are deterministic
and the same for each run. The default delivery seeks in total, $32$ pseudo-random
$512$-bit primes.

In default mode, the final prime found is

```c++
10143171719460317030776606042161455743692462665971704190268979952647685451042758582438652518606262043864611102965422197344907990728961691317186137657753121
```

Optionally near the top of the example's source file, define `BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY`
to use psudo-random system entropy. Then the calculated primes appear more random and are
different in each program run.

It is also possible Increase the number of `max_prime_count` to obtain more primes.

To independently check the primality (or lack thereof), a quick query can be made at
[Wolfram Alpha(R)](https://www.wolframalpha.com/). This is shown, for example, in
[this link](https://www.wolframalpha.com/input?i=PrimeQ%5B10143171719460317030776606042161455743692462665971704190268979952647685451042758582438652518606262043864611102965422197344907990728961691317186137657753121%5D)
and listed below. The response contains the expected text _this_ _is_ _a_ _prime_ _number_.

```
PrimeQ[10143171719460317030776606042161455743692462665971704190268979952647685451042758582438652518606262043864611102965422197344907990728961691317186137657753121]
```

### Check the prime number theorem

The program also calculates the prime density found at the end of its short run.
In this fixed-seed configuration, we find $1/324.1$ primes found
per candidate tested. The theoretical value from the bold conjecture
of the prime number theorem is $1/355.1$.
Our empirical result is somewhat close to the theoretical value.
To get an accurate estimate of the prime density,
very many more trials - resulting in millions of found primes in fact - must be run.

## RSA

A straightforward example portraying a $2048$-bit RSA encryption/decryption round-trip
is implemented in [rsa.cpp](./rsa.cpp). Known parameters from a published test case
in the literature are used.

The `powm(b, p, m)` is central to RSA encryption and decryption and
is needed for this example. It has been implemented locally.
This function computes $b^p{\text{mod}}m$.

The short text field

```c++
inline static constexpr char str_message[] = "Hello std-big-int RSA";
```

is encrypted and subsequently decrypted (recovered) using the calculated
$2048$-bit `big_int` cipher.
