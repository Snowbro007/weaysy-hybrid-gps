// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
namespace weaysy {
constexpr uint32_t local_minute(uint32_t raw, int32_t correction) noexcept {
    const int64_t result = (static_cast<int64_t>(raw) + correction) % 1440;
    return static_cast<uint32_t>(result < 0 ? result + 1440 : result);
}
}
