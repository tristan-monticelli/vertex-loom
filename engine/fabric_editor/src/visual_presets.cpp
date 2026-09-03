#include "fabric/editor/visual_presets.hpp"

#include <cmath>
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

project::VectorNode outlined_rectangle(std::string id, std::string name,
                                       const core::Rect bounds,
                                       const core::Color color,
                                       const float width) {
    auto node = rectangle(std::move(id), std::move(name), bounds,
                          {0.0F, 0.0F, 0.0F, 0.0F});
    node.fill = {.kind = VectorFillKind::none};
    node.stroke = project::VectorStroke{
        .color = color,
        .width = width,
        .join = project::VectorStrokeJoin::round,
        .cap = project::VectorStrokeCap::round};
    return node;
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
    const auto border_id = child_id(request.id, "border");
    const auto composition_id = child_id(request.id, "composition");
    auto path = rail(path_id, request.name + " Rail",
                     *request.thread_texture, 0.0F);
    path.commands.front().point = {-2.0F, 0.0F};
    path.commands.back().point = {2.0F, 0.0F};
    path.commands.back().control1 = {-0.8F, 0.3F};
    path.commands.back().control2 = {0.8F, -0.3F};
    if (request.guided_beam) {
        path.width = request.beam_width;
        path.opacity = request.beam_opacity;
        path.uv_scale.x = request.beam_repetition;
        path.cap = project::TexturedPathCap::butt;
        // The provided Beam PNG remains intact. The 192 px alpha bounds keep
        // 35 px of vertical safety margin; U keeps the full 2048 px width.
        path.texture_metrics = {
            .origin = {0.0F, 0.4375F},
            .size = {1.0F, 0.12890625F}};
        // Beam coloring belongs to the Thread shader. Tinting the raster fill
        // too would multiply the same color twice and erase source details.
        path.color = {1.0F, 1.0F, 1.0F, 1.0F};
        const bool preserve_source = request.beam_color_mode ==
            BeamColorMode::preserve_source;
        path.shader.profile = preserve_source
            ? project::SurfaceShaderProfile::plastic
            : project::SurfaceShaderProfile::thread;
        path.shader.classification = project::TextureClassification::beam;
        path.shader.primary_color = request.beam_color;
        path.shader.effect_color = request.beam_effect_color;
        path.shader.shine = request.beam_shine;
        path.shader.holography = request.beam_holography;
        path.shader.repetition = {request.beam_repetition, 1.0F};
        path.shader.effects = {
            {.kind = project::SurfaceEffectKind::tint,
             .color = request.beam_color,
             .amount = preserve_source ? 0.0F : 1.0F},
            {.kind = project::SurfaceEffectKind::holography,
             .color = request.beam_effect_color,
             .amount = request.beam_holography},
            {.kind = project::SurfaceEffectKind::shine,
             .color = {1.0F, 1.0F, 1.0F, 1.0F},
             .amount = request.beam_shine},
        };
    }
    auto border = vector_asset(
        border_id, request.name + " Border", {4.0F, 0.8F},
        {outlined_rectangle("border-shape", "Beam border",
                            {{-2.0F, -0.4F}, {4.0F, 0.8F}},
                            {0.08F, 0.04F, 0.02F, 1.0F}, 0.06F)});
    const std::string layer_id = request.guided_beam ? "beam" : "seam";
    project::VisualComposition composition{
        .document = {.schema_version =
                         project::current_visual_composition_schema_version,
                     .type = "visualComposition",
                     .id = composition_id,
                     .name = request.name + " Composition"},
        .size = {4.0F, 0.8F},
        .layers = {layer(layer_id, request.guided_beam ? "Beam" : "Seam",
                         VisualLayerKind::textured_path,
                         path_id, "texturedPath", {}, 0.0F)},
    };
    if (!request.guided_beam) {
        composition.layers.push_back(layer(
            "border", "Beam border", VisualLayerKind::vector,
            border_id, "vector", {}, 1.0F));
    }
    using Type = project::VisualParameterType;
    std::vector<project::VisualComponentParameter> parameters;
    if (request.guided_beam) {
        parameters = {
            {"texture", "Texture", Type::resource, path.texture,
             binding(layer_id, "texturedPath", "texture"), false},
            {"color-mode", "Color handling", Type::text,
             std::string{request.beam_color_mode ==
                     BeamColorMode::preserve_source
                 ? "preserve" : "recolor"},
             binding(layer_id, "shader", "colorMode"), false},
            {"width", "Thickness", Type::scalar, path.width,
             binding(layer_id, "texturedPath", "width"), true},
            {"repeat", "Repetition", Type::scalar, path.uv_scale.x,
             binding(layer_id, "texturedPath", "uvScaleX"), true},
            {"color", "Base tint", Type::color, path.shader.primary_color,
             binding(layer_id, "shader", "primaryColor"), true},
            {"effect-color", "Holo color", Type::color,
             path.shader.effect_color,
             binding(layer_id, "shader", "effectColor"), true},
            {"shine", "Shine", Type::scalar, path.shader.shine,
             binding(layer_id, "shader", "shine"), true},
            {"holography", "Holography", Type::scalar,
             path.shader.holography,
             binding(layer_id, "shader", "holography"), true},
            {"opacity", "Opacity", Type::scalar, path.opacity,
             binding(layer_id, "texturedPath", "opacity"), true}};
    } else {
        parameters = {
            {"width", "Width", Type::scalar, path.width,
             binding(layer_id, "texturedPath", "width"), true},
            {"repeat", "Repeat", Type::scalar, path.uv_scale.x,
             binding(layer_id, "texturedPath", "uvScaleX"), true},
            {"offset", "Texture offset", Type::scalar, 0.0F,
             binding(layer_id, "texturedPath", "uvOffsetX"), true},
            {"color", "Color", Type::color, path.color,
             binding(layer_id, "texturedPath", "color"), true},
            {"opacity", "Opacity", Type::scalar, path.opacity,
             binding(layer_id, "texturedPath", "opacity"), true}};
    }
    auto component = component_for(
        request, composition_id, {{-2.0F, -0.4F}, {4.0F, 0.8F}},
        std::move(parameters));
    return {.vectors = request.guided_beam
            ? std::vector<project::VectorAsset>{}
            : std::vector<project::VectorAsset>{std::move(border)},
            .textured_paths = {std::move(path)},
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
    case VisualPresetKind::beam: return "Beam";
    case VisualPresetKind::seam: return "Seam";
    case VisualPresetKind::zipper: return "Zipper";
    }
    return "Visual preset";
}

VisualPresetResult build_visual_preset(
    const project::ProjectManifest& manifest,
    const VisualPresetRequest& request) {
    VisualPresetResult result;
    auto effective_request = request;
    if (!effective_request.thread_texture && manifest.default_stroke_texture)
        effective_request.thread_texture = project::ResourceReference{
            *manifest.default_stroke_texture, "texture"};
    if (!core::ResourceId::is_valid(request.id.value)) {
        add_error(result.errors, project::ErrorCode::invalid_resource_id,
                  "id", "preset id must be a valid resource identifier");
    }
    if (request.name.empty()) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "name", "preset name must not be empty");
    }
    const bool uses_thread = effective_request.kind == VisualPresetKind::beam ||
        effective_request.kind == VisualPresetKind::seam ||
        effective_request.kind == VisualPresetKind::zipper;
    if (uses_thread && (!effective_request.thread_texture ||
        effective_request.thread_texture->expected_type != "texture" ||
        !core::ResourceId::is_valid(effective_request.thread_texture->id.value))) {
        add_error(result.errors, project::ErrorCode::resource_type_mismatch,
                  "threadTexture",
                  "beam, seam and zipper presets require a texture resource");
    }
    if (effective_request.kind == VisualPresetKind::zipper &&
        (effective_request.zipper_tooth_count < 2U ||
         effective_request.zipper_tooth_count > 128U)) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "zipperToothCount", "tooth count must be between 2 and 128");
    }
    if (effective_request.kind == VisualPresetKind::beam &&
        (!std::isfinite(effective_request.beam_width) ||
         effective_request.beam_width <= 0.0F)) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "beamWidth", "beam width must be finite and positive");
    }
    if (effective_request.kind == VisualPresetKind::beam &&
        (!std::isfinite(effective_request.beam_repetition) ||
         effective_request.beam_repetition <= 0.0F)) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "beamRepetition",
                  "beam repetition must be finite and positive");
    }
    if (effective_request.kind == VisualPresetKind::beam &&
        (!std::isfinite(effective_request.beam_opacity) ||
         effective_request.beam_opacity < 0.0F ||
         effective_request.beam_opacity > 1.0F)) {
        add_error(result.errors, project::ErrorCode::invalid_asset,
                  "beamOpacity", "beam opacity must be in [0, 1]");
    }
    if (!result.errors.empty()) return result;

    VisualPresetBundle bundle;
    if (effective_request.kind == VisualPresetKind::beam)
        effective_request.guided_beam = true;
    switch (request.kind) {
    case VisualPresetKind::beam: bundle = seam_preset(effective_request); break;
    case VisualPresetKind::seam: bundle = seam_preset(effective_request); break;
    case VisualPresetKind::zipper: bundle = zipper_preset(effective_request); break;
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
