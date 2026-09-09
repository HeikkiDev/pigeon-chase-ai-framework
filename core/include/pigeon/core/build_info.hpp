#pragma once

#include <string_view>

namespace pigeon::core {

/// Version of the hardware-independent core library.
[[nodiscard]] std::string_view version() noexcept;

}  // namespace pigeon::core
