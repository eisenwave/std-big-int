// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "fuzz_common.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return ::beman::big_int::fuzz::run(std::multiplies<>{}, data, size);
}
