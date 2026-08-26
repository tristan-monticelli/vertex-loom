#include "fabric/render/map_preview.hpp"

#include "fabric/project/animation.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/render/visual_composition_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace fabric::render {
namespace {

void append_errors(MapPreviewResult& result,
                   const std::vector<project::Error>& errors) {
    for (const auto& error : errors)
        result.errors.push_back(error.field + ": " + error.message);
}

core::Vec2 apply_transform(core::Vec2 point, const core::Transform& transform) {
    point.x -= transform.pivot.x;
    point.y -= transform.pivot.y;
    point.x *= transform.scale.x;
    point.y *= transform.scale.y;
    const auto radians = transform.rotation_degrees *
        0.017453292519943295F;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    return {point.x * cosine - point.y * sine + transform.position.x,
            point.x * sine + point.y * cosine + transform.position.y};
}

core::Vec2 apply_node_transform(
    core::Vec2 point, const project::EntityDefinition& entity,
    const std::size_t node_index, const core::Transform& instance_transform) {
    const auto& node = entity.nodes[node_index];
    point = apply_transform(point, node.transform);
    if (node.parent) {
        const auto parent = std::ranges::find_if(
            entity.nodes, [&](const auto& candidate) {
                return candidate.id == *node.parent;
            });
        if (parent != entity.nodes.end())
            return apply_node_transform(
                point, entity,
                static_cast<std::size_t>(
                    std::distance(entity.nodes.begin(), parent)),
                instance_transform);
    }
    return apply_transform(point, instance_transform);
}

void transform_packet(VectorDrawPacket& packet,
                      const project::EntityDefinition& entity,
                      const std::size_t node_index,
                      const core::Transform& instance_transform) {
    const auto transform = [&](const core::Vec2 point) {
        return apply_node_transform(
            point, entity, node_index, instance_transform);
    };
    for (auto& point : packet.outline) point = transform(point);
    for (auto& point : packet.fill_vertices) point = transform(point);
}

std::vector<project::MapProperty> effective_properties(
    const project::MapDocument& map, const project::MapInstance& instance) {
    std::vector<project::MapProperty> properties;
    if (instance.prefab) {
        const auto prefab = std::ranges::find_if(
            map.prefabs, [&](const auto& candidate) {
                return candidate.id == instance.prefab->id.value;
            });
        if (prefab != map.prefabs.end()) properties = prefab->overrides;
    }
    for (const auto& property : instance.properties) {
        const auto existing = std::ranges::find_if(
            properties, [&](const auto& candidate) {
                return candidate.id == property.id;
            });
        if (existing == properties.end()) properties.push_back(property);
        else *existing = property;
    }
    return properties;
}

std::optional<core::ResourceId> entity_id(
    const project::MapDocument& map, const project::MapInstance& instance) {
    if (instance.entity) return instance.entity->id;
    if (!instance.prefab) return std::nullopt;
    const auto prefab = std::ranges::find_if(
        map.prefabs, [&](const auto& candidate) {
            return candidate.id == instance.prefab->id.value;
        });
    return prefab == map.prefabs.end()
        ? std::nullopt : std::optional<core::ResourceId>{prefab->entity.id};
}

std::optional<project::EvaluationResult> animation_evaluation(
    const std::filesystem::path& root,
    const project::ProjectManifest& manifest,
    const std::vector<project::MapProperty>& properties,
    const float time, MapPreviewResult& result) {
    const auto property = std::ranges::find_if(
        properties, [](const auto& candidate) {
            return candidate.id == "animation";
        });
    if (property == properties.end()) return std::nullopt;
    const auto* reference =
        std::get_if<project::ResourceReference>(&property->value);
    if (reference == nullptr || reference->expected_type != "animation") {
        result.errors.push_back(
            "instances.animation: expected an animation reference");
        return std::nullopt;
    }
    auto clip = project::load_animation(
        root, manifest, project::animation_document_path(
            manifest, reference->id));
    if (!clip.ok()) {
        append_errors(result, clip.errors);
        return std::nullopt;
    }
    auto evaluation = project::evaluate_animation(*clip.asset, time);
    if (!evaluation.ok()) append_errors(result, evaluation.errors);
    return evaluation;
}

float layer_depth(const project::MapDocument& map,
                  const project::MapInstance& instance) {
    const auto layer = std::ranges::find_if(
        map.layers, [&](const auto& candidate) {
            return candidate.id == instance.layer_id;
        });
    return layer == map.layers.end() ? 0.0F : layer->depth;
}

bool layer_visible(const project::MapDocument& map,
                   const project::MapInstance& instance) {
    const auto layer = std::ranges::find_if(
        map.layers, [&](const auto& candidate) {
            return candidate.id == instance.layer_id;
        });
    return layer != map.layers.end() && layer->visible;
}

} // namespace

MapPreviewResult resolve_map_preview(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const project::MapDocument& map,
    const float animation_time) {
    MapPreviewResult result;
    struct SortedPacket {
        VectorDrawPacket packet;
        float depth{};
        float z{};
    };
    std::vector<SortedPacket> sorted;
    for (const auto& instance : map.instances) {
        if (!layer_visible(map, instance)) continue;
        const auto resolved_entity_id = entity_id(map, instance);
        if (!resolved_entity_id) {
            result.errors.push_back("instance " + instance.id +
                                    ": entity could not be resolved");
            continue;
        }
        auto loaded_entity = project::load_entity(
            project_root, manifest,
            project::entity_document_path(manifest, *resolved_entity_id));
        if (!loaded_entity.ok()) {
            append_errors(result, loaded_entity.errors);
            continue;
        }
        const auto properties = effective_properties(map, instance);
        const auto evaluation = animation_evaluation(
            project_root, manifest, properties, animation_time, result);
        const auto& entity = *loaded_entity.entity;
        for (std::size_t node_index = 0; node_index < entity.nodes.size();
             ++node_index) {
            const auto& node = entity.nodes[node_index];
            if (!node.visible) continue;
            VectorGeometryResult geometry;
            if (node.drawable.kind == project::EntityDrawableKind::vector &&
                node.drawable.resource) {
                auto loaded = project::load_vector_asset(
                    project_root, manifest,
                    project::vector_document_path(
                        manifest, node.drawable.resource->id));
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
            } else if (node.drawable.kind ==
                           project::EntityDrawableKind::texture &&
                       node.drawable.resource) {
                auto texture = project::load_texture_asset(
                    project_root, manifest,
                    project::texture_document_path(
                        manifest, node.drawable.resource->id));
                if (!texture.ok()) {
                    append_errors(result, texture.errors);
                    continue;
                }
                geometry = build_raster_view_draw_packets({
                    .node_id = node.id,
                    .texture = *node.drawable.resource,
                    .source_width = texture.asset->width,
                    .source_height = texture.asset->height,
                    .pixels_per_unit =
                        static_cast<float>(manifest.pixels_per_unit),
                    .view = texture.asset->view});
            } else if (node.drawable.kind ==
                           project::EntityDrawableKind::visual_component &&
                       node.drawable.resource) {
                auto component = project::load_visual_component(
                    project_root, manifest,
                    project::visual_component_document_path(
                        manifest, node.drawable.resource->id));
                if (!component.ok()) {
                    append_errors(result, component.errors);
                    continue;
                }
                const auto component_instance =
                    node.drawable.component_instance.value_or(
                        project::VisualComponentInstance{});
                auto visual = evaluation && evaluation->ok()
                    ? resolve_animated_visual_component(
                          project_root, manifest, *component.asset,
                          component_instance, node.id, *evaluation)
                    : resolve_visual_component(
                          project_root, manifest, *component.asset,
                          component_instance);
                if (!visual.ok()) {
                    result.errors.insert(result.errors.end(),
                        visual.errors.begin(), visual.errors.end());
                    continue;
                }
                geometry.packets = std::move(visual.packets);
            }
            result.errors.insert(result.errors.end(),
                                 geometry.errors.begin(),
                                 geometry.errors.end());
            for (auto& packet : geometry.packets) {
                transform_packet(packet, entity, node_index,
                                 instance.transform);
                packet.node_id = instance.id + ":" + node.id + ":" +
                    packet.node_id;
                sorted.push_back({std::move(packet), layer_depth(map, instance),
                                  node.z_order});
            }
        }
    }
    std::ranges::stable_sort(sorted, [](const auto& left, const auto& right) {
        if (left.depth != right.depth) return left.depth < right.depth;
        if (left.z != right.z) return left.z < right.z;
        return left.packet.node_id < right.packet.node_id;
    });
    result.packets.reserve(sorted.size());
    for (auto& packet : sorted)
        result.packets.push_back(std::move(packet.packet));
    return result;
}

} // namespace fabric::render
