#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/map.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_scene_schema_version = 1;

struct SceneMapReference {
    ResourceReference map{{}, "map"};
    std::string layer_id;
    friend bool operator==(const SceneMapReference&, const SceneMapReference&) = default;
};

struct SceneTransition {
    std::string id;
    ResourceReference target_scene{{}, "scene"};
    std::string entry_point;
    std::optional<core::ResourceId> event_id;
    friend bool operator==(const SceneTransition&, const SceneTransition&) = default;
};

struct SceneDocument {
    DocumentHeader document{
        .schema_version = current_scene_schema_version,
        .type = "scene",
    };
    std::vector<SceneMapReference> maps;
    std::optional<ResourceReference> entry_map;
    std::vector<SceneTransition> transitions;
    friend bool operator==(const SceneDocument&, const SceneDocument&) = default;
};

struct SceneResult {
    std::optional<SceneDocument> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return asset.has_value() && errors.empty(); }
};

struct SceneEntryPoint {
    std::string id;
    std::string instance_id;
    core::Vec2 position;
    friend bool operator==(const SceneEntryPoint&,
                           const SceneEntryPoint&) = default;
};

struct SceneCompositionResult {
    std::optional<MapDocument> map;
    std::vector<SceneEntryPoint> entry_points;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return map.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path scene_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_scene(const ProjectManifest&, const SceneDocument&);
[[nodiscard]] std::vector<ResourceReference> scene_resource_references(const SceneDocument&);
[[nodiscard]] std::string serialize_scene(const SceneDocument&);
[[nodiscard]] SceneResult parse_scene(const ProjectManifest&, std::string_view);
[[nodiscard]] SceneResult load_scene(const std::filesystem::path&, const ProjectManifest&,
                                     const std::filesystem::path&);
[[nodiscard]] SceneResult publish_scene(const std::filesystem::path&, const ProjectManifest&,
                                        const SceneDocument&);
[[nodiscard]] SceneCompositionResult compose_scene_maps(
    const std::filesystem::path&, const ProjectManifest&,
    const SceneDocument&);

} // namespace fabric::project
