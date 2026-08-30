#pragma once

#include "fabric/core/version.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_map_package_schema_version = 1;
inline constexpr std::string_view map_package_manifest_filename =
    "map-package.json";
inline constexpr std::uint32_t current_scene_package_schema_version = 1;
inline constexpr std::string_view scene_package_manifest_filename =
    "scene-package.json";

struct MapPackageResource {
    ResourceReference resource;
    std::filesystem::path document_path;
    std::vector<std::filesystem::path> payload_paths;
    friend bool operator==(const MapPackageResource&,
                           const MapPackageResource&) = default;
};

struct MapPackageManifest {
    std::uint32_t schema_version{current_map_package_schema_version};
    std::string type{"map-package"};
    core::ResourceId id;
    std::string name;
    std::string minimum_runtime_version;
    ResourceReference root_map;
    std::vector<MapPackageResource> resources;
    friend bool operator==(const MapPackageManifest&,
                           const MapPackageManifest&) = default;
};

struct MapPackageManifestResult {
    std::optional<MapPackageManifest> manifest;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return manifest.has_value() && errors.empty();
    }
};

struct MapPackagePublishResult {
    std::optional<MapPackageManifest> manifest;
    std::filesystem::path destination;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return manifest.has_value() && errors.empty();
    }
};

struct ScenePackageManifest {
    std::uint32_t schema_version{current_scene_package_schema_version};
    std::string type{"scene-package"};
    core::ResourceId id;
    std::string name;
    std::string minimum_runtime_version;
    ResourceReference root_scene{{}, "scene"};
    std::vector<MapPackageResource> resources;
    friend bool operator==(const ScenePackageManifest&,
                           const ScenePackageManifest&) = default;
};

struct ScenePackageManifestResult {
    std::optional<ScenePackageManifest> manifest;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return manifest.has_value() && errors.empty();
    }
};

struct ScenePackagePublishResult {
    std::optional<ScenePackageManifest> manifest;
    std::filesystem::path destination;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return manifest.has_value() && errors.empty();
    }
};

[[nodiscard]] ValidationReport validate_map_package_manifest(
    const MapPackageManifest&);
[[nodiscard]] std::string serialize_map_package_manifest(
    const MapPackageManifest&);
[[nodiscard]] MapPackageManifestResult parse_map_package_manifest(
    std::string_view);
[[nodiscard]] MapPackageManifestResult plan_map_package(
    const std::filesystem::path& project_root, const core::ResourceId& map_id,
    std::string_view minimum_runtime_version = core::version());
[[nodiscard]] MapPackagePublishResult publish_map_package(
    const std::filesystem::path& project_root, const core::ResourceId& map_id,
    const std::filesystem::path& destination,
    std::string_view minimum_runtime_version = core::version());
[[nodiscard]] bool runtime_can_load_map_package(
    const MapPackageManifest&,
    std::string_view runtime_version = core::version()) noexcept;
[[nodiscard]] ValidationReport validate_scene_package_manifest(
    const ScenePackageManifest&);
[[nodiscard]] std::string serialize_scene_package_manifest(
    const ScenePackageManifest&);
[[nodiscard]] ScenePackageManifestResult parse_scene_package_manifest(
    std::string_view);
[[nodiscard]] ScenePackageManifestResult plan_scene_package(
    const std::filesystem::path& project_root,
    const core::ResourceId& scene_id,
    std::string_view minimum_runtime_version = core::version());
[[nodiscard]] ScenePackagePublishResult publish_scene_package(
    const std::filesystem::path& project_root,
    const core::ResourceId& scene_id,
    const std::filesystem::path& destination,
    std::string_view minimum_runtime_version = core::version());
[[nodiscard]] bool runtime_can_load_scene_package(
    const ScenePackageManifest&,
    std::string_view runtime_version = core::version()) noexcept;

} // namespace fabric::project
