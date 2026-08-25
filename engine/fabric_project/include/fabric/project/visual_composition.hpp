#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/visual_component.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_visual_composition_schema_version = 1;

enum class VisualLayerKind { raster, vector, component, textured_path };

[[nodiscard]] std::string_view to_string(VisualLayerKind kind) noexcept;

struct VisualCompositionLayer {
    std::string id;
    std::string name;
    VisualLayerKind kind{VisualLayerKind::raster};
    ResourceReference resource;
    core::Vec2 anchor{0.5F, 0.5F};
    core::Transform transform;
    bool visible{true};
    float opacity{1.0F};
    float z_order{};
    std::optional<RasterView> raster_view;
    std::optional<VisualComponentInstance> component_instance;

    friend bool operator==(const VisualCompositionLayer&,
                           const VisualCompositionLayer&) = default;
};

struct VisualComposition {
    DocumentHeader document{
        .schema_version = current_visual_composition_schema_version,
        .type = "visualComposition",
    };
    core::Vec2 size{1.0F, 1.0F};
    std::vector<VisualCompositionLayer> layers;

    friend bool operator==(const VisualComposition&,
                           const VisualComposition&) = default;
};

struct VisualCompositionResult {
    std::optional<VisualComposition> asset;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path visual_composition_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_visual_composition(
    const ProjectManifest&, const VisualComposition&);
[[nodiscard]] std::vector<ResourceReference>
visual_composition_resource_references(const VisualComposition&);
[[nodiscard]] std::string serialize_visual_composition(
    const VisualComposition&);
[[nodiscard]] VisualCompositionResult parse_visual_composition(
    const ProjectManifest&, std::string_view);
[[nodiscard]] VisualCompositionResult load_visual_composition(
    const std::filesystem::path&, const ProjectManifest&,
    const std::filesystem::path&);
[[nodiscard]] VisualCompositionResult publish_visual_composition(
    const std::filesystem::path&, const ProjectManifest&,
    const VisualComposition&);

} // namespace fabric::project
