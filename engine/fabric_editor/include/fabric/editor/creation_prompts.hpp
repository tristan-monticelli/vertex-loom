#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/core/types.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/vector_asset.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::editor {

struct PromptError {
    std::string field;
    std::string message;

    friend bool operator==(const PromptError&, const PromptError&) = default;
};

struct PromptValidation {
    std::vector<PromptError> errors;
    std::filesystem::path destination;
    std::vector<std::string> summary;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    [[nodiscard]] std::optional<std::string_view> error_for(
        std::string_view field) const noexcept;
};

enum class ProjectScalePreset {
    standard,
    compact,
    high_detail,
    custom,
};

[[nodiscard]] std::string_view label(ProjectScalePreset preset) noexcept;
[[nodiscard]] double preset_pixels_per_unit(ProjectScalePreset preset) noexcept;

struct CreateProjectPrompt {
    std::filesystem::path parent_directory;
    std::string name;
    ProjectScalePreset preset{ProjectScalePreset::standard};
    double pixels_per_unit{project::default_pixels_per_unit};

    void reset() noexcept;
    void select_preset(ProjectScalePreset selected) noexcept;
    [[nodiscard]] PromptValidation validate() const;
    [[nodiscard]] std::filesystem::path project_root() const;
    [[nodiscard]] core::ResourceId resource_id() const;
    [[nodiscard]] project::ProjectManifest manifest() const;
};

enum class ImportSourceKind {
    png_image,
    linked_svg,
};

[[nodiscard]] std::string_view label(ImportSourceKind kind) noexcept;

struct ImportSourcePrompt {
    std::filesystem::path source;
    std::string name;

    void reset() noexcept;
    [[nodiscard]] PromptValidation validate(
        ImportSourceKind kind,
        const std::filesystem::path& project_root,
        const project::ProjectManifest& manifest) const;
    [[nodiscard]] core::ResourceId resource_id(
        const std::filesystem::path& project_root,
        const project::ProjectManifest& manifest) const;
};

enum class ArtworkOrigin {
    center,
    top_left,
};

enum class InitialShape {
    rectangle,
    ellipse,
    empty,
};

enum class InitialFill {
    color,
    image,
    transparent,
};

[[nodiscard]] std::string_view label(ArtworkOrigin origin) noexcept;
[[nodiscard]] std::string_view label(InitialShape shape) noexcept;
[[nodiscard]] std::string_view label(InitialFill fill) noexcept;

struct CreateVectorArtworkPrompt {
    std::string name;
    double width{10.0};
    double height{10.0};
    ArtworkOrigin origin{ArtworkOrigin::center};
    InitialShape first_shape{InitialShape::rectangle};
    InitialFill initial_fill{InitialFill::color};
    core::Color initial_color{};
    std::string initial_image_id;
    project::VectorImageFit image_fit{project::VectorImageFit::cover};
    core::Transform image_transform;
    double image_opacity{1.0};
    bool deform_image_with_shape{true};

    void reset() noexcept;
    [[nodiscard]] PromptValidation validate(
        const std::filesystem::path& project_root,
        const project::ProjectManifest& manifest) const;
    [[nodiscard]] core::ResourceId resource_id(
        const std::filesystem::path& project_root,
        const project::ProjectManifest& manifest) const;
};

[[nodiscard]] core::ResourceId generated_resource_id(
    std::string_view visible_name,
    std::string_view fallback = "resource");

} // namespace fabric::editor
