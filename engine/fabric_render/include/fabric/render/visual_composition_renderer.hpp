#pragma once

#include "fabric/project/manifest.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/project/visual_composition.hpp"
#include "fabric/render/vector_geometry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fabric::render {

struct VisualCompositionDrawResult {
    std::vector<VectorDrawPacket> packets;
    core::Rect bounds{{-0.5F, -0.5F}, {1.0F, 1.0F}};
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] VisualCompositionDrawResult resolve_visual_composition(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComposition& composition);

[[nodiscard]] VisualCompositionDrawResult resolve_visual_component(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComponent& component,
    const project::VisualComponentInstance& instance = {});

} // namespace fabric::render
