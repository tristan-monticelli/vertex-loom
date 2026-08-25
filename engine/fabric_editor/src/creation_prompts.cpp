#include "fabric/editor/creation_prompts.hpp"

#include "fabric/project/texture_asset.hpp"

#include <cmath>
#include <system_error>
#include <utility>

namespace fabric::editor {
namespace {

constexpr double maximum_working_size = 1'000'000.0;
constexpr double maximum_pixels_per_unit = 1'000'000.0;

bool blank(const std::string_view value) {
    return value.empty() || value.find_first_not_of(" \t\r\n") ==
                                std::string_view::npos;
}

void add_error(PromptValidation& validation, std::string field,
               std::string message) {
    validation.errors.push_back(
        PromptError{std::move(field), std::move(message)});
}

void validate_identity(PromptValidation& validation,
                       const std::string_view name,
                       const std::string_view id) {
    if (blank(name)) {
        add_error(validation, "name", "Name must not be empty.");
    } else if (name.size() > 255) {
        add_error(validation, "name", "Name must contain at most 255 characters.");
    }
    if (!core::ResourceId::is_valid(id)) {
        add_error(validation, "id",
                  "Use 1-128 lowercase letters, digits, dots, underscores or hyphens; start and end with a letter or digit.");
    }
}

bool identifier_conflicts(const std::filesystem::path& project_root,
                          const project::ProjectManifest& manifest,
                          const std::string_view id) {
    std::error_code error;
    const auto assets = project_root / manifest.directories.assets;
    const auto entities = project_root / manifest.directories.entities;
    const auto maps = project_root / manifest.directories.maps;
    const auto scenes = project_root / manifest.directories.scenes;
    for (const auto& root : {assets, entities, maps, scenes}) {
        if (!std::filesystem::is_directory(root, error)) {
            error.clear();
            continue;
        }
        for (std::filesystem::recursive_directory_iterator iterator{
                 root, std::filesystem::directory_options::skip_permission_denied,
                 error};
             !error && iterator != std::filesystem::recursive_directory_iterator{};
             iterator.increment(error)) {
            if (!iterator->is_regular_file(error)) {
                error.clear();
                continue;
            }
            const std::string filename = iterator->path().filename().string();
            if (filename.starts_with(std::string(id) + ".") &&
                iterator->path().extension() == ".json") {
                return true;
            }
        }
        error.clear();
    }
    return false;
}

} // namespace

std::optional<std::string_view> PromptValidation::error_for(
    const std::string_view field) const noexcept {
    for (const auto& error : errors) {
        if (error.field == field) {
            return error.message;
        }
    }
    return std::nullopt;
}

std::string_view label(const ProjectScalePreset preset) noexcept {
    switch (preset) {
    case ProjectScalePreset::standard: return "Standard (100 px/unit)";
    case ProjectScalePreset::compact: return "Compact (64 px/unit)";
    case ProjectScalePreset::high_detail: return "High detail (256 px/unit)";
    case ProjectScalePreset::custom: return "Custom";
    }
    return "Custom";
}

double preset_pixels_per_unit(const ProjectScalePreset preset) noexcept {
    switch (preset) {
    case ProjectScalePreset::standard: return 100.0;
    case ProjectScalePreset::compact: return 64.0;
    case ProjectScalePreset::high_detail: return 256.0;
    case ProjectScalePreset::custom: return project::default_pixels_per_unit;
    }
    return project::default_pixels_per_unit;
}

void CreateProjectPrompt::reset() noexcept {
    *this = CreateProjectPrompt{};
}

void CreateProjectPrompt::select_preset(
    const ProjectScalePreset selected) noexcept {
    preset = selected;
    if (selected != ProjectScalePreset::custom) {
        pixels_per_unit = preset_pixels_per_unit(selected);
    }
}

PromptValidation CreateProjectPrompt::validate() const {
    PromptValidation validation;
    validation.destination = destination / "project.json";
    validate_identity(validation, name, id);
    if (destination.empty()) {
        add_error(validation, "destination", "Choose a project destination.");
    } else {
        std::error_code error;
        if (std::filesystem::exists(destination, error)) {
            if (error || !std::filesystem::is_directory(destination, error)) {
                add_error(validation, "destination",
                          "Destination must be a directory.");
            } else if (!std::filesystem::is_empty(destination, error) || error) {
                add_error(validation, "destination",
                          "Destination must be empty.");
            }
        } else if (error) {
            add_error(validation, "destination",
                      "Destination cannot be inspected.");
        }
    }
    if (!std::isfinite(pixels_per_unit) || pixels_per_unit <= 0.0 ||
        pixels_per_unit > maximum_pixels_per_unit) {
        add_error(validation, "pixelsPerUnit",
                  "Pixels per unit must be finite and between 0 and 1,000,000.");
    }
    validation.summary = {
        "Document: ProjectManifest v2",
        "Project: " + (name.empty() ? std::string{"<unnamed>"} : name),
        "Resource ID: " + (id.empty() ? std::string{"<missing>"} : id),
        "Units: world units",
        "Scale preset: " + std::string(label(preset)),
        "Pixels per unit: " + std::to_string(pixels_per_unit),
        "Publish to: " + validation.destination.generic_string(),
    };
    return validation;
}

project::ProjectManifest CreateProjectPrompt::manifest() const {
    return project::ProjectManifest{
        .schema_version = project::current_schema_version,
        .id = {.value = id},
        .name = name,
        .pixels_per_unit = pixels_per_unit,
        .directories = {},
    };
}

std::string_view label(const ImportSourceKind kind) noexcept {
    switch (kind) {
    case ImportSourceKind::png_image: return "PNG image source";
    case ImportSourceKind::linked_svg: return "Linked SVG source";
    }
    return "Import source";
}

void ImportSourcePrompt::reset() noexcept {
    *this = ImportSourcePrompt{};
}

PromptValidation ImportSourcePrompt::validate(
    const ImportSourceKind kind, const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    validate_identity(validation, name, id);

    std::string expected_extension;
    std::filesystem::path directory;
    std::string document_suffix;
    switch (kind) {
    case ImportSourceKind::png_image:
        expected_extension = ".png";
        directory = manifest.directories.assets / "textures";
        document_suffix = ".texture.json";
        break;
    case ImportSourceKind::linked_svg:
        expected_extension = ".svg";
        directory = manifest.directories.assets / "vectors";
        document_suffix = ".vector.json";
        break;
    }
    validation.destination = project_root / directory / (id + document_suffix);

    std::error_code error;
    if (source.empty()) {
        add_error(validation, "source", "Choose a source file.");
    } else if (!std::filesystem::is_regular_file(source, error) || error) {
        add_error(validation, "source", "Source must be an existing regular file.");
    } else {
        std::string extension = source.extension().string();
        for (char& character : extension) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        const bool extension_matches = extension == expected_extension;
        if (!extension_matches) {
            add_error(validation, "source",
                      "The selected file extension does not match this import.");
        }
    }
    if (core::ResourceId::is_valid(id) &&
        identifier_conflicts(project_root, manifest, id)) {
        add_error(validation, "id",
                  "This resource ID is already registered in the project.");
    }
    validation.summary = {
        "Operation: Import",
        "Source type: " + std::string(label(kind)),
        "Source: " + (source.empty() ? std::string{"<missing>"}
                                      : source.generic_string()),
        "Resource ID: " + (id.empty() ? std::string{"<missing>"} : id),
        "Publish to: " + validation.destination.generic_string(),
    };
    return validation;
}

std::string_view label(const ArtworkOrigin origin) noexcept {
    switch (origin) {
    case ArtworkOrigin::center: return "Centered";
    case ArtworkOrigin::top_left: return "Top left";
    }
    return "Centered";
}

std::string_view label(const InitialShape shape) noexcept {
    switch (shape) {
    case InitialShape::rectangle: return "Rectangle";
    case InitialShape::ellipse: return "Ellipse";
    case InitialShape::empty: return "Empty artwork";
    }
    return "Empty artwork";
}

std::string_view label(const InitialFill fill) noexcept {
    switch (fill) {
    case InitialFill::color: return "Solid color";
    case InitialFill::image: return "Image in shape";
    case InitialFill::transparent: return "Transparent";
    }
    return "Transparent";
}

void CreateVectorArtworkPrompt::reset() noexcept {
    *this = CreateVectorArtworkPrompt{};
}

PromptValidation CreateVectorArtworkPrompt::validate(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    validation.destination = project_root / manifest.directories.assets /
                             "vectors" / (id + ".vector.json");
    validate_identity(validation, name, id);
    if (!std::isfinite(width) || width <= 0.0 ||
        width > maximum_working_size) {
        add_error(validation, "width",
                  "Width must be finite and between 0 and 1,000,000 world units.");
    }
    if (!std::isfinite(height) || height <= 0.0 ||
        height > maximum_working_size) {
        add_error(validation, "height",
                  "Height must be finite and between 0 and 1,000,000 world units.");
    }
    const auto valid_color_channel = [](const float channel) {
        return std::isfinite(channel) && channel >= 0.0F && channel <= 1.0F;
    };
    if (initial_fill == InitialFill::color &&
        (!valid_color_channel(initial_color.red) ||
         !valid_color_channel(initial_color.green) ||
         !valid_color_channel(initial_color.blue) ||
         !valid_color_channel(initial_color.alpha))) {
        add_error(validation, "initialFill",
                  "Initial color channels must be finite values from 0 to 1.");
    }
    if (initial_fill == InitialFill::image) {
        if (first_shape == InitialShape::empty) {
            add_error(validation, "initialFill",
                      "An image fill requires an initial shape.");
        }
        const core::ResourceId texture_id{.value = initial_image_id};
        if (!core::ResourceId::is_valid(initial_image_id)) {
            add_error(validation, "initialImage",
                      "Choose a valid imported texture resource ID.");
        } else {
            const auto loaded = project::load_texture_asset(
                project_root, manifest,
                project::texture_document_path(manifest, texture_id));
            if (!loaded.ok()) {
                add_error(validation, "initialImage",
                          "The texture resource is missing or invalid.");
            }
        }
        const auto finite = [](const float value) {
            return std::isfinite(value);
        };
        if (!finite(image_transform.position.x) ||
            !finite(image_transform.position.y) ||
            !finite(image_transform.rotation_degrees) ||
            !finite(image_transform.scale.x) ||
            !finite(image_transform.scale.y) ||
            image_transform.scale.x == 0.0F ||
            image_transform.scale.y == 0.0F ||
            !finite(image_transform.pivot.x) ||
            !finite(image_transform.pivot.y)) {
            add_error(validation, "imageTransform",
                      "Image transform values must be finite with non-zero scale.");
        }
        if (!std::isfinite(image_opacity) || image_opacity < 0.0 ||
            image_opacity > 1.0) {
            add_error(validation, "imageOpacity",
                      "Image opacity must be a finite value from 0 to 1.");
        }
    }
    if (core::ResourceId::is_valid(id) &&
        identifier_conflicts(project_root, manifest, id)) {
        add_error(validation, "id",
                  "This resource ID is already registered in the project.");
    }
    validation.summary = {
        "Document: native VectorAsset v2 creation intent",
        "Artwork: " + (name.empty() ? std::string{"<unnamed>"} : name),
        "Resource ID: " + (id.empty() ? std::string{"<missing>"} : id),
        "Working size: " + std::to_string(width) + " x " +
            std::to_string(height) + " world units",
        "Origin: " + std::string(label(origin)),
        "First shape: " + std::string(label(first_shape)),
        "Initial fill: " + std::string(label(initial_fill)),
        "Publish to: " + validation.destination.generic_string(),
    };
    if (initial_fill == InitialFill::image) {
        validation.summary.insert(
            validation.summary.end() - 1,
            {"Texture resource: " +
                 (initial_image_id.empty() ? std::string{"<missing>"}
                                           : initial_image_id),
             "Image fit: " + std::string(project::to_string(image_fit)),
             std::string{"Deforms with shape: "} +
                 (deform_image_with_shape ? "yes" : "no")});
    }
    return validation;
}

} // namespace fabric::editor
