#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/core/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_entity_schema_version = 1;

enum class EntityDrawableKind { none, vector, texture };
[[nodiscard]] std::string_view to_string(EntityDrawableKind) noexcept;

struct EntityDrawable {
    EntityDrawableKind kind{EntityDrawableKind::none};
    std::optional<ResourceReference> resource;
    std::optional<ResourceReference> material;
    friend bool operator==(const EntityDrawable&, const EntityDrawable&) = default;
};

struct EntityNode {
    std::string id;
    std::string name;
    std::optional<std::string> parent;
    core::Transform transform;
    float z_order{};
    EntityDrawable drawable;
    friend bool operator==(const EntityNode&, const EntityNode&) = default;
};

struct EntityDefinition {
    DocumentHeader document{
        .schema_version = current_entity_schema_version,
        .type = "entity",
    };
    std::vector<EntityNode> nodes;
    friend bool operator==(const EntityDefinition&, const EntityDefinition&) = default;
};

struct EntityResult {
    std::optional<EntityDefinition> entity;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return entity.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path entity_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_entity(
    const ProjectManifest&, const EntityDefinition&);
[[nodiscard]] std::vector<ResourceReference> entity_resource_references(
    const EntityDefinition&);
[[nodiscard]] std::string serialize_entity(const EntityDefinition&);
[[nodiscard]] EntityResult parse_entity(const ProjectManifest&, std::string_view);
[[nodiscard]] EntityResult load_entity(const std::filesystem::path&,
                                        const ProjectManifest&,
                                        const std::filesystem::path&);
[[nodiscard]] EntityResult publish_entity(const std::filesystem::path&,
                                           const ProjectManifest&,
                                           const EntityDefinition&);

} // namespace fabric::project
