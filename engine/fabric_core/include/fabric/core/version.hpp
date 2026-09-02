#pragma once

#include "fabric/core/version_config.hpp"

#include <string_view>

namespace fabric::core {

[[nodiscard]] constexpr std::string_view version() noexcept {
    return VERTEX_LOOM_VERSION;
}

} // namespace fabric::core
