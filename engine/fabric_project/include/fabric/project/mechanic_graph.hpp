#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_mechanic_graph_schema_version = 1;

enum class MechanicValueType {
    boolean,
    integer,
    scalar,
    text,
    vec2,
    resource,
    body_handle,
    pivot_handle,
    joint_handle,
};
enum class MechanicPortDirection { input, output };
enum class MechanicNodeKind {
    body,
    pivot,
    joint,
    motor,
    sensor,
    constraint,
    event,
};

using MechanicValue = std::variant<bool, std::int64_t, float, std::string,
                                   core::Vec2, ResourceReference>;

struct MechanicParameterDefinition {
    std::string id;
    std::string name;
    MechanicValueType type{MechanicValueType::scalar};
    MechanicValue default_value{0.0F};
    std::string target_node;
    std::string target_property;
    friend bool operator==(const MechanicParameterDefinition&,
                           const MechanicParameterDefinition&) = default;
};

struct MechanicNodeProperty {
    std::string id;
    MechanicValue value{0.0F};
    friend bool operator==(const MechanicNodeProperty&,
                           const MechanicNodeProperty&) = default;
};

struct MechanicPortDefinition {
    std::string id;
    std::string name;
    MechanicPortDirection direction{MechanicPortDirection::input};
    MechanicValueType type{MechanicValueType::scalar};
    friend bool operator==(const MechanicPortDefinition&,
                           const MechanicPortDefinition&) = default;
};

struct MechanicNodeDefinition {
    std::string id;
    std::string type;
    std::vector<MechanicPortDefinition> ports;
    std::vector<MechanicNodeProperty> properties;
    friend bool operator==(const MechanicNodeDefinition&,
                           const MechanicNodeDefinition&) = default;
};

struct MechanicConnection {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    friend bool operator==(const MechanicConnection&,
                           const MechanicConnection&) = default;
};

struct MechanicNodePortSchema {
    std::string_view id;
    MechanicPortDirection direction;
    MechanicValueType type;
};

struct MechanicNodePropertySchema {
    std::string_view id;
    MechanicValueType type;
    bool required{true};
};

struct MechanicNodeSchema {
    MechanicNodeKind kind;
    std::string_view type;
    std::vector<MechanicNodePortSchema> ports;
    std::vector<MechanicNodePropertySchema> properties;
};

struct MechanicGraph {
    DocumentHeader document{
        .schema_version = current_mechanic_graph_schema_version,
        .type = "mechanic",
    };
    std::vector<MechanicParameterDefinition> parameters;
    std::vector<MechanicNodeDefinition> nodes;
    std::vector<MechanicConnection> connections;
    friend bool operator==(const MechanicGraph&, const MechanicGraph&) = default;
};

struct MechanicGraphResult {
    std::optional<MechanicGraph> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] bool mechanic_value_matches(
    MechanicValueType, const MechanicValue&) noexcept;
[[nodiscard]] std::string_view to_string(MechanicValueType) noexcept;
[[nodiscard]] std::string_view to_string(MechanicNodeKind) noexcept;
[[nodiscard]] std::optional<MechanicNodeKind> mechanic_node_kind(
    std::string_view) noexcept;
[[nodiscard]] const MechanicNodeSchema& mechanic_node_schema(
    MechanicNodeKind);
[[nodiscard]] std::filesystem::path mechanic_graph_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_mechanic_graph(
    const ProjectManifest&, const MechanicGraph&);
[[nodiscard]] std::vector<ResourceReference>
mechanic_graph_resource_references(const MechanicGraph&);
[[nodiscard]] std::string serialize_mechanic_graph(const MechanicGraph&);
[[nodiscard]] MechanicGraphResult parse_mechanic_graph(
    const ProjectManifest&, std::string_view);
[[nodiscard]] MechanicGraphResult load_mechanic_graph(
    const std::filesystem::path&, const ProjectManifest&,
    const std::filesystem::path&);
[[nodiscard]] MechanicGraphResult publish_mechanic_graph(
    const std::filesystem::path&, const ProjectManifest&,
    const MechanicGraph&);

} // namespace fabric::project
