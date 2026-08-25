#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_map_schema_version = 1;
inline constexpr float map_chunk_size = 64.0F;

enum class MapLayerKind { visual, tiles, instances, collision, triggers, gameplay };
[[nodiscard]] std::string_view to_string(MapLayerKind) noexcept;

using MapPropertyValue = std::variant<bool, std::int64_t, float, std::string,
                                       core::Vec2, ResourceReference>;

struct MapProperty {
    std::string id;
    MapPropertyValue value{false};
    friend bool operator==(const MapProperty&, const MapProperty&) = default;
};

struct LayerDefinition {
    std::string id;
    std::string name;
    MapLayerKind kind{MapLayerKind::visual};
    bool visible{true};
    bool locked{};
    float depth{};
    friend bool operator==(const LayerDefinition&, const LayerDefinition&) = default;
};

struct PrefabDefinition {
    std::string id;
    ResourceReference entity;
    std::vector<MapProperty> overrides;
    friend bool operator==(const PrefabDefinition&, const PrefabDefinition&) = default;
};

struct MapInstance {
    std::string id;
    std::optional<ResourceReference> entity;
    std::optional<ResourceReference> prefab;
    std::string layer_id;
    core::Transform transform;
    std::int32_t chunk_x{};
    std::int32_t chunk_y{};
    std::vector<MapProperty> properties;
    friend bool operator==(const MapInstance&, const MapInstance&) = default;
};

enum class CollisionShapeKind { circle, capsule, polygon, chain };

struct CollisionShape {
    CollisionShapeKind kind{CollisionShapeKind::polygon};
    std::string layer_id;
    bool sensor{};
    core::Vec2 center;
    float radius{};
    float length{};
    std::vector<core::Vec2> points;
    friend bool operator==(const CollisionShape&, const CollisionShape&) = default;
};

struct TriggerDefinition {
    std::string id;
    std::string layer_id;
    std::size_t collision_index{};
    core::ResourceId event_id;
    std::vector<MapProperty> properties;
    friend bool operator==(const TriggerDefinition&, const TriggerDefinition&) = default;
};

struct MapEventDefinition {
    core::ResourceId id;
    std::vector<MapProperty> payload;
    friend bool operator==(const MapEventDefinition&, const MapEventDefinition&) = default;
};

struct MapDocument {
    DocumentHeader document{
        .schema_version = current_map_schema_version,
        .type = "map",
    };
    float chunk_size{map_chunk_size};
    std::vector<LayerDefinition> layers;
    std::vector<PrefabDefinition> prefabs;
    std::vector<MapInstance> instances;
    std::vector<CollisionShape> collisions;
    std::vector<TriggerDefinition> triggers;
    std::vector<MapEventDefinition> events;
    friend bool operator==(const MapDocument&, const MapDocument&) = default;
};

struct MapResult {
    std::optional<MapDocument> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return asset.has_value() && errors.empty(); }
};

[[nodiscard]] std::filesystem::path map_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_map(const ProjectManifest&, const MapDocument&);
[[nodiscard]] std::vector<ResourceReference> map_resource_references(const MapDocument&);
[[nodiscard]] std::string serialize_map(const MapDocument&);
[[nodiscard]] MapResult parse_map(const ProjectManifest&, std::string_view);
[[nodiscard]] MapResult load_map(const std::filesystem::path&, const ProjectManifest&,
                                 const std::filesystem::path&);
[[nodiscard]] MapResult publish_map(const std::filesystem::path&, const ProjectManifest&,
                                    const MapDocument&);

} // namespace fabric::project
