#pragma once

#include <string_view>

namespace fabric::core {

[[nodiscard]] constexpr std::string_view version() noexcept {
    return "0.1.0";
}

} // namespace fabric::core
