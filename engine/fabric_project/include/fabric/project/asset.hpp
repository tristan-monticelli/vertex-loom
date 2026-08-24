#pragma once

#include "fabric/core/resource_id.hpp"

#include <cstdint>
#include <string>

namespace fabric::project {

inline constexpr std::uint32_t current_asset_schema_version = 1;

struct AssetDocument {
    std::uint32_t schema_version{current_asset_schema_version};
    std::string type;
    core::ResourceId id;
    std::string name;

    friend bool operator==(const AssetDocument&, const AssetDocument&) = default;
};

} // namespace fabric::project
