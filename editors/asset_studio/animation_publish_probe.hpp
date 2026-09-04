#pragma once

#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <string>

namespace fabric::asset_studio {

struct AnimationPublishProof {
    bool map_published{};
    bool package_published{};
    bool package_contains_animation{};
    bool runtime_loaded{};
    bool runtime_ran{};
    bool animation_evaluated{};
    bool target_node_evaluated{};
    bool marker_evaluated{};

    [[nodiscard]] bool ok() const noexcept {
        return map_published && package_published &&
            package_contains_animation && runtime_loaded && runtime_ran &&
            animation_evaluated && target_node_evaluated && marker_evaluated;
    }
};

[[nodiscard]] AnimationPublishProof prove_published_animation_workflow(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const std::string& entity_id,
    const std::string& animation_id,
    const std::string& target_node_id,
    float evaluation_time);

} // namespace fabric::asset_studio
