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

## Roots

We explore integer roots in [roots.cpp](./roots.cpp). A $1,001$ decimal digit
unsigned integer is selected. Local implementations of `sqrt` and `cbrt`
are used to take the integer square root and integer cube root af this argument.
Well-known root-finding algorithms from the literature are used.

The integer results are exact and the calculated values are compared
with known control values in order to verify numerical correctness.

The $1,001$ decimal digit test argument is

```c++
inline static constexpr char arg_digits[] =
    "3"
    "1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679"
    "8214808651328230664709384460955058223172535940812848111745028410270193852110555964462294895493038196"
    "4428810975665933446128475648233786783165271201909145648566923460348610454326648213393607260249141273"
    "7245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094"
    "3305727036575959195309218611738193261179310511854807446237996274956735188575272489122793818301194912"
    "9833673362440656643086021394946395224737190702179860943702770539217176293176752384674818467669405132"
    "0005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235"
    "4201995611212902196086403441815981362977477130996051870721134999999837297804995105973173281609631859"
    "5024459455346908302642522308253344685035261931188171010003137838752886587533208381420617177669147303"
    "5982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989";
```

The expected square root result is

```c++
inline static constexpr char ctrl_sqrt[] =
    "1772453850905516027298167483341145182797549456122387128213807789852911284591032181374950656738544665"
    "4162268236242825706662361528657244226025250937096027870684620376986531051228499251730289508262289320"
    "9537926796280017463901535147972051670019018523401858544697449491264031392177552590621640541933250090"
    "6398407613733477475153433667989789365851836408795451165161738760059067393431791332809854846248184902"
    "0546548521956132515616474675150427387610561079961271072100603720444836723652966137080943234988316684"
    "2";
```

The expected cube root result is

```c++
inline static constexpr char ctrl_cbrt[] =
    "3155367569301821867326519405336421207498251961314999997901193388809739079012897744887631254739292007"
    "9079474336184847584816275489895017190892204824599487754328163424105552825406129605014330212966409604"
    "2345022792020993821188771907712942838554336194775802398318469748078708796622343248668272179222147264"
    "8796720202642738747343051078071241";
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
8911508676488368383475561283727944998457661480546015847355276362591769064854598469023499142072222587968711568262291618443921820762599423351134473334554903
```

Optionally near the top of the example's source file, define `BEMAN_BIG_INT_EXAMPLE_PRIMES_USE_ENTROPY`
to use psudo-random system entropy. Then the calculated primes appear more random and are
different in each program run.

It is also possible Increase the number of `max_prime_count` to obtain more primes.

To independently check the primality (or lack thereof), a quick query can be made at
[Wolfram Alpha(R)](https://www.wolframalpha.com/). This is shown, for example, in
[this link](https://www.wolframalpha.com/input?i=PrimeQ%5B8911508676488368383475561283727944998457661480546015847355276362591769064854598469023499142072222587968711568262291618443921820762599423351134473334554903%5D)
and listed below. The response contains the expected text _this_ _is_ _a_ _prime_ _number_.

```
PrimeQ[8911508676488368383475561283727944998457661480546015847355276362591769064854598469023499142072222587968711568262291618443921820762599423351134473334554903]
```

### Check the prime number theorem

The program also calculates the prime density found at the end of its short run.
In this fixed-seed configuration, we find $1/311.0$ primes found
per candidate tested. The theoretical value from the bold conjecture
of the prime number theorem is $1/355.1$.
Our empirical result is somewhat close to the theoretical value.
To get an accurate estimate of the prime density,
very many more trials - resulting in millions of found primes in fact - must be run.
