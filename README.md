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

## Documentation

For the full library documentation please see our [GitHub Pages Site](https://eisenwave.github.io/std-big-int/build_and_usage.html).

## License

`beman.big_int` is dual-licensed under Boost Software License 1.0 and the Apache License v2.0 with LLVM Exceptions.

## Building and Usage

For build requirements and supported platforms, building, the optional
SIMD-accelerated multiplication, installation, CMake integration, and basic
usage, see the
[Build and Usage](https://eisenwave.github.io/std-big-int/build_and_usage.html) documentation.

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## References

See the [References](https://eisenwave.github.io/std-big-int/references.html) documentation for the
works underpinning the algorithms used in this library.
