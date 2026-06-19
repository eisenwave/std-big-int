# beman.big_int: Reference implementation of std::big_int

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

<!-- markdownlint-disable-next-line line-length -->
![Library Status](https://raw.githubusercontent.com/bemanproject/beman/refs/heads/main/images/badges/beman_badge-beman_library_under_development.svg)
![Continuous Integration Tests](https://github.com/eisenwave/std-big-int/actions/workflows/ci_tests.yml/badge.svg)
![Lint Check (pre-commit)](https://github.com/eisenwave/std-big-int/actions/workflows/pre-commit-check.yml/badge.svg)
[![Coverage](https://coveralls.io/repos/github/eisenwave/std-big-int/badge.svg?branch=main)](https://coveralls.io/github/eisenwave/std-big-int?branch=main)
![Standard Target](https://github.com/bemanproject/beman/blob/main/images/badges/cpp29.svg)

**Implements**: `std::big_int` proposed in [P4444](https://isocpp.org/files/papers/D4444R0.html).

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

`beman.big_int` is a minimal C++ library conforming to [The Beman Standard](https://github.com/bemanproject/beman/blob/main/docs/beman_standard.md).
This can be used as a template for those intending to write Beman libraries.
It may also find use as a minimal and modern  C++ project structure.

## License

`beman.big_int` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Building and Usage

For build requirements and supported platforms, building, the optional
SIMD-accelerated multiplication, installation, CMake integration, and basic
usage, see the
[Build and Usage](doc/modules/ROOT/pages/build_and_usage.adoc) documentation.

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## References

[1] R. P. Brent and P. Zimmermann, *Modern Computer Arithmetic*, Cambridge Monographs on Applied and Computational Mathematics, vol. 18. Cambridge, U.K.: Cambridge University Press, 2011.

[2] D. E. Knuth, *The Art of Computer Programming, Vol. 2: Seminumerical Algorithms*, 3rd ed. Boston, MA, USA: Addison-Wesley, 1997.

[3] J. Arndt, *Matters Computational: Ideas, Algorithms, Source Code*. Berlin, Germany: Springer-Verlag, 2011.

[4] C. Burnikel and J. Ziegler, "Fast recursive division," Max-Planck-Institut für Informatik, Saarbrücken, Germany, Tech. Rep. MPI-I-98-1-022, Oct. 1998.

[5] M. Bodrato, "Towards optimal Toom-Cook multiplication for univariate and multivariate polynomials in characteristic 2 and 0," in *Proc. 1st Int. Workshop Arithmetic of Finite Fields (WAIFI 2007)*, ser. Lecture Notes in Computer Science, C. Carlet and B. Sunar, Eds., vol. 4547. Berlin, Germany: Springer, Jun. 2007, pp. 116–133.

[6] M. Bodrato, "High degree Toom'n'half for balanced and unbalanced multiplication," in *Proc. 20th IEEE Symp. Computer Arithmetic (ARITH 2011)*, E. Antelo, D. Hough, and P. Ienne, Eds. Washington, DC, USA: IEEE Computer Society, Jul. 2011, pp. 15–22.
