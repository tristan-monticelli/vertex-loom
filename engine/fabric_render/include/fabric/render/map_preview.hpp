#pragma once

#include "fabric/project/map.hpp"
#include "fabric/render/vector_geometry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fabric::render {

struct MapPreviewResult {
    std::vector<VectorDrawPacket> packets;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] MapPreviewResult resolve_map_preview(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::MapDocument& map,
    float animation_time = 0.0F);

} // namespace fabric::render
