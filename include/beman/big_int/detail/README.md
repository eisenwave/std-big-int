## Performance Measurements

Performance testing `big_int` binary operations uses various control mechanisms, such as
a computer algebra system (for numerical correctness check).
When measuring, localize the time of `big_int` multiplication only, running a chrono
stopwatch just before and after the multiplication operation, summing and averaging the times.


### Multiplication

TODO: Follow the evolution of high-order multiplication and add relevant content accordingly.

TODO: Provide detailed measurements (with a table) comparing the multiplication performance
of `big_int` with that of GMP. The relative information below is good. But a measurement-based
comparison with the _industry_ _standard_ of performance will also be useful.

Various runs use limb counts to emphasize crossover points such as the limb-cutoff
where Karatsuba --> Toom-Cook. Additional runs use higher limb counts, landing directly
in one of the ranges of testing an algoritm's complexity in as much of an isolated
way as possible. The runs involve quite high limb counts. By manipulating the constant-programmed
cutoffs, some indivicual measurements were performed to temporarily exclude certain algorithms,
then successively add, for instance, schoolbook only, then Karatsuba only and then
Karatsuba + Toom-Cook 3, and so forth.

- All the runs were numerically correct.
- We see that right around 800 limbs, Toom-Cook 3 starts becoming advantageous.
- For larger limb counts, Toom-Cook 3 and 4 become quite beneficial.

| 64-bit limbs   | binary digit width   | schoolbook            |  with Karatsuba          | with Kara and Toom-Cook 3     | with Kara and Toom-Cook 3 and 4 |
|----------------|----------------------|---------------------- |--------------------------|-------------------------------|---------------------------------|
| 700-1,400      | 44,800-89,600        |   1,270us  per mul    |  380us    per mul        |   370us   per mul             | ---     |
| 6,000-8,000    | 384,000-512,000      |   ---                 |  8,220us  per mul        |   6,240us  per mul            | ---     |
| 10,000-12,000  | 640,000-768,000      |   ---                 |  15,700us per mul        |  11,800us per mul             | ---     |
| 21,000-23,000  | 1,344,000-1,472,000  |   ---                 |  ---                     |  33,100us per mul             |  27,800us per mul |
| 43,000-45,000  | 2,752,000-2,880,000  |   ---                 |  ---                     |  ---                          |  72,300us per mul |

### Toom-Cook 3 computational complexity
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

### Toom-Cook 4 computational complexity
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

### Division

TODO: Follow the progress of sub-quadratic division.

### Base-conversion

TODO: Follow the progress of sub-quadratic base-change.
