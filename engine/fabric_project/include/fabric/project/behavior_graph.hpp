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

inline constexpr std::uint32_t current_behavior_graph_schema_version = 1;

enum class BehaviorValueType { signal, boolean, integer, scalar, text, vec2, resource };
enum class BehaviorPortDirection { input, output };

using BehaviorValue = std::variant<bool, std::int64_t, float, std::string,
                                   core::Vec2, ResourceReference>;

struct BehaviorParameterDefinition {
    std::string id;
    BehaviorValueType type{BehaviorValueType::boolean};
    BehaviorValue default_value{false};
    friend bool operator==(const BehaviorParameterDefinition&,
                           const BehaviorParameterDefinition&) = default;
};

struct BehaviorPortDefinition {
    std::string id;
    BehaviorPortDirection direction{BehaviorPortDirection::input};
    BehaviorValueType type{BehaviorValueType::signal};
    friend bool operator==(const BehaviorPortDefinition&,
                           const BehaviorPortDefinition&) = default;
};

struct BehaviorNodeProperty {
    std::string id;
    BehaviorValue value{false};
    friend bool operator==(const BehaviorNodeProperty&,
                           const BehaviorNodeProperty&) = default;
};

struct BehaviorNodeDefinition {
    std::string id;
    std::string type;
    std::vector<BehaviorPortDefinition> ports;
    std::vector<BehaviorNodeProperty> properties;
    friend bool operator==(const BehaviorNodeDefinition&,
                           const BehaviorNodeDefinition&) = default;
};

struct BehaviorConnection {
    std::string id;
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    friend bool operator==(const BehaviorConnection&,
                           const BehaviorConnection&) = default;
};

struct BehaviorGraph {
    DocumentHeader document{
        .schema_version = current_behavior_graph_schema_version,
        .type = "behavior",
    };
    std::vector<BehaviorParameterDefinition> parameters;
    std::vector<BehaviorNodeDefinition> nodes;
    std::vector<BehaviorConnection> connections;
    friend bool operator==(const BehaviorGraph&, const BehaviorGraph&) = default;
};

struct BehaviorGraphResult {
    std::optional<BehaviorGraph> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept { return asset.has_value() && errors.empty(); }
};

[[nodiscard]] std::string_view to_string(BehaviorValueType) noexcept;
[[nodiscard]] bool behavior_value_matches(BehaviorValueType,
                                           const BehaviorValue&) noexcept;
[[nodiscard]] bool is_behavior_node_type(std::string_view) noexcept;
[[nodiscard]] std::filesystem::path behavior_graph_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_behavior_graph(
    const ProjectManifest&, const BehaviorGraph&);
[[nodiscard]] std::vector<ResourceReference> behavior_graph_resource_references(
    const BehaviorGraph&);
[[nodiscard]] std::string serialize_behavior_graph(const BehaviorGraph&);
[[nodiscard]] BehaviorGraphResult parse_behavior_graph(
    const ProjectManifest&, std::string_view);
[[nodiscard]] BehaviorGraphResult load_behavior_graph(
    const std::filesystem::path&, const ProjectManifest&,
    const std::filesystem::path&);
[[nodiscard]] BehaviorGraphResult publish_behavior_graph(
    const std::filesystem::path&, const ProjectManifest&, const BehaviorGraph&);

} // namespace fabric::project
