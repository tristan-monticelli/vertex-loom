#pragma once

#include "fabric/project/document.hpp"

#include <cstdint>

namespace fabric::project {

inline constexpr std::uint32_t current_asset_schema_version = 1;

using AssetDocument = DocumentHeader;

} // namespace fabric::project
