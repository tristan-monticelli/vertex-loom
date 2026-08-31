#pragma once

#include "fabric/project/document.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/shader_profile.hpp"
#include "fabric/core/types.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_material_schema_version = 2;

enum class MaterialBlendMode { normal, additive, multiply, screen };

[[nodiscard]] std::string_view to_string(MaterialBlendMode mode) noexcept;

struct MaterialDefinition {
    DocumentHeader document{
        .schema_version = current_material_schema_version,
        .type = "material",
    };
    core::Color color{1.0F, 1.0F, 1.0F, 1.0F};
    float opacity{1.0F};
    MaterialBlendMode blend{MaterialBlendMode::normal};
    std::optional<ResourceReference> texture;
    std::optional<ResourceReference> vector_pattern;
    std::optional<ShaderSurfaceSettings> shader;
    core::Transform uv_transform;

    friend bool operator==(const MaterialDefinition&, const MaterialDefinition&) = default;
};

struct MaterialResult {
    std::optional<MaterialDefinition> asset;
    std::vector<Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return asset.has_value() && errors.empty();
    }
};

[[nodiscard]] std::filesystem::path material_document_path(
    const ProjectManifest&, const core::ResourceId&);
[[nodiscard]] ValidationReport validate_material(
    const ProjectManifest&, const MaterialDefinition&);
[[nodiscard]] std::vector<ResourceReference> material_resource_references(
    const MaterialDefinition&);
[[nodiscard]] std::string serialize_material(const MaterialDefinition&);
[[nodiscard]] MaterialResult parse_material(const ProjectManifest&, std::string_view);
[[nodiscard]] MaterialResult load_material(const std::filesystem::path&,
                                            const ProjectManifest&,
                                            const std::filesystem::path&);
[[nodiscard]] MaterialResult publish_material(const std::filesystem::path&,
                                               const ProjectManifest&,
                                               const MaterialDefinition&);

} // namespace fabric::project
