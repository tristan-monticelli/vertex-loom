#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_entity_transformation_schema_version = 1;

enum class TransferMode { preserve, reset, mapping, error };
enum class TransferDomain { property, behavior_parameter, animation };

struct TransferMapping {
    TransferDomain domain{TransferDomain::property};
    std::string source;
    std::string target;
    friend bool operator==(const TransferMapping&, const TransferMapping&) = default;
};

struct EntityTransferPolicy {
    TransferMode world_transform{TransferMode::preserve};
    TransferMode instance_id{TransferMode::preserve};
    TransferMode layer_and_z{TransferMode::preserve};
    TransferMode physics{TransferMode::reset};
    TransferMode properties{TransferMode::preserve};
    TransferMode behavior_parameters{TransferMode::preserve};
    TransferMode animation{TransferMode::reset};
    TransferMode timers_and_cooldowns{TransferMode::reset};
    TransferMode camera_follow{TransferMode::preserve};
    TransferMode incompatible_values{TransferMode::error};
    std::vector<TransferMapping> mappings;
    friend bool operator==(const EntityTransferPolicy&,
                           const EntityTransferPolicy&) = default;
};

struct EntityTransformation {
    DocumentHeader document{
        .schema_version = current_entity_transformation_schema_version,
        .type = "transformation",
    };
    ResourceReference source_entity;
    ResourceReference destination_entity;
    EntityTransferPolicy policy;
    friend bool operator==(const EntityTransformation&,
                           const EntityTransformation&) = default;
};

struct EntityTransformationResult {
    std::optional<EntityTransformation> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return asset.has_value() && errors.empty(); }
};

[[nodiscard]] std::string_view to_string(TransferMode) noexcept;
[[nodiscard]] std::string_view to_string(TransferDomain) noexcept;
[[nodiscard]] std::filesystem::path entity_transformation_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_entity_transformation(
    const ProjectManifest&, const EntityTransformation&);
[[nodiscard]] std::vector<ResourceReference>
entity_transformation_resource_references(const EntityTransformation&);
[[nodiscard]] std::string serialize_entity_transformation(
    const EntityTransformation&);
[[nodiscard]] EntityTransformationResult parse_entity_transformation(
    const ProjectManifest&, std::string_view);
[[nodiscard]] EntityTransformationResult load_entity_transformation(
    const std::filesystem::path&, const ProjectManifest&,
    const std::filesystem::path&);
[[nodiscard]] EntityTransformationResult publish_entity_transformation(
    const std::filesystem::path&, const ProjectManifest&,
    const EntityTransformation&);

} // namespace fabric::project
