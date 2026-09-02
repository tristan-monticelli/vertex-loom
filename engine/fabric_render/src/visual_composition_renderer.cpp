#include "fabric/render/visual_composition_renderer.hpp"

#include "fabric/render/svg_vector.hpp"
#include "fabric/render/textured_path_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace fabric::render {
namespace {

struct LayerOverrides {
    std::optional<core::Vec2> position;
    std::optional<core::Vec2> scale;
    std::optional<float> rotation;
    std::optional<float> opacity;
    std::optional<float> path_width;
    std::optional<float> path_repeat;
    std::optional<float> path_offset;
    std::optional<project::ResourceReference> path_texture;
    std::optional<core::Color> path_color;
    std::optional<float> path_opacity;
    std::optional<core::Color> shader_primary;
    std::optional<core::Color> shader_effect;
    std::optional<project::SurfaceShaderProfile> shader_profile;
    std::optional<float> shader_shine;
    std::optional<float> shader_holography;
    std::optional<float> shader_intensity;
};

using OverrideMap = std::unordered_map<std::string, LayerOverrides>;

void append_errors(VisualCompositionDrawResult& result,
                   const std::vector<project::Error>& errors) {
    for (const auto& error : errors) {
        result.errors.push_back(error.field + ": " + error.message);
    }
}

core::Vec2 transform_point(const core::Vec2 point,
                           const core::Transform& transform) {
    const core::Vec2 local{
        (point.x - transform.pivot.x) * transform.scale.x,
        (point.y - transform.pivot.y) * transform.scale.y};
    const float angle = transform.rotation_degrees *
        std::numbers::pi_v<float> / 180.0F;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {local.x * cosine - local.y * sine + transform.pivot.x +
                transform.position.x,
            local.x * sine + local.y * cosine + transform.pivot.y +
                transform.position.y};
}

void transform_packet(VectorDrawPacket& packet,
                      const core::Transform& transform,
                      const float opacity, const std::string& prefix) {
    for (auto& point : packet.outline) point = transform_point(point, transform);
    for (auto& point : packet.fill_vertices) {
        point = transform_point(point, transform);
    }
    if (packet.image_fill) {
        packet.image_fill->opacity *= opacity;
    } else if (packet.fill_color) {
        packet.fill_color->alpha *= opacity;
    }
    if (packet.stroke) packet.stroke->color.alpha *= opacity;
    if (packet.shader) packet.shader->opacity *= opacity;
    packet.node_id = prefix + packet.node_id;
    if (packet.parent_id) packet.parent_id = prefix + *packet.parent_id;
    if (packet.clip_node_id) packet.clip_node_id = prefix + *packet.clip_node_id;
}

bool apply_parameter(OverrideMap& overrides,
                     const project::ResolvedVisualParameter& parameter) {
    auto& layer = overrides[parameter.target.node_id];
    const auto& component = parameter.target.component_id;
    const auto& property = parameter.target.property_id;
    if (component == "transform" && property == "position") {
        if (const auto* value = std::get_if<core::Vec2>(&parameter.value)) {
            layer.position = *value;
            return true;
        }
    } else if (component == "transform" && property == "scale") {
        if (const auto* value = std::get_if<core::Vec2>(&parameter.value)) {
            layer.scale = *value;
            return true;
        }
    } else if (component == "transform" && property == "rotationDegrees") {
        if (const auto* value = std::get_if<float>(&parameter.value)) {
            layer.rotation = *value;
            return true;
        }
    } else if (component == "layer" && property == "opacity") {
        if (const auto* value = std::get_if<float>(&parameter.value)) {
            layer.opacity = *value;
            return true;
        }
    } else if (component == "texturedPath" && property == "width") {
        if (const auto* value = std::get_if<float>(&parameter.value)) {
            layer.path_width = *value;
            return true;
        }
    } else if (component == "texturedPath" && property == "uvScaleX") {
        if (const auto* value = std::get_if<float>(&parameter.value)) {
            layer.path_repeat = *value;
            return true;
        }
    } else if (component == "texturedPath" && property == "uvOffsetX") {
        if (const auto* value = std::get_if<float>(&parameter.value)) {
            layer.path_offset = *value;
            return true;
        }
    } else if (component == "texturedPath" && property == "color") {
        if (const auto* value = std::get_if<core::Color>(&parameter.value)) {
            layer.path_color = *value;
            return true;
        }
    } else if (component == "texturedPath" && property == "opacity") {
        if (const auto* value = std::get_if<float>(&parameter.value)) {
            layer.path_opacity = *value;
            return true;
        }
    } else if (component == "texturedPath" && property == "texture") {
        if (const auto* value =
                std::get_if<project::ResourceReference>(&parameter.value)) {
            layer.path_texture = *value;
            return true;
        }
    } else if (component == "shader" && property == "primaryColor") {
        if (const auto* value = std::get_if<core::Color>(&parameter.value)) { layer.shader_primary = *value; return true; }
    } else if (component == "shader" && property == "effectColor") {
        if (const auto* value = std::get_if<core::Color>(&parameter.value)) { layer.shader_effect = *value; return true; }
    } else if (component == "shader" && property == "colorMode") {
        if (const auto* value = std::get_if<std::string>(&parameter.value)) {
            if (*value == "recolor") {
                layer.shader_profile = project::SurfaceShaderProfile::thread;
                return true;
            }
            if (*value == "preserve") {
                layer.shader_profile = project::SurfaceShaderProfile::plastic;
                return true;
            }
        }
    } else if (component == "shader" && property == "shine") {
        if (const auto* value = std::get_if<float>(&parameter.value)) { layer.shader_shine = *value; return true; }
    } else if (component == "shader" && property == "holography") {
        if (const auto* value = std::get_if<float>(&parameter.value)) { layer.shader_holography = *value; return true; }
    } else if (component == "shader" && property == "intensity") {
        if (const auto* value = std::get_if<float>(&parameter.value)) { layer.shader_intensity = *value; return true; }
    }
    return false;
}

void translate_packets(std::vector<VectorDrawPacket>& packets,
                       const core::Vec2 offset) {
    for (auto& packet : packets) {
        for (auto& point : packet.outline) {
            point.x += offset.x;
            point.y += offset.y;
        }
        for (auto& point : packet.fill_vertices) {
            point.x += offset.x;
            point.y += offset.y;
        }
    }
}

void apply_path_overrides(project::TexturedPath& path,
                          const LayerOverrides& overrides) {
    const auto effect_of_kind = [&](const project::SurfaceEffectKind kind) {
        return std::ranges::find(path.shader.effects, kind,
                                 &project::SurfaceEffect::kind);
    };
    if (overrides.path_width) path.width = *overrides.path_width;
    if (overrides.path_repeat) path.uv_scale.x = *overrides.path_repeat;
    if (overrides.path_offset) path.uv_offset.x = *overrides.path_offset;
    if (overrides.path_texture) path.texture = *overrides.path_texture;
    if (overrides.path_color) path.color = *overrides.path_color;
    if (overrides.path_opacity) path.opacity = *overrides.path_opacity;
    if (overrides.shader_primary) {
        path.shader.primary_color = *overrides.shader_primary;
        const auto effect = effect_of_kind(project::SurfaceEffectKind::tint);
        if (effect != path.shader.effects.end()) effect->color = *overrides.shader_primary;
    }
    if (overrides.shader_effect) {
        path.shader.effect_color = *overrides.shader_effect;
        const auto effect = effect_of_kind(project::SurfaceEffectKind::holography);
        if (effect != path.shader.effects.end()) effect->color = *overrides.shader_effect;
    }
    if (overrides.shader_profile) path.shader.profile = *overrides.shader_profile;
    if (overrides.shader_shine) {
        path.shader.shine = *overrides.shader_shine;
        const auto effect = effect_of_kind(project::SurfaceEffectKind::shine);
        if (effect != path.shader.effects.end()) effect->amount = *overrides.shader_shine;
    }
    if (overrides.shader_holography) {
        path.shader.holography = *overrides.shader_holography;
        const auto effect = effect_of_kind(project::SurfaceEffectKind::holography);
        if (effect != path.shader.effects.end())
            effect->amount = *overrides.shader_holography;
    }
    if (overrides.shader_intensity) path.shader.intensity = *overrides.shader_intensity;
}

void calculate_bounds(VisualCompositionDrawResult& result) {
    if (result.packets.empty()) return;
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const auto& packet : result.packets) {
        const auto& points = packet.fill_vertices.empty()
            ? packet.outline : packet.fill_vertices;
        for (const auto point : points) {
            min_x = std::min(min_x, point.x);
            min_y = std::min(min_y, point.y);
            max_x = std::max(max_x, point.x);
            max_y = std::max(max_y, point.y);
        }
    }
    if (min_x <= max_x && min_y <= max_y) {
        const float margin = std::max(
            0.1F, std::max(max_x - min_x, max_y - min_y) * 0.1F);
        result.bounds = {{min_x - margin, min_y - margin},
                         {max_x - min_x + 2.0F * margin,
                          max_y - min_y + 2.0F * margin}};
    }
}

VisualCompositionDrawResult resolve_composition(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComposition& composition,
    const OverrideMap& overrides,
    std::unordered_set<std::string>& active_components);

VisualCompositionDrawResult resolve_component_internal(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComponent& component,
    const project::VisualComponentInstance& instance,
    std::unordered_set<std::string>& active_components) {
    VisualCompositionDrawResult result;
    if (!active_components.insert(component.document.id.value).second) {
        result.errors.push_back("visual component cycle: " +
                                component.document.id.value);
        return result;
    }
    const auto resolved = project::resolve_visual_component_instance(
        component, instance);
    if (!resolved.ok()) {
        append_errors(result, resolved.errors);
        active_components.erase(component.document.id.value);
        return result;
    }
    OverrideMap overrides;
    for (const auto& parameter : resolved.parameters) {
        if (!apply_parameter(overrides, parameter)) {
            result.errors.push_back(
                "unsupported visual parameter target: " +
                parameter.target.node_id + "." +
                parameter.target.component_id + "." +
                parameter.target.property_id);
        }
    }
    auto loaded = project::load_visual_composition(
        project_root, manifest,
        project::visual_composition_document_path(
            manifest, component.composition.id));
    if (!loaded.ok()) {
        append_errors(result, loaded.errors);
        active_components.erase(component.document.id.value);
        return result;
    }
    auto parameter_errors = std::move(result.errors);
    result = resolve_composition(project_root, manifest, *loaded.asset,
                                 overrides, active_components);
    result.errors.insert(result.errors.begin(), parameter_errors.begin(),
                         parameter_errors.end());
    if (instance.anchor_id) {
        const auto anchor = std::ranges::find_if(
            component.anchors, [&](const auto& candidate) {
                return candidate.id == *instance.anchor_id;
            });
        if (anchor != component.anchors.end()) {
            translate_packets(result.packets,
                              {-anchor->position.x, -anchor->position.y});
            calculate_bounds(result);
        }
    }
    active_components.erase(component.document.id.value);
    return result;
}

VisualCompositionDrawResult resolve_composition(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComposition& composition,
    const OverrideMap& overrides,
    std::unordered_set<std::string>& active_components) {
    VisualCompositionDrawResult result;
    std::vector<std::size_t> order(composition.layers.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::ranges::stable_sort(order, {}, [&](const std::size_t index) {
        return composition.layers[index].z_order;
    });
    for (const auto index : order) {
        auto layer = composition.layers[index];
        if (!layer.visible) continue;
        const auto override = overrides.find(layer.id);
        if (override != overrides.end()) {
            if (override->second.position)
                layer.transform.position = *override->second.position;
            if (override->second.scale)
                layer.transform.scale = *override->second.scale;
            if (override->second.rotation)
                layer.transform.rotation_degrees = *override->second.rotation;
            if (override->second.opacity)
                layer.opacity = *override->second.opacity;
        }
        layer.transform.position.x +=
            (layer.anchor.x - 0.5F) * composition.size.x;
        layer.transform.position.y +=
            (layer.anchor.y - 0.5F) * composition.size.y;
        VectorGeometryResult geometry;
        if (layer.kind == project::VisualLayerKind::vector) {
            auto loaded = project::load_vector_asset(
                project_root, manifest,
                project::vector_document_path(manifest, layer.resource.id));
            if (!loaded.ok()) {
                append_errors(result, loaded.errors);
                continue;
            }
            auto vector = std::move(*loaded.asset);
            if (vector.source_kind == project::VectorSourceKind::linked_svg) {
                auto converted = convert_svg_to_native(
                    project_root / vector.source, vector.document.id,
                    vector.document.name);
                if (!converted.ok()) {
                    append_errors(result, converted.errors);
                    continue;
                }
                vector = std::move(*converted.asset);
            }
            geometry = build_native_draw_packets(vector);
        } else if (layer.kind == project::VisualLayerKind::raster) {
            auto loaded = project::load_texture_asset(
                project_root, manifest,
                project::texture_document_path(manifest, layer.resource.id));
            if (!loaded.ok()) {
                append_errors(result, loaded.errors);
                continue;
            }
            geometry = build_raster_view_draw_packets({
                .node_id = layer.id,
                .texture = layer.resource,
                .source_width = loaded.asset->width,
                .source_height = loaded.asset->height,
                .pixels_per_unit = static_cast<float>(manifest.pixels_per_unit),
                .view = layer.raster_view ? layer.raster_view : loaded.asset->view});
        } else if (layer.kind == project::VisualLayerKind::textured_path) {
            auto loaded = project::load_textured_path(
                project_root, manifest,
                project::textured_path_document_path(manifest, layer.resource.id));
            if (!loaded.ok()) {
                append_errors(result, loaded.errors);
                continue;
            }
            auto path = std::move(*loaded.asset);
            auto texture = project::load_texture_asset(
                project_root, manifest,
                project::texture_document_path(manifest, path.texture.id));
            if (!texture.ok()) {
                append_errors(result, texture.errors);
                continue;
            }
            if (override != overrides.end())
                apply_path_overrides(path, override->second);
            geometry = build_textured_path_draw_packets(path);
        } else {
            auto loaded = project::load_visual_component(
                project_root, manifest,
                project::visual_component_document_path(manifest, layer.resource.id));
            if (!loaded.ok()) {
                append_errors(result, loaded.errors);
                continue;
            }
            const auto instance = layer.component_instance.value_or(
                project::VisualComponentInstance{});
            auto child = resolve_component_internal(
                project_root, manifest, *loaded.asset, instance,
                active_components);
            result.errors.insert(result.errors.end(), child.errors.begin(),
                                 child.errors.end());
            geometry.packets = std::move(child.packets);
        }
        result.errors.insert(result.errors.end(), geometry.errors.begin(),
                             geometry.errors.end());
        const std::string prefix = composition.document.id.value + ":" +
            layer.id + ":";
        for (auto& packet : geometry.packets) {
            transform_packet(packet, layer.transform,
                             std::clamp(layer.opacity, 0.0F, 1.0F), prefix);
            result.packets.push_back(std::move(packet));
        }
    }
    calculate_bounds(result);
    return result;
}

} // namespace

VisualCompositionDrawResult resolve_visual_composition(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComposition& composition) {
    std::unordered_set<std::string> active_components;
    return resolve_composition(project_root, manifest, composition, {},
                               active_components);
}

VisualCompositionDrawResult resolve_visual_component(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComponent& component,
    const project::VisualComponentInstance& instance) {
    std::unordered_set<std::string> active_components;
    return resolve_component_internal(project_root, manifest, component,
                                      instance, active_components);
}

VisualCompositionDrawResult resolve_animated_visual_component(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::VisualComponent& component,
    const project::VisualComponentInstance& instance,
    const std::string_view node_id,
    const project::EvaluationResult& evaluation) {
    VisualCompositionDrawResult result;
    if (!evaluation.ok()) {
        append_errors(result, evaluation.errors);
        return result;
    }
    const auto base = project::resolve_visual_component_instance(
        component, instance);
    if (!base.ok()) {
        append_errors(result, base.errors);
        return result;
    }
    auto animated = instance;
    for (const auto& property : evaluation.properties) {
        if (property.binding.node_id != node_id ||
            property.binding.component_id != component.document.id.value)
            continue;
        const auto parameter = std::ranges::find_if(
            base.parameters, [&](const auto& candidate) {
                return candidate.id == property.binding.property_id &&
                    candidate.animatable;
            });
        if (parameter == base.parameters.end()) {
            result.errors.push_back("animation targets an unknown or static visual parameter: " +
                                    property.binding.property_id);
            continue;
        }
        auto value = std::visit(
            [](const auto& source) -> project::VisualParameterValue {
                return source;
            }, property.value);
        if (property.composition ==
            project::AnimationComposition::additive) {
            bool composed = false;
            if (auto* target = std::get_if<float>(&value)) {
                if (const auto* initial =
                        std::get_if<float>(&parameter->value)) {
                    *target += *initial;
                    composed = true;
                }
            } else if (auto* target = std::get_if<core::Vec2>(&value)) {
                if (const auto* initial =
                        std::get_if<core::Vec2>(&parameter->value)) {
                    target->x += initial->x;
                    target->y += initial->y;
                    composed = true;
                }
            } else if (auto* target = std::get_if<core::Color>(&value)) {
                if (const auto* initial =
                        std::get_if<core::Color>(&parameter->value)) {
                    target->red += initial->red;
                    target->green += initial->green;
                    target->blue += initial->blue;
                    target->alpha += initial->alpha;
                    composed = true;
                }
            }
            if (!composed) {
                result.errors.push_back(
                    "visual parameter does not support additive animation: " +
                    property.binding.property_id);
                continue;
            }
        }
        const auto existing = std::ranges::find_if(
            animated.overrides, [&](const auto& candidate) {
                return candidate.parameter_id == parameter->id;
            });
        if (existing == animated.overrides.end())
            animated.overrides.push_back({parameter->id, std::move(value)});
        else existing->value = std::move(value);
    }
    if (!result.errors.empty()) return result;
    return resolve_visual_component(
        project_root, manifest, component, animated);
}

} // namespace fabric::render
