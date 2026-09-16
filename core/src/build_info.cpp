#include "pigeon/core/build_info.hpp"

namespace pigeon::core {

std::string_view version() noexcept { return PIGEON_VERSION_STRING; }

}  // namespace pigeon::core
