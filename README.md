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

## Usage

Full runnable examples can be found in [`examples/`](examples/).
These exhibit many features of `beman.big_int`, showing the convenience
and efficiency of this type.

The examples progress from introductory to medium levels.
They are intended to portray the power, efficiency and ease-of-use
of `beman.big_int`. The aggregate type specifically behaves
very much like a built-in integral type. This makes it possible to use
`beman.big_int` essentially seammlessly with detailed algorithms,
built-in types, and visualization features such as output-streaming
and string representation. Efficient move-semantics ensure that intuitive
and easy-to-read code remains performant.

TODO: `constexpr`-ness.

The straightforward example below shows usage of `beman.big_int`. The code snippet
computes and verifies the value of $100!$ in its full, pure-integral, non-truncated form.

```c++
#include <beman/big_int/big_int.hpp>

#include <iomanip>
#include <iostream>

template <class BigIntType>
constexpr auto factorial(unsigned int n) -> BigIntType {
    return (n <= 1) ? 1 : n * factorial<BigIntType>(n - 1);
}

auto main() -> int {
    using beman::big_int::big_int;

    // Compute the 100th Factorial number.
    const big_int fact_100{factorial<big_int>(100)};

    using namespace beman::big_int::literals;

    const big_int bn_ctrl{
        93326215443944152681699238856266700490715968264381621468592963895217599993229915608941463976156518286253697920827223758251185210916864000000000000000000000000_n};

    const bool result_is_ok{fact_100 == bn_ctrl};

    std::cout << "fact_100:\n"
              << to_string(fact_100) << "\n\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
```

## Dependencies

### Build Environment

This project requires at least the following to build:

* A C++ compiler that conforms to the C++23 standard or greater
* CMake 3.30 or later
* (Test Only) GoogleTest

You can disable building tests by setting CMake option `BEMAN_BIG_INT_BUILD_TESTS` to
`OFF` when configuring the project.

### Supported Platforms

| Compiler   | Version | C++ Standards | Standard Library  |
|------------|---------|---------------|-------------------|
| GCC        | 15-13   | C++26-C++23   | libstdc++         |
| GCC        | 12-11   | C++23-C++23   | libstdc++         |
| Clang      | 22-19   | C++26-C++23   | libstdc++, libc++ |
| Clang      | 18-17   | C++26-C++23   | libc++            |
| Clang      | 18-17   | C++23         | libstdc++         |
| AppleClang | latest  | C++26-C++23   | libc++            |
| MSVC       | latest  | C++23         | MSVC STL          |

## Development

See the [Contributing Guidelines](CONTRIBUTING.md).

## Integrate beman.big_int into your project

### Build

You can build big_int using a CMake workflow preset:

```bash
cmake --workflow --preset gcc-release
```

To list available workflow presets, you can invoke:

```bash
cmake --list-presets=workflow
```

For details on building beman.big_int without using a CMake preset, refer to the
[Contributing Guidelines](CONTRIBUTING.md).

### Optional: SIMD-accelerated multiplication

By default, multiplication of very large integers uses an exact integer
number-theoretic transform for its FFT tier. This is correct on every conforming
compiler and imposes no special build requirements.

A faster double-precision floating-point transform with hand-written SIMD kernels
(ARM NEON, x86 AVX2, selected at runtime) is available behind the CMake
option `BEMAN_BIG_INT_SIMD_MUL` (default `OFF`):

```bash
cmake --preset gcc-release -DBEMAN_BIG_INT_SIMD_MUL=ON
```

> [!IMPORTANT]
>
> The SIMD path is exact **only** under the default IEEE round-to-nearest mode with
> no fast-math and no floating-point contraction. When built with this project's
> CMake, the required flags (`-ffp-contract=off -fno-fast-math`, or `/fp:strict` on
> MSVC) are applied to the relevant translation units automatically. If you build
> with a different build system you must guarantee that floating-point environment
> yourself, or results may be silently incorrect. The default integer path has no
> such requirement.


### Installation

To install beman.big_int globally after building with the `gcc-release` preset, you can
run:

```bash
sudo cmake --install build/gcc-release
```

Alternatively, to install to a prefix, for example `/opt/beman`, you can run:

```bash
sudo cmake --install build/gcc-release --prefix /opt/beman
```

This will generate the following directory structure:

```txt
/opt/beman
├── include
│   └── beman
│       └── big_int
│           ├── big_int.hpp
│           └── ...
└── lib
    └── cmake
        └── beman.big_int
            ├── beman.big_int-config-version.cmake
            ├── beman.big_int-config.cmake
            └── beman.big_int-targets.cmake
```

### CMake Configuration

If you installed beman.big_int to a prefix, you can specify that prefix to your CMake
project using `CMAKE_PREFIX_PATH`; for example, `-DCMAKE_PREFIX_PATH=/opt/beman`.

You need to bring in the `beman.big_int` package to define the `beman::big_int` CMake
target:

```cmake
find_package(beman.big_int REQUIRED)
```

You will then need to add `beman::big_int` to the link libraries of any libraries or
executables that include `beman.big_int` headers.

```cmake
target_link_libraries(yourlib PUBLIC beman::big_int)
```

### Using beman.big_int

To use `beman.big_int` in your C++ project,
include an appropriate `beman.big_int` header from your source code.

```c++
#include <beman/big_int/big_int.hpp>
```

> [!NOTE]
>
> `beman.big_int` headers are to be included with the `beman/big_int/` prefix.
> Altering include search paths to spell the include target another way (e.g.
> `#include <big_int.hpp>`) is unsupported.
