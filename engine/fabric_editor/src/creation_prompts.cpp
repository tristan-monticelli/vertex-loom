#include "fabric/editor/creation_prompts.hpp"

#include "fabric/project/texture_asset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <system_error>
#include <utility>

namespace fabric::editor {
namespace {

constexpr double maximum_working_size = 1'000'000.0;
constexpr double maximum_pixels_per_unit = 1'000'000.0;
constexpr std::size_t maximum_resource_id_length = 128;

bool blank(const std::string_view value) {
    return value.empty() || value.find_first_not_of(" \t\r\n") ==
                                std::string_view::npos;
}

void add_error(PromptValidation& validation, std::string field,
               std::string message) {
    validation.errors.push_back(
        PromptError{std::move(field), std::move(message)});
}

void validate_name(PromptValidation& validation,
                   const std::string_view name) {
    if (blank(name)) {
        add_error(validation, "name", "Name must not be empty.");
    } else if (name.size() > 255) {
        add_error(validation, "name", "Name must contain at most 255 characters.");
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

bool resource_document_exists(const std::filesystem::path& project_root,
                              const project::ProjectManifest& manifest,
                              const std::string_view id,
                              const std::filesystem::path& directory,
                              const std::string_view suffix) {
    if (id.empty()) return false;
    std::error_code error;
    const auto root = project_root / manifest.directories.assets;
    const auto candidate = root / directory / (std::string{id} + std::string{suffix});
    return std::filesystem::is_regular_file(candidate, error);
}

core::ResourceId available_resource_id(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const core::ResourceId& preferred) {
    if (!identifier_conflicts(project_root, manifest, preferred.value)) {
        return preferred;
    }
    for (std::uint32_t sequence = 2;; ++sequence) {
        const std::string suffix = "-" + std::to_string(sequence);
        std::string candidate = preferred.value.substr(
            0, maximum_resource_id_length - suffix.size());
        while (!candidate.empty() && candidate.back() == '-') {
            candidate.pop_back();
        }
        candidate += suffix;
        if (!identifier_conflicts(project_root, manifest, candidate)) {
            return {.value = std::move(candidate)};
        }
    }
}

} // namespace

core::ResourceId generated_resource_id(const std::string_view visible_name,
                                       const std::string_view fallback) {
    std::string folded;
    folded.reserve(visible_name.size());
    for (std::size_t index = 0; index < visible_name.size(); ++index) {
        const auto character = static_cast<unsigned char>(visible_name[index]);
        if (character < 0x80U) {
            folded.push_back(static_cast<char>(character));
            continue;
        }
        if (character == 0xC3U && index + 1 < visible_name.size()) {
            const auto continuation =
                static_cast<unsigned char>(visible_name[++index]);
            const char replacement =
                continuation >= 0x80U && continuation <= 0x85U ? 'a'
                : continuation == 0x87U                         ? 'c'
                : continuation >= 0x88U && continuation <= 0x8BU ? 'e'
                : continuation >= 0x8CU && continuation <= 0x8FU ? 'i'
                : continuation >= 0x92U && continuation <= 0x96U ? 'o'
                : continuation >= 0x99U && continuation <= 0x9CU ? 'u'
                : continuation == 0x9FU                         ? 's'
                : continuation >= 0xA0U && continuation <= 0xA5U ? 'a'
                : continuation == 0xA7U                         ? 'c'
                : continuation >= 0xA8U && continuation <= 0xABU ? 'e'
                : continuation >= 0xACU && continuation <= 0xAFU ? 'i'
                : continuation >= 0xB2U && continuation <= 0xB6U ? 'o'
                : continuation >= 0xB9U && continuation <= 0xBCU ? 'u'
                : continuation == 0xBFU                         ? 'y'
                                                                 : ' ';
            folded.push_back(replacement);
            continue;
        }
        if (character == 0xC5U && index + 1 < visible_name.size()) {
            const auto continuation =
                static_cast<unsigned char>(visible_name[++index]);
            if (continuation == 0x92U || continuation == 0x93U) {
                folded += "oe";
            } else {
                folded.push_back(' ');
            }
            continue;
        }
        folded.push_back(' ');
        while (index + 1 < visible_name.size() &&
               (static_cast<unsigned char>(visible_name[index + 1]) & 0xC0U) ==
                   0x80U) {
            ++index;
        }
    }
    std::string slug;
    slug.reserve(std::min(folded.size(), maximum_resource_id_length));
    bool separator_pending = false;
    for (const char raw_character : folded) {
        const auto character = static_cast<unsigned char>(raw_character);
        const bool uppercase = character >= 'A' && character <= 'Z';
        const bool lowercase = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (uppercase || lowercase || digit) {
            if (separator_pending && !slug.empty() &&
                slug.size() < maximum_resource_id_length) {
                slug.push_back('-');
            }
            separator_pending = false;
            if (slug.size() < maximum_resource_id_length) {
                slug.push_back(uppercase
                                   ? static_cast<char>(character - 'A' + 'a')
                                   : static_cast<char>(character));
            }
        } else {
            separator_pending = !slug.empty();
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = std::string(fallback);
    }
    return {.value = std::move(slug)};
}

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
    const auto root = project_root();
    validation.destination = root / "project.json";
    validate_name(validation, name);
    if (parent_directory.empty()) {
        add_error(validation, "destination", "Choose a parent folder.");
    } else {
        std::error_code error;
        if (!std::filesystem::is_directory(parent_directory, error) || error) {
            add_error(validation, "destination",
                      "Parent folder must be an existing directory.");
        } else if (std::filesystem::exists(root, error)) {
            if (error || !std::filesystem::is_directory(root, error)) {
                add_error(validation, "destination",
                          "The calculated project path must be a directory.");
            } else if (!std::filesystem::is_empty(root, error) || error) {
                add_error(validation, "destination",
                          "A non-empty project folder already uses this name.");
            }
        } else if (error) {
            add_error(validation, "destination",
                      "The calculated project path cannot be inspected.");
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
        "Internal ID: " + resource_id().value + " (generated)",
        "Units: world units",
        "Scale preset: " + std::string(label(preset)),
        "Pixels per unit: " + std::to_string(pixels_per_unit),
        "Publish to: " + validation.destination.generic_string(),
    };
    return validation;
}

std::filesystem::path CreateProjectPrompt::project_root() const {
    return parent_directory / resource_id().value;
}

core::ResourceId CreateProjectPrompt::resource_id() const {
    return generated_resource_id(name, "project");
}

project::ProjectManifest CreateProjectPrompt::manifest() const {
    return project::ProjectManifest{
        .schema_version = project::current_schema_version,
        .id = resource_id(),
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
    validate_name(validation, name);
    const auto id = resource_id(project_root, manifest);

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
    validation.destination = project_root / directory /
                             (id.value + document_suffix);

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
    validation.summary = {
        "Operation: Import",
        "Source type: " + std::string(label(kind)),
        "Source: " + (source.empty() ? std::string{"<missing>"}
                                      : source.generic_string()),
        "Internal ID: " + id.value + " (generated)",
        "Publish to: " + validation.destination.generic_string(),
    };
    return validation;
}

core::ResourceId ImportSourcePrompt::resource_id(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    const auto visible_name = blank(name) ? source.stem().string() : name;
    return available_resource_id(
        project_root, manifest, generated_resource_id(visible_name));
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

void CreateMaterialPrompt::reset() noexcept {
    *this = CreateMaterialPrompt{};
}

PromptValidation CreateMaterialPrompt::validate(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    validate_name(validation, name);
    const auto id = resource_id(project_root, manifest);
    if (!core::ResourceId::is_valid(id.value)) {
        add_error(validation, "id", "Generated resource id is invalid.");
    }
    for (const auto [field, value] : {
             std::pair{"red", color.red}, std::pair{"green", color.green},
             std::pair{"blue", color.blue}, std::pair{"alpha", color.alpha}}) {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
            add_error(validation, "color." + std::string(field),
                      "Color channels must be finite in [0,1].");
        }
    }
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        add_error(validation, "opacity", "Opacity must be finite in [0,1].");
    }
    if (!texture_id.empty() && !core::ResourceId::is_valid(texture_id)) {
        add_error(validation, "texture", "Texture id must be a valid resource id.");
    } else if (!texture_id.empty() &&
               !resource_document_exists(project_root, manifest, texture_id,
                                          "textures",
                                          ".texture.json")) {
        add_error(validation, "texture", "Texture id is not registered in the project.");
    }
    if (!vector_pattern_id.empty() &&
        !core::ResourceId::is_valid(vector_pattern_id)) {
        add_error(validation, "vectorPattern",
                  "Vector pattern id must be a valid resource id.");
    } else if (!vector_pattern_id.empty() &&
               !resource_document_exists(project_root, manifest,
                                         vector_pattern_id, "vectors",
                                         ".vector.json")) {
        add_error(validation, "vectorPattern",
                  "Vector pattern id is not registered in the project.");
    }
    const auto destination = project::material_document_path(manifest, id);
    validation.destination = project_root / destination;
    validation.summary = {
        "Create MaterialDefinition v1: " + name,
        "Id: " + id.value,
        "Destination: " + validation.destination.generic_string(),
        "Blend: " + std::string(project::to_string(blend)),
    };
    return validation;
}

core::ResourceId CreateMaterialPrompt::resource_id(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    return available_resource_id(project_root, manifest,
                                 generated_resource_id(name, "material"));
}

void CreateEntityPrompt::reset() noexcept {
    *this = CreateEntityPrompt{};
}

PromptValidation CreateEntityPrompt::validate(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    validate_name(validation, name);
    validate_name(validation, node_name);
    const auto id = resource_id_for_document(project_root, manifest);
    if (!core::ResourceId::is_valid(id.value)) {
        add_error(validation, "id", "Generated resource id is invalid.");
    }
    const auto finite = std::isfinite(z_order) &&
        std::isfinite(transform.position.x) &&
        std::isfinite(transform.position.y) &&
        std::isfinite(transform.rotation_degrees) &&
        std::isfinite(transform.scale.x) && std::isfinite(transform.scale.y) &&
        std::isfinite(transform.pivot.x) && std::isfinite(transform.pivot.y);
    if (!finite) {
        add_error(validation, "transform", "Transform and z-order must be finite.");
    }
    const auto validate_resource = [&](const std::string& reference,
                                       const std::filesystem::path& directory,
                                       const std::string_view suffix,
                                       const std::string_view field) {
        if (!core::ResourceId::is_valid(reference)) {
            add_error(validation, std::string{field},
                      "Drawable resource id must be valid.");
        } else if (!resource_document_exists(project_root, manifest, reference,
                                              directory, suffix)) {
            add_error(validation, std::string{field},
                      "Drawable resource is not registered in the project.");
        }
    };
    if (drawable == project::EntityDrawableKind::none) {
        if (!resource_id.empty()) {
            add_error(validation, "resource", "None drawable cannot reference a resource.");
        }
    } else if (drawable == project::EntityDrawableKind::texture) {
        validate_resource(resource_id, "textures", ".texture.json", "resource");
    } else if (drawable == project::EntityDrawableKind::vector) {
        validate_resource(resource_id, "vectors", ".vector.json", "resource");
    } else if (drawable == project::EntityDrawableKind::visual_component) {
        validate_resource(resource_id, "components", ".component.json",
                          "resource");
    }
    if (drawable == project::EntityDrawableKind::visual_component &&
        !material_id.empty())
        add_error(validation, "material",
                  "Visual components own their composed materials.");
    if (!material_id.empty()) {
        if (!core::ResourceId::is_valid(material_id)) {
            add_error(validation, "material", "Material id must be valid.");
        } else if (!resource_document_exists(project_root, manifest, material_id,
                                              "materials", ".material.json")) {
            add_error(validation, "material",
                      "Material is not registered in the project.");
        }
    }
    validation.destination = project_root /
        project::entity_document_path(manifest, id);
    validation.summary = {
        "Create EntityDefinition v1: " + name,
        "Id: " + id.value,
        "Destination: " + validation.destination.generic_string(),
        "Root node: " + node_name,
    };
    return validation;
}

core::ResourceId CreateEntityPrompt::resource_id_for_document(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    return available_resource_id(project_root, manifest,
                                 generated_resource_id(name, "entity"));
}

void CreateAnimationPrompt::reset() noexcept {
    *this = CreateAnimationPrompt{};
}

void CreateInputPrompt::reset() noexcept {
    *this = CreateInputPrompt{};
}

PromptValidation CreateInputPrompt::validate(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    validate_name(validation, name);
    const auto id = resource_id_for_document(project_root, manifest);
    if (!core::ResourceId::is_valid(id.value))
        add_error(validation, "id", "Generated resource id is invalid.");
    std::vector<std::string> action_ids;
    for (std::size_t action_index = 0; action_index < actions.size(); ++action_index) {
        const auto& action = actions[action_index];
        const auto field = "actions[" + std::to_string(action_index) + "]";
        if (!core::ResourceId::is_valid(action.id))
            add_error(validation, field + ".id", "Action id must be valid.");
        if (std::find(action_ids.begin(), action_ids.end(), action.id) != action_ids.end())
            add_error(validation, field + ".id", "Action id must be unique.");
        action_ids.push_back(action.id);
        std::vector<project::InputBinding> bindings;
        for (std::size_t binding_index = 0; binding_index < action.bindings.size(); ++binding_index) {
            const auto& binding = action.bindings[binding_index];
            if (binding.code < 0)
                add_error(validation, field + ".bindings[" + std::to_string(binding_index) + "]",
                          "Binding code must be non-negative.");
            if (std::find(bindings.begin(), bindings.end(), binding) != bindings.end())
                add_error(validation, field + ".bindings[" + std::to_string(binding_index) + "]",
                          "Binding must be unique.");
            bindings.push_back(binding);
        }
    }
    validation.destination = project_root /
        project::input_document_path(manifest, id);
    validation.summary = {
        "Create InputDocument v1: " + name,
        "Id: " + id.value,
        "Actions: " + std::to_string(actions.size()),
        "Destination: " + validation.destination.generic_string(),
    };
    return validation;
}

core::ResourceId CreateInputPrompt::resource_id_for_document(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    return available_resource_id(project_root, manifest,
                                 generated_resource_id(name, "input"));
}

PromptValidation CreateAnimationPrompt::validate(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    validate_name(validation, name);
    const auto id = resource_id_for_document(project_root, manifest);
    if (!core::ResourceId::is_valid(id.value)) {
        add_error(validation, "id", "Generated resource id is invalid.");
    }
    if (!std::isfinite(duration) || duration <= 0.0) {
        add_error(validation, "duration",
                  "Duration must be finite and greater than zero.");
    }
    if (!marker_id.empty()) {
        if (!core::ResourceId::is_valid(marker_id)) {
            add_error(validation, "marker", "Marker id must be valid.");
        }
        if (!std::isfinite(marker_time) || marker_time < 0.0 ||
            marker_time > duration) {
            add_error(validation, "markerTime",
                      "Marker time must be finite and within the clip.");
        }
    }
    validation.destination = project_root /
        project::animation_document_path(manifest, id);
    validation.summary = {
        "Create AnimationClip v1: " + name,
        "Id: " + id.value,
        "Destination: " + validation.destination.generic_string(),
        "Duration: " + std::to_string(duration) + " seconds",
        std::string{"Loop: "} + (loop ? "yes" : "no"),
    };
    return validation;
}

core::ResourceId CreateAnimationPrompt::resource_id_for_document(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    return available_resource_id(project_root, manifest,
                                 generated_resource_id(name, "animation"));
}

PromptValidation CreateVectorArtworkPrompt::validate(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    PromptValidation validation;
    const auto id = resource_id(project_root, manifest);
    validation.destination = project_root / manifest.directories.assets /
                             "vectors" / (id.value + ".vector.json");
    validate_name(validation, name);
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
    validation.summary = {
        "Document: native VectorAsset v2 creation intent",
        "Artwork: " + (name.empty() ? std::string{"<unnamed>"} : name),
        "Internal ID: " + id.value + " (generated)",
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

core::ResourceId CreateVectorArtworkPrompt::resource_id(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest) const {
    return available_resource_id(
        project_root, manifest, generated_resource_id(name, "artwork"));
}

} // namespace fabric::editor
