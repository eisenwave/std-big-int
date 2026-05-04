# Examples

In this folder, we implement and describe several motivational, practical,
real-world examples making use of and showing the features and interfaces of
`beman.big_int`.

## Factorial

In [factorial.cpp](./factorial.cpp), we compute and verify the value of $100!$.

The factorial $n!$ is defined by

$$
n!=n{\times}(n-1){\times}(n-2){\times}(n-3){\times}{\cdot}{\times}3{\times}2{\times}1{\text{.}}
$$

The factorial of $1$ is $1$ and, by convention, the factorial of $0$ is also $1$.

Even though factorials have such a simple recurrence, they
can be found in many areas of physics, engineering and mathematics
such as combinatorics, probability, and analysis.

Factorials grow roughly exponentially approximately according to

$$
n!\sim\sqrt{2 {\pi} n}\left(\dfrac{n}{e}\right)^{n}{\text{,}}
$$

where $e{\approx}2.7182818$ is Euler's constant.

The factorial can be extended to the real-valued gamma function
${\Gamma}(x)$ via

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

They grow roughly exponentially approximately according to

$$
F_{n}\sim\dfrac{{\phi}^{n}}{\sqrt{5}}{\text{,}}
$$

where ${\phi}$ is the golden ratio.
