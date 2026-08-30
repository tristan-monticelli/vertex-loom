#include "fabric/editor/visual_presets.hpp"

#include <utility>

namespace fabric::editor {
namespace {

using project::VectorFillKind;
using project::VectorShapeKind;
using project::VisualLayerKind;

void add_error(std::vector<project::Error>& errors,
               const project::ErrorCode code, std::string field,
               std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

core::ResourceId child_id(const core::ResourceId& base,
                          const std::string_view suffix) {
    return {.value = base.value + "-" + std::string{suffix}};
}

project::VectorNode ellipse(std::string id, std::string name,
                            const core::Rect bounds,
                            const core::Color color) {
    const std::string shape_id = id + "-shape";
    return {.id = std::move(id),
            .name = std::move(name),
            .shape = {.id = shape_id,
                      .kind = VectorShapeKind::ellipse,
                      .bounds = bounds},
            .fill = {.kind = VectorFillKind::solid, .color = color}};
}

project::VectorNode rectangle(std::string id, std::string name,
                              const core::Rect bounds,
                              const core::Color color) {
    const std::string shape_id = id + "-shape";
    return {.id = std::move(id),
            .name = std::move(name),
            .shape = {.id = shape_id,
                      .kind = VectorShapeKind::rectangle,
                      .bounds = bounds},
            .fill = {.kind = VectorFillKind::solid, .color = color}};
}

project::VectorAsset vector_asset(const core::ResourceId& id,
                                  std::string name, const core::Vec2 size,
                                  std::vector<project::VectorNode> nodes) {
    return {
        .document = {.schema_version = project::current_vector_schema_version,
                     .type = "vector",
                     .id = id,
                     .name = std::move(name)},
        .source_kind = project::VectorSourceKind::native,
        .native = project::NativeVectorDefinition{
            .size = size,
            .origin = project::VectorOrigin::center,
            .nodes = std::move(nodes)},
    };
}

project::VisualCompositionLayer layer(
    std::string id, std::string name, const VisualLayerKind kind,
    const core::ResourceId& resource, const std::string_view type,
    const core::Transform transform = {}, const float z_order = 0.0F) {
    return {.id = std::move(id),
            .name = std::move(name),
            .kind = kind,
            .resource = {resource, std::string{type}},
            .transform = transform,
            .z_order = z_order};
}

project::PropertyBinding binding(std::string layer_id,
                                 std::string component,
                                 std::string property) {
    return {.node_id = std::move(layer_id),
            .component_id = std::move(component),
            .property_id = std::move(property)};
}

project::VisualComponent component_for(
    const VisualPresetRequest& request, const core::ResourceId& composition_id,
    const core::Rect bounds,
    std::vector<project::VisualComponentParameter> parameters,
    std::vector<project::VisualComponentVariant> variants = {}) {
    return {
        .document = {
            .schema_version = project::current_visual_component_schema_version,
            .type = "visualComponent",
            .id = request.id,
            .name = request.name},
        .composition = {composition_id, "visualComposition"},
        .bounds = bounds,
        .anchors = {{"center", "Center", {0.0F, 0.0F}}},
        .parameters = std::move(parameters),
        .variants = std::move(variants),
    };
}

VisualPresetBundle eye_preset(const VisualPresetRequest& request) {
    const auto artwork_id = child_id(request.id, "artwork");
    const auto composition_id = child_id(request.id, "composition");
    auto artwork = vector_asset(
        artwork_id, request.name + " Artwork", {2.0F, 1.5F},
        {ellipse("sclera", "Sclera", {{-1.0F, -0.65F}, {2.0F, 1.3F}},
                 {0.96F, 0.91F, 0.78F, 1.0F}),
         ellipse("iris", "Iris", {{-0.48F, -0.48F}, {0.96F, 0.96F}},
                 {0.28F, 0.55F, 0.62F, 1.0F}),
         ellipse("pupil", "Pupil", {{-0.2F, -0.32F}, {0.4F, 0.64F}},
                 {0.08F, 0.07F, 0.06F, 1.0F}),
         ellipse("highlight", "Highlight", {{0.02F, -0.18F}, {0.12F, 0.18F}},
                 {1.0F, 0.98F, 0.9F, 0.9F})});
    project::VisualComposition composition{
        .document = {.schema_version =
                         project::current_visual_composition_schema_version,
                     .type = "visualComposition",
                     .id = composition_id,
                     .name = request.name + " Composition"},
        .size = {2.0F, 1.5F},
        .layers = {layer("eye", "Eye", VisualLayerKind::vector,
                         artwork_id, "vector")},
    };
    using Type = project::VisualParameterType;
    auto component = component_for(
        request, composition_id, {{-1.0F, -0.75F}, {2.0F, 1.5F}},
        {{"scale", "Scale", Type::vec2, core::Vec2{1.0F, 1.0F},
          binding("eye", "transform", "scale"), true},
         {"rotation", "Rotation", Type::angle, 0.0F,
          binding("eye", "transform", "rotationDegrees"), true},
         {"opacity", "Opacity", Type::scalar, 1.0F,
          binding("eye", "layer", "opacity"), true}},
        {{"sleepy", "Sleepy",
          {{"scale", core::Vec2{1.0F, 0.55F}}}},
         {"wide", "Wide",
          {{"scale", core::Vec2{1.2F, 1.1F}}}}});
    return {.vectors = {std::move(artwork)},
            .composition = std::move(composition),
            .component = std::move(component)};
}

VisualPresetBundle button_preset(const VisualPresetRequest& request) {
    const auto artwork_id = child_id(request.id, "artwork");
    const auto composition_id = child_id(request.id, "composition");
    std::vector<project::VectorNode> nodes;
    nodes.push_back(ellipse("body", "Button body",
                            {{-0.75F, -0.75F}, {1.5F, 1.5F}},
                            {0.72F, 0.24F, 0.18F, 1.0F}));
    constexpr float hole = 0.12F;
    for (std::size_t index = 0U; index < 4U; ++index) {
        const float x = index % 2U == 0U ? -0.27F : 0.27F;
        const float y = index < 2U ? -0.27F : 0.27F;
        nodes.push_back(ellipse(
            "hole-" + std::to_string(index + 1U), "Thread hole",
            {{x - hole, y - hole}, {hole * 2.0F, hole * 2.0F}},
            {0.12F, 0.08F, 0.06F, 1.0F}));
    }
    auto artwork = vector_asset(
        artwork_id, request.name + " Artwork", {1.5F, 1.5F},
        std::move(nodes));
    project::VisualComposition composition{
        .document = {.schema_version =
                         project::current_visual_composition_schema_version,
                     .type = "visualComposition",
                     .id = composition_id,
                     .name = request.name + " Composition"},
        .size = {1.5F, 1.5F},
        .layers = {layer("button", "Button", VisualLayerKind::vector,
                         artwork_id, "vector")},
    };
    using Type = project::VisualParameterType;
    auto component = component_for(
        request, composition_id, {{-0.75F, -0.75F}, {1.5F, 1.5F}},
        {{"scale", "Scale", Type::vec2, core::Vec2{1.0F, 1.0F},
          binding("button", "transform", "scale"), true},
         {"rotation", "Rotation", Type::angle, 0.0F,
          binding("button", "transform", "rotationDegrees"), true},
         {"opacity", "Opacity", Type::scalar, 1.0F,
          binding("button", "layer", "opacity"), true}},
        {{"small", "Small", {{"scale", core::Vec2{0.7F, 0.7F}}}},
         {"large", "Large", {{"scale", core::Vec2{1.4F, 1.4F}}}}});
    return {.vectors = {std::move(artwork)},
            .composition = std::move(composition),
            .component = std::move(component)};
}

project::TexturedPath rail(const core::ResourceId& id, std::string name,
                           const project::ResourceReference& texture,
                           const float x) {
    using Kind = project::TexturedPathCommandKind;
    return {
        .document = {.schema_version =
                         project::current_textured_path_schema_version,
                     .type = "texturedPath",
                     .id = id,
                     .name = std::move(name)},
        .commands = {{.kind = Kind::move, .point = {x, -2.0F}},
                     {.kind = Kind::cubic,
                      .point = {x, 2.0F},
                      .control1 = {x - 0.08F, -0.7F},
                      .control2 = {x + 0.08F, 0.7F}}},
        .width = 0.12F,
        .texture = texture,
        .uv_scale = {5.0F, 1.0F},
        .join = project::TexturedPathJoin::round,
        .cap = project::TexturedPathCap::round,
    };
}

VisualPresetBundle seam_preset(const VisualPresetRequest& request) {
    const auto path_id = child_id(request.id, "rail");
    const auto composition_id = child_id(request.id, "composition");
    auto path = rail(path_id, request.name + " Rail",
                     *request.thread_texture, 0.0F);
    path.commands.front().point = {-2.0F, 0.0F};
    path.commands.back().point = {2.0F, 0.0F};
    path.commands.back().control1 = {-0.8F, 0.3F};
    path.commands.back().control2 = {0.8F, -0.3F};
    project::VisualComposition composition{
        .document = {.schema_version =
                         project::current_visual_composition_schema_version,
                     .type = "visualComposition",
                     .id = composition_id,
                     .name = request.name + " Composition"},
        .size = {4.0F, 0.8F},
        .layers = {layer("seam", "Seam", VisualLayerKind::textured_path,
                         path_id, "texturedPath")},
    };
    using Type = project::VisualParameterType;
    auto component = component_for(
        request, composition_id, {{-2.0F, -0.4F}, {4.0F, 0.8F}},
        {{"width", "Width", Type::scalar, path.width,
          binding("seam", "texturedPath", "width"), true},
         {"repeat", "Repeat", Type::scalar, path.uv_scale.x,
          binding("seam", "texturedPath", "uvScaleX"), true},
         {"offset", "Texture offset", Type::scalar, 0.0F,
          binding("seam", "texturedPath", "uvOffsetX"), true},
         {"color", "Color", Type::color, path.color,
          binding("seam", "texturedPath", "color"), true},
         {"opacity", "Opacity", Type::scalar, path.opacity,
          binding("seam", "texturedPath", "opacity"), true}});
    return {.textured_paths = {std::move(path)},
            .composition = std::move(composition),
            .component = std::move(component)};
}

VisualPresetBundle zipper_preset(const VisualPresetRequest& request) {
    const auto left_id = child_id(request.id, "left-rail");
    const auto right_id = child_id(request.id, "right-rail");
    const auto tooth_id = child_id(request.id, "tooth");
    const auto slider_id = child_id(request.id, "slider");
    const auto composition_id = child_id(request.id, "composition");
    auto left = rail(left_id, request.name + " Left rail",
                     *request.thread_texture, -0.42F);
    auto right = rail(right_id, request.name + " Right rail",
                      *request.thread_texture, 0.42F);
    auto tooth = vector_asset(
        tooth_id, request.name + " Tooth", {0.62F, 0.18F},
        {rectangle("tooth", "Tooth", {{-0.31F, -0.09F}, {0.62F, 0.18F}},
                   {0.82F, 0.72F, 0.48F, 1.0F})});
    auto slider = vector_asset(
        slider_id, request.name + " Slider", {0.9F, 0.7F},
        {ellipse("body", "Slider body", {{-0.45F, -0.35F}, {0.9F, 0.7F}},
                 {0.74F, 0.58F, 0.3F, 1.0F}),
         ellipse("opening", "Slider opening",
                 {{-0.18F, -0.14F}, {0.36F, 0.28F}},
                 {0.18F, 0.13F, 0.08F, 1.0F})});
    project::VisualComposition composition{
        .document = {.schema_version =
                         project::current_visual_composition_schema_version,
                     .type = "visualComposition",
                     .id = composition_id,
                     .name = request.name + " Composition"},
        .size = {1.6F, 4.4F},
    };
    composition.layers.push_back(layer(
        "left-rail", "Left rail", VisualLayerKind::textured_path,
        left_id, "texturedPath", {}, 0.0F));
    composition.layers.push_back(layer(
        "right-rail", "Right rail", VisualLayerKind::textured_path,
        right_id, "texturedPath", {}, 0.0F));
    const float first_y = -1.75F;
    const float spacing = request.zipper_tooth_count > 1U
        ? 3.5F / static_cast<float>(request.zipper_tooth_count - 1U) : 0.0F;
    for (std::size_t index = 0U; index < request.zipper_tooth_count; ++index) {
        const float y = first_y + spacing * static_cast<float>(index);
        composition.layers.push_back(layer(
            "tooth-" + std::to_string(index + 1U), "Tooth",
            VisualLayerKind::vector, tooth_id, "vector",
            {.position = {0.0F, y}}, 1.0F));
    }
    composition.layers.push_back(layer(
        "slider", "Slider", VisualLayerKind::vector, slider_id, "vector",
        {.position = {0.0F, 0.0F}}, 2.0F));
    using Type = project::VisualParameterType;
    auto component = component_for(
        request, composition_id, {{-0.8F, -2.2F}, {1.6F, 4.4F}},
        {{"slider-position", "Slider position", Type::vec2,
          core::Vec2{0.0F, 0.0F},
          binding("slider", "transform", "position"), true},
         {"slider-rotation", "Slider rotation", Type::angle, 0.0F,
          binding("slider", "transform", "rotationDegrees"), true},
         {"slider-opacity", "Slider opacity", Type::scalar, 1.0F,
          binding("slider", "layer", "opacity"), true}});
    return {.vectors = {std::move(tooth), std::move(slider)},
            .textured_paths = {std::move(left), std::move(right)},
            .composition = std::move(composition),
            .component = std::move(component)};
}

void append_errors(std::vector<project::Error>& destination,
                   project::ValidationReport report) {
    destination.insert(destination.end(),
                       std::make_move_iterator(report.errors.begin()),
                       std::make_move_iterator(report.errors.end()));
}

std::vector<std::filesystem::path> document_paths(
    const project::ProjectManifest& manifest,
    const VisualPresetBundle& bundle) {
    std::vector<std::filesystem::path> paths;
    for (const auto& vector : bundle.vectors) {
        paths.push_back(project::vector_document_path(
            manifest, vector.document.id));
    }
    for (const auto& path : bundle.textured_paths) {
        paths.push_back(project::textured_path_document_path(
            manifest, path.document.id));
    }
    paths.push_back(project::visual_composition_document_path(
        manifest, bundle.composition.document.id));
    paths.push_back(project::visual_component_document_path(
        manifest, bundle.component.document.id));
    return paths;
}

} // namespace

std::string_view label(const VisualPresetKind kind) noexcept {
    switch (kind) {
    case VisualPresetKind::eye: return "Eye";
    case VisualPresetKind::button: return "Button";
    case VisualPresetKind::seam: return "Seam";
    case VisualPresetKind::zipper: return "Zipper";
    }
    return "Visual preset";
}

VisualPresetResult build_visual_preset(
    const project::ProjectManifest& manifest,
    const VisualPresetRequest& request) {
    VisualPresetResult result;
    if (!core::ResourceId::is_valid(request.id.value)) {
        add_error(result.errors, project::ErrorCode::invalid_resource_id,
                  "id", "preset id must be a valid resource identifier");
    }
    if (request.name.empty()) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "name", "preset name must not be empty");
    }
    const bool uses_thread = request.kind == VisualPresetKind::seam ||
        request.kind == VisualPresetKind::zipper;
    if (uses_thread && (!request.thread_texture ||
        request.thread_texture->expected_type != "texture" ||
        !core::ResourceId::is_valid(request.thread_texture->id.value))) {
        add_error(result.errors, project::ErrorCode::resource_type_mismatch,
                  "threadTexture",
                  "seam and zipper presets require a texture resource");
    }
    if (request.kind == VisualPresetKind::zipper &&
        (request.zipper_tooth_count < 2U ||
         request.zipper_tooth_count > 128U)) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "zipperToothCount", "tooth count must be between 2 and 128");
    }
    if (!result.errors.empty()) return result;

    VisualPresetBundle bundle;
    switch (request.kind) {
    case VisualPresetKind::eye: bundle = eye_preset(request); break;
    case VisualPresetKind::button: bundle = button_preset(request); break;
    case VisualPresetKind::seam: bundle = seam_preset(request); break;
    case VisualPresetKind::zipper: bundle = zipper_preset(request); break;
    }
    for (const auto& vector : bundle.vectors) {
        append_errors(result.errors,
                      project::validate_vector_asset(manifest, vector));
    }
    for (const auto& path : bundle.textured_paths) {
        append_errors(result.errors,
                      project::validate_textured_path(manifest, path));
    }
    append_errors(result.errors, project::validate_visual_composition(
                                      manifest, bundle.composition));
    append_errors(result.errors, project::validate_visual_component(
                                      manifest, bundle.component));
    if (result.errors.empty()) result.bundle = std::move(bundle);
    return result;
}

VisualPresetResult publish_visual_preset(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const VisualPresetRequest& request) {
    auto result = build_visual_preset(manifest, request);
    if (!result.ok()) return result;
    for (const auto& path : document_paths(manifest, *result.bundle)) {
        std::error_code error;
        if (std::filesystem::exists(project_root / path, error) || error) {
            result.bundle.reset();
            add_error(result.errors, project::ErrorCode::asset_already_exists,
                      "destination", "preset destination already exists");
            return result;
        }
    }
    for (const auto& vector : result.bundle->vectors) {
        const auto published = project::publish_native_vector_asset(
            project_root, manifest, vector);
        if (!published.ok()) {
            result.bundle.reset();
            result.errors = published.errors;
            return result;
        }
    }
    for (const auto& path : result.bundle->textured_paths) {
        const auto published = project::publish_textured_path(
            project_root, manifest, path);
        if (!published.ok()) {
            result.bundle.reset();
            result.errors = published.errors;
            return result;
        }
    }
    const auto composition = project::publish_visual_composition(
        project_root, manifest, result.bundle->composition);
    if (!composition.ok()) {
        result.bundle.reset();
        result.errors = composition.errors;
        return result;
    }
    const auto component = project::publish_visual_component(
        project_root, manifest, result.bundle->component);
    if (!component.ok()) {
        result.bundle.reset();
        result.errors = component.errors;
        return result;
    }
    return result;
}

} // namespace fabric::editor
