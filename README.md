# beman.big_int: Reference implementation of std::big_int

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

<!-- markdownlint-disable-next-line line-length -->
![Library Status](https://raw.githubusercontent.com/bemanproject/beman/refs/heads/main/images/badges/beman_badge-beman_library_under_development.svg) ![Continuous Integration Tests](https://github.com/bemanproject/big_int/actions/workflows/ci_tests.yml/badge.svg) ![Lint Check (pre-commit)](https://github.com/bemanproject/big_int/actions/workflows/pre-commit-check.yml/badge.svg) [![Coverage](https://coveralls.io/repos/github/bemanproject/big_int/badge.svg?branch=main)](https://coveralls.io/github/bemanproject/big_int?branch=main) ![Standard Target](https://github.com/bemanproject/beman/blob/main/images/badges/cpp29.svg)

`beman.big_int` is a minimal C++ library conforming to [The Beman Standard](https://github.com/bemanproject/beman/blob/main/docs/beman_standard.md).
This can be used as a template for those intending to write Beman libraries.
It may also find use as a minimal and modern  C++ project structure.

**Implements**: `std::big_int` proposed in [P4444R0](https://wg21.link/P4444R0).

**Status**: [Under development and not yet ready for production use.](https://github.com/bemanproject/beman/blob/main/docs/beman_library_maturity_model.md#under-development-and-not-yet-ready-for-production-use)

## License

`beman.big_int` is licensed under the Apache License v2.0 with LLVM Exceptions.

## Usage

TODO

Full runnable examples can be found in [`examples/`](examples/).

## Dependencies

### Build Environment

This project requires at least the following to build:

* A C++ compiler that conforms to the C++20 standard or greater
* CMake 3.30 or later
* (Test Only) GoogleTest

You can disable building tests by setting CMake option `BEMAN_BIG_INT_BUILD_TESTS` to
`OFF` when configuring the project.

### Supported Platforms

| Compiler   | Version | C++ Standards | Standard Library  |
|------------|---------|---------------|-------------------|
| GCC        | 15-13   | C++26-C++17   | libstdc++         |
| GCC        | 12-11   | C++23-C++17   | libstdc++         |
| Clang      | 22-19   | C++26-C++17   | libstdc++, libc++ |
| Clang      | 18-17   | C++26-C++17   | libc++            |
| Clang      | 18-17   | C++20, C++17  | libstdc++         |
| AppleClang | latest  | C++26-C++17   | libc++            |
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
