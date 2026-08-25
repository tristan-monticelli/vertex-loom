#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/property.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_visual_component_schema_version = 1;

enum class VisualParameterType {
    scalar,
    angle,
    integer,
    boolean,
    text,
    vec2,
    color,
    resource,
};

[[nodiscard]] std::string_view to_string(VisualParameterType type) noexcept;

using VisualParameterValue = std::variant<float, std::int64_t, bool,
                                           std::string, core::Vec2,
                                           core::Color, ResourceReference>;

struct VisualParameterOverride {
    std::string parameter_id;
    VisualParameterValue value{0.0F};

    friend bool operator==(const VisualParameterOverride&,
                           const VisualParameterOverride&) = default;
};

struct VisualComponentInstance {
    std::optional<std::string> variant_id;
    std::optional<std::string> anchor_id;
    std::vector<VisualParameterOverride> overrides;

    friend bool operator==(const VisualComponentInstance&,
                           const VisualComponentInstance&) = default;
};

struct VisualComponentAnchor {
    std::string id;
    std::string name;
    core::Vec2 position;

    friend bool operator==(const VisualComponentAnchor&,
                           const VisualComponentAnchor&) = default;
};

struct VisualComponentParameter {
    std::string id;
    std::string name;
    VisualParameterType type{VisualParameterType::scalar};
    VisualParameterValue default_value{0.0F};
    PropertyBinding target;
    bool animatable{};

    friend bool operator==(const VisualComponentParameter&,
                           const VisualComponentParameter&) = default;
};

struct VisualComponentVariant {
    std::string id;
    std::string name;
    std::vector<VisualParameterOverride> overrides;

    friend bool operator==(const VisualComponentVariant&,
                           const VisualComponentVariant&) = default;
};

struct VisualComponent {
    DocumentHeader document{
        .schema_version = current_visual_component_schema_version,
        .type = "visualComponent",
    };
    ResourceReference composition{{}, "visualComposition"};
    core::Rect bounds{{-0.5F, -0.5F}, {1.0F, 1.0F}};
    std::vector<VisualComponentAnchor> anchors;
    std::vector<VisualComponentParameter> parameters;
    std::vector<VisualComponentVariant> variants;

    friend bool operator==(const VisualComponent&,
                           const VisualComponent&) = default;
};

struct VisualComponentResult {
    std::optional<VisualComponent> asset;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

struct ResolvedVisualParameter {
    std::string id;
    VisualParameterValue value{0.0F};
    PropertyBinding target;
    bool animatable{};

    friend bool operator==(const ResolvedVisualParameter&,
                           const ResolvedVisualParameter&) = default;
};

struct VisualComponentInstanceResult {
    std::vector<ResolvedVisualParameter> parameters;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] bool visual_parameter_value_matches(
    VisualParameterType, const VisualParameterValue&) noexcept;
[[nodiscard]] std::filesystem::path visual_component_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_visual_component(
    const ProjectManifest&, const VisualComponent&);
[[nodiscard]] std::vector<ResourceReference>
visual_component_resource_references(const VisualComponent&);
[[nodiscard]] std::vector<PropertyDescriptor>
visual_component_property_descriptors(const VisualComponent&);
[[nodiscard]] VisualComponentInstanceResult resolve_visual_component_instance(
    const VisualComponent&, const VisualComponentInstance&);
[[nodiscard]] std::string serialize_visual_component(const VisualComponent&);
[[nodiscard]] VisualComponentResult parse_visual_component(
    const ProjectManifest&, std::string_view);
[[nodiscard]] VisualComponentResult load_visual_component(
    const std::filesystem::path&, const ProjectManifest&,
    const std::filesystem::path&);
[[nodiscard]] VisualComponentResult publish_visual_component(
    const std::filesystem::path&, const ProjectManifest&,
    const VisualComponent&);

} // namespace fabric::project
