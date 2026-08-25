#pragma once

#include "fabric/project/vector_asset.hpp"

#include <filesystem>

namespace fabric::render {

// Converts the bounded NanoSVG path subset into editable VectorAsset v2
// geometry. Unsupported paint features are reported instead of being dropped.
[[nodiscard]] project::VectorAssetResult convert_svg_to_native(
    const std::filesystem::path& path,
    const core::ResourceId& id,
    std::string name);

} // namespace fabric::render
