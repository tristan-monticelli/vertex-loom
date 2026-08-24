#pragma once

#include <string>
#include <string_view>

namespace fabric::core {

struct ResourceId {
    std::string value;

    [[nodiscard]] static bool is_valid(std::string_view candidate) noexcept;

    friend bool operator==(const ResourceId&, const ResourceId&) = default;
};

} // namespace fabric::core
