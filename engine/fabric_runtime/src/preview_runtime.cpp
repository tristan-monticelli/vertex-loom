#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/render/vector_geometry.hpp"
#include "fabric/render/visual_composition_renderer.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/entity_transformation.hpp"
#include "fabric/project/animation_ik.hpp"
#include "fabric/project/audio.hpp"
#include "fabric/project/behavior_graph.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/map_chunk_index.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/runtime/replay_player.hpp"
#include "fabric/runtime/camera2d.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>

namespace fabric::runtime {

struct RuntimePacketBounds {
    core::Vec2 minimum;
    core::Vec2 maximum;
};

struct PreviewRuntime::Impl {
    struct TextureSource {
        std::filesystem::path path;
        std::uint32_t width{};
        std::uint32_t height{};
        std::optional<project::RasterView> view;
    };

    struct PacketBaseTransform {
        core::Vec2 local_position;
        float rotation_degrees{};
        core::Vec2 scale{1.0F, 1.0F};
        core::Vec2 world_origin;
    };

    struct PacketSortKey {
        float layer_depth{};
        float z_order{};
    };

    struct EntitySimulation {
        core::ResourceId entity_id;
        std::optional<project::DeformationMesh> mesh;
        std::optional<project::XpbdSystem> xpbd;
        std::vector<core::Vec2> previous_xpbd_positions;
        std::vector<core::Vec2> interpolated_xpbd_positions;
        std::vector<project::EntityNode> nodes;
        std::vector<project::AnimationConstraint> constraints;
        std::vector<project::FabrikChainDefinition> ik_chains;
        std::vector<project::DeformationPose> poses;
        core::Transform instance_transform;
        float layer_depth{};
        std::optional<BehaviorEvaluator> behavior;
        std::map<std::string, project::BehaviorValue> behavior_properties;
    };

    struct AnimatedVisualComponent {
        project::VisualComponent component;
        project::VisualComponentInstance instance;
        project::EntityDefinition entity;
        std::size_t node_index{};
        core::Transform instance_transform;
        std::string instance_id;
        std::string node_id;
    };

    struct MechanicVisualBinding {
        std::string body_node_id;
        core::Vec2 initial_position;
        float initial_rotation_degrees{};
    };

    struct MechanicInstanceSimulation {
        physics::MechanicSimulation simulation;
        std::optional<MechanicVisualBinding> visual_binding;
    };

    SDL_Window* window{};
    SDL_GLContext context{};
    render::OpenGLVectorRenderer renderer;
    PcmAudioMixer audio_mixer;
    PcmAudioDevice audio_device;
    std::optional<PcmWavClip> audio_clip;
    std::string audio_bus{"master"};
    float audio_bus_gain{1.0F};
    float audio_gain{1.0F};
    float audio_pan{};
    bool audio_loop{};
    SDL_GameController* controller{};
    Camera2D camera;
    std::vector<render::VectorDrawPacket> packets;
    std::vector<render::VectorDrawPacket> last_frame_packets;
    std::unordered_map<std::string, TextureSource> texture_sources;
    std::unordered_map<std::string, render::OpenGLTextureHandle> texture_handles;
    std::unordered_map<std::string, project::VectorAsset> vector_assets;
    render::VectorGeometryCache vector_geometry_cache;
    std::unordered_map<std::string, project::AnimationClip> animation_clips;
    std::unordered_map<std::string, std::string> animation_instances;
    std::unordered_map<std::string, project::AnimationStateMachine> animation_state_machines;
    std::unordered_map<std::string, std::vector<project::AnimationParameter>> animation_parameters;
    mutable bool evaluation_cache_valid{};
    mutable float evaluation_cache_time{};
    mutable std::unordered_map<std::string,
        std::optional<project::EvaluationResult>> animation_evaluation_cache;
    mutable std::unordered_map<std::string,
        std::optional<std::vector<project::EntityNode>>> node_evaluation_cache;
    std::unordered_map<std::string, PacketBaseTransform> packet_base_transforms;
    std::unordered_map<std::string, PacketSortKey> packet_sort_keys;
    std::vector<RuntimePacketBounds> packet_bounds;
    std::vector<bool> packet_bounds_dynamic;
    std::unordered_map<std::string, core::Rect> trigger_actor_bounds;
    std::unordered_map<std::string, EntitySimulation> entity_simulations;
    std::unordered_map<std::string, AnimatedVisualComponent>
        animated_visual_components;
    std::unordered_map<std::string, MechanicInstanceSimulation>
        mechanic_instances;
    std::vector<GameplayEvent> gameplay_events;
    std::vector<AnimationMarkerEvent> animation_marker_events;
    std::vector<BehaviorAction> behavior_actions;
    std::vector<std::pair<std::string, core::ResourceId>>
        pending_transformations;
    project::MapChunkIndex chunk_index;
    std::unordered_map<std::string, std::vector<std::size_t>> packet_indices_by_instance;
    bool chunk_index_ready{};
    bool sdl_initialized{};
    std::filesystem::path project_root;
    const project::ProjectManifest* manifest{};
    std::vector<std::string>* errors{};

    [[nodiscard]] bool transform_entity_instance(
        const std::string& instance_id,
        const core::ResourceId& transformation_id);
    void flush_transformations() {
        auto pending = std::move(pending_transformations);
        pending_transformations.clear();
        for (const auto& [instance_id, transformation_id] : pending)
            static_cast<void>(transform_entity_instance(
                instance_id, transformation_id));
    }

    render::VectorDrawPacket apply_mechanic_pose(
        render::VectorDrawPacket packet, const std::string& instance_id) const {
        const auto mechanic = mechanic_instances.find(instance_id);
        if (mechanic == mechanic_instances.end() ||
            !mechanic->second.visual_binding) return packet;
        const auto& binding = *mechanic->second.visual_binding;
        const auto body = std::ranges::find(
            mechanic->second.simulation.body_states(), binding.body_node_id,
            &physics::MechanicBodyState::node_id);
        if (body == mechanic->second.simulation.body_states().end()) return packet;
        const auto radians =
            (body->rotation_degrees - binding.initial_rotation_degrees) *
            0.017453292519943295F;
        const auto cosine = std::cos(radians);
        const auto sine = std::sin(radians);
        const auto transform_point = [&](core::Vec2& point) {
            point.x -= binding.initial_position.x;
            point.y -= binding.initial_position.y;
            const auto rotated_x = point.x * cosine - point.y * sine;
            const auto rotated_y = point.x * sine + point.y * cosine;
            point.x = rotated_x + body->position.x;
            point.y = rotated_y + body->position.y;
        };
        for (auto& point : packet.outline) transform_point(point);
        for (auto& point : packet.fill_vertices) transform_point(point);
        return packet;
    }

    void begin_evaluation_cache(const float time) const {
        if (evaluation_cache_valid && evaluation_cache_time == time) return;
        evaluation_cache_valid = true;
        evaluation_cache_time = time;
        animation_evaluation_cache.clear();
        node_evaluation_cache.clear();
    }

    void apply_behavior_actions(const std::string& instance_id,
                                const std::vector<BehaviorAction>& actions,
                                const float fixed_step_seconds) {
        const auto simulation = entity_simulations.find(instance_id);
        if (simulation == entity_simulations.end()) return;
        std::vector<core::ResourceId> transformations;
        for (const auto& action : actions) {
            if (action.kind == BehaviorActionKind::move) {
                const auto vector = std::ranges::find(
                    action.properties, "vector",
                    &project::BehaviorNodeProperty::id);
                if (vector != action.properties.end())
                    if (const auto* value = std::get_if<core::Vec2>(&vector->value)) {
                        simulation->second.instance_transform.position.x +=
                            value->x * fixed_step_seconds;
                        simulation->second.instance_transform.position.y +=
                            value->y * fixed_step_seconds;
                    }
            } else if (action.kind == BehaviorActionKind::set_property) {
                const auto target = std::ranges::find(
                    action.properties, "target", &project::BehaviorNodeProperty::id);
                const auto value = std::ranges::find(
                    action.properties, "value", &project::BehaviorNodeProperty::id);
                if (target != action.properties.end() && value != action.properties.end())
                    if (const auto* id = std::get_if<std::string>(&target->value))
                        simulation->second.behavior_properties[*id] = value->value;
            } else if (action.kind == BehaviorActionKind::transform_entity) {
                const auto property = std::ranges::find(
                    action.properties, "transformation",
                    &project::BehaviorNodeProperty::id);
                if (property != action.properties.end())
                    if (const auto* reference =
                            std::get_if<project::ResourceReference>(
                                &property->value))
                        transformations.push_back(reference->id);
            }
            behavior_actions.push_back(action);
        }
        for (auto& transformation : transformations)
            pending_transformations.emplace_back(
                instance_id, std::move(transformation));
    }
};

namespace {

void append_errors(std::vector<std::string>& output,
                   const std::vector<project::Error>& errors) {
    for (const auto& error : errors)
        output.push_back(error.field + ": " + error.message);
}

core::Vec2 apply_transform(core::Vec2 point, const core::Transform& transform) {
    point.x = (point.x - transform.pivot.x) * transform.scale.x;
    point.y = (point.y - transform.pivot.y) * transform.scale.y;
    const auto radians = transform.rotation_degrees * 0.017453292519943295F;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    return {point.x * cosine - point.y * sine + transform.pivot.x + transform.position.x,
            point.x * sine + point.y * cosine + transform.pivot.y + transform.position.y};
}

core::Vec2 clamp_vec(const core::Vec2 value,
                    const std::optional<core::Vec2>& minimum,
                    const std::optional<core::Vec2>& maximum) {
    return {std::clamp(value.x, minimum ? minimum->x : value.x,
                       maximum ? maximum->x : value.x),
            std::clamp(value.y, minimum ? minimum->y : value.y,
                       maximum ? maximum->y : value.y)};
}

bool resolve_constraints(std::vector<project::EntityNode>& nodes,
                         const std::vector<project::AnimationConstraint>& constraints) {
    const auto ordered = project::order_animation_constraints(constraints);
    if (ordered.size() != constraints.size()) return false;
    const auto find = [&](const std::string& id) {
        return std::find_if(nodes.begin(), nodes.end(),
            [&](const auto& node) { return node.id == id; });
    };
    for (const auto* constraint : ordered) {
        auto target = find(constraint->target_node);
        const auto source = find(constraint->source_node);
        if (target == nodes.end() || source == nodes.end()) return false;
        if (constraint->kind == project::AnimationConstraintKind::copy_transform) {
            if (constraint->constrain_position) target->transform.position = source->transform.position;
            if (constraint->constrain_rotation) target->transform.rotation_degrees = source->transform.rotation_degrees;
            if (constraint->constrain_scale) target->transform.scale = source->transform.scale;
        } else if (constraint->kind == project::AnimationConstraintKind::look_at) {
            const auto delta = core::Vec2{source->transform.position.x - target->transform.position.x,
                                          source->transform.position.y - target->transform.position.y};
            if (constraint->constrain_rotation) {
                if (std::abs(delta.x) > 1.0e-6F || std::abs(delta.y) > 1.0e-6F)
                    target->transform.rotation_degrees = std::atan2(delta.y, delta.x) *
                        180.0F / 3.14159265358979323846F;
            }
            if (constraint->constrain_position) target->transform.position = source->transform.position;
        } else {
            if (constraint->constrain_position)
                target->transform.position = clamp_vec(target->transform.position,
                    constraint->min_position, constraint->max_position);
            if (constraint->constrain_rotation) {
                if (constraint->min_rotation_degrees)
                    target->transform.rotation_degrees = std::max(
                        target->transform.rotation_degrees, *constraint->min_rotation_degrees);
                if (constraint->max_rotation_degrees)
                    target->transform.rotation_degrees = std::min(
                        target->transform.rotation_degrees, *constraint->max_rotation_degrees);
            }
            if (constraint->constrain_scale)
                target->transform.scale = clamp_vec(target->transform.scale,
                    constraint->min_scale, constraint->max_scale);
        }
    }
    return true;
}

bool resolve_ik_chains(std::vector<project::EntityNode>& nodes,
                       const std::vector<project::FabrikChainDefinition>& chains) {
    const auto find = [&](const std::string& id) {
        return std::find_if(nodes.begin(), nodes.end(),
            [&](const auto& node) { return node.id == id; });
    };
    for (const auto& chain : chains) {
        std::vector<core::Vec2> joints;
        joints.reserve(chain.joints.size());
        for (const auto& joint_id : chain.joints) {
            const auto joint = find(joint_id);
            if (joint == nodes.end()) return false;
            joints.push_back(joint->transform.position);
        }
        const auto target = find(chain.target_node);
        if (target == nodes.end()) return false;
        const auto result = project::solve_fabrik({
            .joints = std::move(joints),
            .target = target->transform.position,
            .max_iterations = chain.max_iterations,
            .tolerance = chain.tolerance});
        if (!result.ok() || result.joints.size() != chain.joints.size()) return false;
        for (std::size_t index = 0; index < chain.joints.size(); ++index) {
            const auto joint = find(chain.joints[index]);
            if (joint == nodes.end()) return false;
            joint->transform.position = result.joints[index];
        }
    }
    return true;
}

core::Vec2 apply_node_transform(core::Vec2, const project::EntityDefinition&,
                                std::size_t, const core::Transform&);
void transform_packet(render::VectorDrawPacket&,
                      const project::EntityDefinition&, std::size_t,
                      const core::Transform&);
void apply_material(render::VectorDrawPacket&,
                    const project::MaterialDefinition&);
void generate_planar_uvs(render::VectorDrawPacket&);
RuntimePacketBounds packet_bounds_for(const render::VectorDrawPacket&);

} // namespace

bool PreviewRuntime::Impl::transform_entity_instance(
    const std::string& instance_id,
    const core::ResourceId& transformation_id) {
    const auto source = entity_simulations.find(instance_id);
    if (source == entity_simulations.end() || !manifest || !errors) return false;
    const auto loaded_transformation = project::load_entity_transformation(
        project_root, *manifest,
        project::entity_transformation_document_path(
            *manifest, transformation_id));
    if (!loaded_transformation.ok()) {
        append_errors(*errors, loaded_transformation.errors);
        return false;
    }
    const auto& transformation = *loaded_transformation.asset;
    if (source->second.entity_id != transformation.source_entity.id) {
        errors->push_back("transformation source does not match instance entity: " +
                          instance_id);
        return false;
    }
    auto loaded_entity = project::load_entity(
        project_root, *manifest,
        project::entity_document_path(
            *manifest, transformation.destination_entity.id));
    if (!loaded_entity.ok()) {
        append_errors(*errors, loaded_entity.errors);
        return false;
    }
    auto destination = std::move(*loaded_entity.entity);
    if (!resolve_constraints(destination.nodes, destination.constraints) ||
        !resolve_ik_chains(destination.nodes, destination.ik_chains)) {
        errors->push_back("transformation destination constraints are invalid");
        return false;
    }

    EntitySimulation candidate{
        .entity_id = destination.document.id,
        .mesh = destination.deformation_mesh,
        .xpbd = destination.xpbd,
        .nodes = destination.nodes,
        .constraints = destination.constraints,
        .ik_chains = destination.ik_chains,
        .instance_transform = transformation.policy.world_transform ==
                project::TransferMode::preserve
            ? source->second.instance_transform : core::Transform{},
        .layer_depth = source->second.layer_depth};
    if (destination.behavior) {
        auto behavior = project::load_behavior_graph(
            project_root, *manifest,
            project::behavior_graph_document_path(
                *manifest, destination.behavior->id));
        if (!behavior.ok()) {
            append_errors(*errors, behavior.errors);
            return false;
        }
        candidate.behavior.emplace(std::move(*behavior.asset));
    }
    if (candidate.xpbd) {
        if (transformation.policy.physics == project::TransferMode::preserve &&
            source->second.xpbd &&
            source->second.xpbd->particles.size() ==
                candidate.xpbd->particles.size()) {
            *candidate.xpbd = *source->second.xpbd;
        } else if (transformation.policy.physics ==
                       project::TransferMode::preserve &&
                   transformation.policy.incompatible_values ==
                       project::TransferMode::error) {
            errors->push_back(
                "transformation cannot preserve incompatible physics state");
            return false;
        }
        for (const auto& particle : candidate.xpbd->particles) {
            candidate.previous_xpbd_positions.push_back(particle.position);
            candidate.interpolated_xpbd_positions.push_back(particle.position);
        }
    }
    for (const auto& node : candidate.nodes)
        candidate.poses.push_back({.node_id = node.id,
                                   .transform = node.transform});

    if (transformation.policy.properties == project::TransferMode::preserve) {
        candidate.behavior_properties = source->second.behavior_properties;
    } else if (transformation.policy.properties ==
               project::TransferMode::mapping) {
        for (const auto& mapping : transformation.policy.mappings) {
            if (mapping.domain != project::TransferDomain::property) continue;
            const auto value = source->second.behavior_properties.find(mapping.source);
            if (value == source->second.behavior_properties.end()) {
                if (transformation.policy.incompatible_values ==
                    project::TransferMode::error) {
                    errors->push_back("transformation property mapping is missing: " +
                                      mapping.source);
                    return false;
                }
                continue;
            }
            candidate.behavior_properties[mapping.target] = value->second;
        }
    }

    std::optional<std::string> next_animation;
    if (const auto current = animation_instances.find(instance_id);
        current != animation_instances.end()) next_animation = current->second;
    if (transformation.policy.animation == project::TransferMode::reset) {
        next_animation.reset();
    } else if (transformation.policy.animation == project::TransferMode::mapping &&
               next_animation) {
        const auto mapping = std::ranges::find_if(
            transformation.policy.mappings, [&](const auto& item) {
                return item.domain == project::TransferDomain::animation &&
                    item.source == *next_animation;
            });
        if (mapping == transformation.policy.mappings.end() ||
            !animation_clips.contains(mapping->target)) {
            if (transformation.policy.incompatible_values ==
                project::TransferMode::error) {
                errors->push_back("transformation animation mapping is invalid");
                return false;
            }
            next_animation.reset();
        } else next_animation = mapping->target;
    }

    std::string destination_instance_id = instance_id;
    if (transformation.policy.instance_id == project::TransferMode::reset) {
        destination_instance_id = instance_id + "-transformed";
        for (std::size_t suffix = 2U;
             entity_simulations.contains(destination_instance_id); ++suffix)
            destination_instance_id = instance_id + "-transformed-" +
                std::to_string(suffix);
    }

    std::vector<render::VectorDrawPacket> candidate_packets;
    std::unordered_map<std::string, PacketBaseTransform> candidate_bases;
    std::unordered_map<std::string, PacketSortKey> candidate_sorts;
    std::unordered_map<std::string, AnimatedVisualComponent>
        candidate_components;
    const auto ensure_texture = [&](const project::ResourceReference& reference) {
        if (texture_sources.contains(reference.id.value)) return true;
        auto texture = project::load_texture_asset(
            project_root, *manifest,
            project::texture_document_path(*manifest, reference.id));
        if (!texture.ok()) {
            append_errors(*errors, texture.errors);
            return false;
        }
        texture_sources.emplace(reference.id.value, TextureSource{
            .path = project_root / texture.asset->source,
            .width = texture.asset->width,
            .height = texture.asset->height,
            .view = texture.asset->view});
        return true;
    };
    const auto append_packet = [&](render::VectorDrawPacket packet,
                                   const project::EntityNode& node,
                                   const std::size_t node_index,
                                   const std::string& packet_id) {
        transform_packet(packet, destination, node_index,
                         candidate.instance_transform);
        packet.node_id = packet_id;
        candidate_bases.emplace(packet.node_id, PacketBaseTransform{
            .local_position = node.transform.position,
            .rotation_degrees = node.transform.rotation_degrees,
            .scale = node.transform.scale,
            .world_origin = apply_node_transform(
                {0.0F, 0.0F}, destination, node_index,
                candidate.instance_transform)});
        candidate_sorts.emplace(packet.node_id, PacketSortKey{
            candidate.layer_depth, node.z_order});
        candidate_packets.push_back(std::move(packet));
    };
    for (std::size_t node_index = 0; node_index < destination.nodes.size();
         ++node_index) {
        const auto& node = destination.nodes[node_index];
        if (!node.visible) continue;
        if (node.drawable.kind == project::EntityDrawableKind::vector &&
            node.drawable.resource) {
            const auto vector_id = node.drawable.resource->id.value;
            project::VectorAsset drawable;
            if (const auto cached = vector_assets.find(vector_id);
                cached != vector_assets.end()) drawable = cached->second;
            else {
                auto loaded = project::load_vector_asset(
                    project_root, *manifest,
                    project::vector_document_path(
                        *manifest, node.drawable.resource->id));
                if (!loaded.ok()) { append_errors(*errors, loaded.errors); return false; }
                drawable = std::move(*loaded.asset);
                if (drawable.source_kind == project::VectorSourceKind::linked_svg) {
                    auto converted = render::convert_svg_to_native(
                        project_root / drawable.source, drawable.document.id,
                        drawable.document.name);
                    if (!converted.ok()) {
                        append_errors(*errors, converted.errors);
                        return false;
                    }
                    drawable = std::move(*converted.asset);
                }
                vector_assets.emplace(vector_id, drawable);
            }
            auto geometry = vector_geometry_cache.get_or_build(drawable);
            if (!geometry.ok()) {
                errors->insert(errors->end(), geometry.errors.begin(),
                               geometry.errors.end());
                return false;
            }
            std::optional<project::MaterialDefinition> material;
            if (node.drawable.material) {
                auto loaded = project::load_material(
                    project_root, *manifest,
                    project::material_document_path(
                        *manifest, node.drawable.material->id));
                if (!loaded.ok()) { append_errors(*errors, loaded.errors); return false; }
                material = std::move(*loaded.asset);
            }
            for (auto& packet : geometry.packets) {
                if (packet.image_fill && !ensure_texture(packet.image_fill->texture))
                    return false;
                if (material && material->texture &&
                    !ensure_texture(*material->texture)) return false;
                if (material) apply_material(packet, *material);
                if (material && material->texture) {
                    packet.image_fill = project::VectorImageFill{
                        .texture = *material->texture,
                        .transform = material->uv_transform,
                        .opacity = material->opacity};
                    packet.fill_color.reset();
                    generate_planar_uvs(packet);
                }
                const auto packet_id = destination_instance_id + ":" + node.id +
                    ":" + packet.node_id;
                append_packet(std::move(packet), node, node_index, packet_id);
            }
        } else if (node.drawable.kind ==
                       project::EntityDrawableKind::visual_component &&
                   node.drawable.resource) {
            auto component = project::load_visual_component(
                project_root, *manifest,
                project::visual_component_document_path(
                    *manifest, node.drawable.resource->id));
            if (!component.ok()) { append_errors(*errors, component.errors); return false; }
            const auto component_instance = node.drawable.component_instance.value_or(
                project::VisualComponentInstance{});
            candidate_components.emplace(destination_instance_id + ":" + node.id,
                AnimatedVisualComponent{
                    .component = *component.asset,
                    .instance = component_instance,
                    .entity = destination,
                    .node_index = node_index,
                    .instance_transform = candidate.instance_transform,
                    .instance_id = destination_instance_id,
                    .node_id = node.id});
            auto visual = render::resolve_visual_component(
                project_root, *manifest, *component.asset, component_instance);
            if (!visual.ok()) {
                errors->insert(errors->end(), visual.errors.begin(),
                               visual.errors.end());
                return false;
            }
            for (auto& packet : visual.packets) {
                if (packet.image_fill && !ensure_texture(packet.image_fill->texture))
                    return false;
                const auto packet_id = destination_instance_id + ":" + node.id +
                    ":" + packet.node_id;
                append_packet(std::move(packet), node, node_index, packet_id);
            }
        } else if (node.drawable.kind == project::EntityDrawableKind::texture &&
                   node.drawable.resource) {
            if (!ensure_texture(*node.drawable.resource)) return false;
            const auto& texture = texture_sources.at(
                node.drawable.resource->id.value);
            auto geometry = render::build_raster_view_draw_packets({
                .node_id = node.id,
                .texture = *node.drawable.resource,
                .source_width = texture.width,
                .source_height = texture.height,
                .pixels_per_unit = static_cast<float>(manifest->pixels_per_unit),
                .view = texture.view});
            if (!geometry.ok()) {
                errors->insert(errors->end(), geometry.errors.begin(),
                               geometry.errors.end());
                return false;
            }
            auto packet = std::move(geometry.packets.front());
            if (node.drawable.material) {
                auto loaded = project::load_material(
                    project_root, *manifest,
                    project::material_document_path(
                        *manifest, node.drawable.material->id));
                if (!loaded.ok()) {
                    append_errors(*errors, loaded.errors);
                    return false;
                }
                apply_material(packet, *loaded.asset);
            }
            append_packet(std::move(packet), node, node_index,
                          destination_instance_id + ":" + node.id);
        }
    }

    entity_simulations.erase(source);
    entity_simulations.emplace(destination_instance_id, std::move(candidate));
    animation_instances.erase(instance_id);
    if (next_animation)
        animation_instances[destination_instance_id] = *next_animation;
    animation_state_machines.erase(instance_id);
    if (destination.animation_state_machine)
        animation_state_machines[destination_instance_id] =
            *destination.animation_state_machine;
    animation_parameters.erase(instance_id);
    animation_parameters.emplace(destination_instance_id,
                                 std::vector<project::AnimationParameter>{});
    if (transformation.policy.physics == project::TransferMode::reset)
        mechanic_instances.erase(instance_id);
    else if (destination_instance_id != instance_id) {
        auto mechanic = mechanic_instances.extract(instance_id);
        if (!mechanic.empty()) {
            mechanic.key() = destination_instance_id;
            mechanic_instances.insert(std::move(mechanic));
        }
    }

    const auto prefix = instance_id + ":";
    std::vector<render::VectorDrawPacket> retained_packets;
    std::vector<RuntimePacketBounds> retained_bounds;
    std::vector<bool> retained_dynamic;
    retained_packets.reserve(packets.size());
    retained_bounds.reserve(packet_bounds.size());
    retained_dynamic.reserve(packet_bounds_dynamic.size());
    for (std::size_t index = 0; index < packets.size(); ++index) {
        if (packets[index].node_id.starts_with(prefix)) continue;
        retained_packets.push_back(std::move(packets[index]));
        if (index < packet_bounds.size())
            retained_bounds.push_back(packet_bounds[index]);
        if (index < packet_bounds_dynamic.size())
            retained_dynamic.push_back(packet_bounds_dynamic[index]);
    }
    packets = std::move(retained_packets);
    packet_bounds = std::move(retained_bounds);
    packet_bounds_dynamic = std::move(retained_dynamic);
    packet_indices_by_instance.clear();
    for (std::size_t index = 0; index < packets.size(); ++index) {
        const auto separator = packets[index].node_id.find(':');
        if (separator != std::string::npos)
            packet_indices_by_instance[
                packets[index].node_id.substr(0, separator)].push_back(index);
    }
    std::erase_if(animated_visual_components, [&](const auto& item) {
        return item.first.starts_with(prefix);
    });
    std::erase_if(packet_base_transforms, [&](const auto& item) {
        return item.first.starts_with(prefix);
    });
    std::erase_if(packet_sort_keys, [&](const auto& item) {
        return item.first.starts_with(prefix);
    });
    animated_visual_components.insert(
        std::make_move_iterator(candidate_components.begin()),
        std::make_move_iterator(candidate_components.end()));
    packet_base_transforms.insert(
        std::make_move_iterator(candidate_bases.begin()),
        std::make_move_iterator(candidate_bases.end()));
    packet_sort_keys.insert(
        std::make_move_iterator(candidate_sorts.begin()),
        std::make_move_iterator(candidate_sorts.end()));
    packets.insert(packets.end(),
                   std::make_move_iterator(candidate_packets.begin()),
                   std::make_move_iterator(candidate_packets.end()));
    std::stable_sort(packets.begin(), packets.end(), [&](const auto& left,
                                                         const auto& right) {
        const auto& left_key = packet_sort_keys.at(left.node_id);
        const auto& right_key = packet_sort_keys.at(right.node_id);
        if (left_key.layer_depth != right_key.layer_depth)
            return left_key.layer_depth < right_key.layer_depth;
        if (left_key.z_order != right_key.z_order)
            return left_key.z_order < right_key.z_order;
        return left.node_id < right.node_id;
    });
    packet_indices_by_instance.clear();
    packet_bounds.clear();
    packet_bounds_dynamic.clear();
    for (std::size_t index = 0; index < packets.size(); ++index) {
        const auto separator = packets[index].node_id.find(':');
        const auto packet_instance = separator == std::string::npos
            ? std::string{} : packets[index].node_id.substr(0, separator);
        if (!packet_instance.empty())
            packet_indices_by_instance[packet_instance].push_back(index);
        packet_bounds.push_back(packet_bounds_for(packets[index]));
        const auto simulation = entity_simulations.find(packet_instance);
        packet_bounds_dynamic.push_back(
            simulation != entity_simulations.end() &&
            (simulation->second.mesh.has_value() ||
             !simulation->second.constraints.empty() ||
             !simulation->second.ik_chains.empty() ||
             animation_instances.contains(packet_instance) ||
             animation_state_machines.contains(packet_instance) ||
             mechanic_instances.contains(packet_instance)));
    }
    evaluation_cache_valid = false;
    chunk_index_ready = false;
    return true;
}

namespace {

void apply_animation_to_nodes(std::vector<project::EntityNode>& nodes,
                              const project::EvaluationResult& animation) {
    for (const auto& property : animation.properties) {
        if (property.binding.component_id != "transform") continue;
        const auto node = std::find_if(nodes.begin(), nodes.end(),
            [&](const auto& candidate) {
                return candidate.id == property.binding.node_id;
            });
        if (node == nodes.end()) continue;
        const bool additive = property.composition ==
            project::AnimationComposition::additive;
        if (property.binding.property_id == "position") {
            if (const auto* value = std::get_if<core::Vec2>(&property.value)) {
                if (additive) {
                    node->transform.position.x += value->x;
                    node->transform.position.y += value->y;
                } else {
                    node->transform.position = *value;
                }
            }
        } else if (property.binding.property_id == "rotationDegrees") {
            if (const auto* value = std::get_if<float>(&property.value)) {
                node->transform.rotation_degrees = additive
                    ? node->transform.rotation_degrees + *value
                    : *value;
            }
        } else if (property.binding.property_id == "scale") {
            if (const auto* value = std::get_if<core::Vec2>(&property.value)) {
                if (additive) {
                    node->transform.scale.x += value->x;
                    node->transform.scale.y += value->y;
                } else {
                    node->transform.scale = *value;
                }
            }
        }
    }
}

struct ResolvedAnimation {
    std::string state_id;
    std::string clip_id;
    float local_time{};
};

std::optional<ResolvedAnimation> resolve_state_machine_animation(
    const project::AnimationStateMachine& machine,
    const std::unordered_map<std::string, project::AnimationClip>& clips,
    const std::vector<project::AnimationParameter>& parameters,
    float time) {
    std::string state_id = machine.initial_state;
    float remaining = std::max(0.0F, time);
    for (std::size_t guard = 0; guard < machine.transitions.size() + 64U; ++guard) {
        const auto* state = project::find_animation_state(machine, state_id);
        if (!state) return std::nullopt;
        const auto clip = clips.find(state->clip.id.value);
        if (clip == clips.end()) return std::nullopt;
        const float duration = std::max(0.0F, clip->second.duration);
        const float local = duration > 0.0F ? std::min(remaining, duration) : 0.0F;
        const float normalized = duration > 0.0F ? local / duration : 1.0F;
        const auto* transition = project::select_animation_transition(
            machine, state_id, parameters, normalized);
        if (transition) {
            state_id = transition->to_state;
            const float transition_time = transition->exit_time
                ? duration * *transition->exit_time : local;
            remaining = std::max(0.0F, remaining - transition_time);
            continue;
        }
        float local_time = local;
        if (clip->second.loop && duration > 0.0F)
            local_time = std::fmod(remaining, duration);
        return ResolvedAnimation{.state_id = state_id,
                                 .clip_id = clip->first,
                                 .local_time = local_time};
    }
    return std::nullopt;
}

core::Vec2 apply_node_transform(core::Vec2 point,
                                const project::EntityDefinition& entity,
                                std::size_t node_index,
                                const core::Transform& instance_transform) {
    const auto& node = entity.nodes[node_index];
    point = apply_transform(point, node.transform);
    if (node.parent) {
        const auto parent = std::find_if(entity.nodes.begin(), entity.nodes.end(),
            [&](const auto& candidate) { return candidate.id == *node.parent; });
        if (parent != entity.nodes.end()) {
            return apply_node_transform(point, entity,
                static_cast<std::size_t>(std::distance(entity.nodes.begin(), parent)),
                instance_transform);
        }
    }
    return apply_transform(point, instance_transform);
}

void transform_packet(render::VectorDrawPacket& packet,
                      const project::EntityDefinition& entity,
                      const std::size_t node_index,
                      const core::Transform& instance_transform) {
    const auto transform = [&](core::Vec2 point) {
        return apply_node_transform(point, entity, node_index, instance_transform);
    };
    for (auto& point : packet.outline) point = transform(point);
    for (auto& point : packet.fill_vertices) point = transform(point);
}

bool deformation_topology_matches(const project::DeformationMesh& mesh,
                                  const render::VectorDrawPacket& packet) {
    if (mesh.vertices.size() != packet.fill_vertices.size()) return false;
    if (mesh.triangles.empty()) return true;
    if (mesh.triangles.size() * 3U != packet.fill_indices.size()) return false;
    for (std::size_t index = 0; index < mesh.triangles.size(); ++index) {
        const auto& triangle = mesh.triangles[index];
        if (packet.fill_indices[index * 3U] != triangle.first ||
            packet.fill_indices[index * 3U + 1U] != triangle.second ||
            packet.fill_indices[index * 3U + 2U] != triangle.third)
            return false;
    }
    return true;
}

void apply_material(render::VectorDrawPacket& packet,
                    const project::MaterialDefinition& material) {
    if (packet.fill_color) {
        packet.fill_color->red *= material.color.red;
        packet.fill_color->green *= material.color.green;
        packet.fill_color->blue *= material.color.blue;
        packet.fill_color->alpha *= material.color.alpha * material.opacity;
    } else {
        auto color = material.color;
        color.alpha *= material.opacity;
        packet.fill_color = color;
    }
    if (material.shader) packet.shader = *material.shader;
}

void generate_planar_uvs(render::VectorDrawPacket& packet) {
    if (packet.fill_vertices.empty()) return;
    auto min_x = packet.fill_vertices.front().x;
    auto max_x = min_x;
    auto min_y = packet.fill_vertices.front().y;
    auto max_y = min_y;
    for (const auto point : packet.fill_vertices) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    const auto width = std::max(max_x - min_x, 1.0e-6F);
    const auto height = std::max(max_y - min_y, 1.0e-6F);
    packet.fill_uv.clear();
    packet.fill_uv.reserve(packet.fill_vertices.size());
    for (const auto point : packet.fill_vertices)
        packet.fill_uv.push_back({(point.x - min_x) / width, (point.y - min_y) / height});
}

bool packet_visible(const render::VectorDrawPacket& packet,
                    const core::Rect& bounds) {
    if (packet.fill_vertices.empty() && packet.outline.empty()) return false;
    const auto& points = packet.fill_vertices.empty() ? packet.outline : packet.fill_vertices;
    auto min_x = points.front().x;
    auto max_x = min_x;
    auto min_y = points.front().y;
    auto max_y = min_y;
    for (const auto point : points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    return max_x >= bounds.origin.x && min_x <= bounds.origin.x + bounds.size.x &&
        max_y >= bounds.origin.y && min_y <= bounds.origin.y + bounds.size.y;
}

bool packet_bounds_visible(const RuntimePacketBounds& packet,
                           const core::Rect& bounds) {
    return packet.maximum.x >= bounds.origin.x &&
        packet.minimum.x <= bounds.origin.x + bounds.size.x &&
        packet.maximum.y >= bounds.origin.y &&
        packet.minimum.y <= bounds.origin.y + bounds.size.y;
}

RuntimePacketBounds packet_bounds_for(
    const render::VectorDrawPacket& packet) {
    const auto& points = packet.fill_vertices.empty() ? packet.outline : packet.fill_vertices;
    if (points.empty()) return {};
    auto result = RuntimePacketBounds{
        .minimum = points.front(), .maximum = points.front()};
    for (const auto point : points) {
        result.minimum.x = std::min(result.minimum.x, point.x);
        result.minimum.y = std::min(result.minimum.y, point.y);
        result.maximum.x = std::max(result.maximum.x, point.x);
        result.maximum.y = std::max(result.maximum.y, point.y);
    }
    return result;
}

bool package_path_is_file(const std::filesystem::path& package_root,
                          const std::filesystem::path& relative_path,
                          std::string& error) {
    std::error_code filesystem_error;
    const auto root = std::filesystem::weakly_canonical(package_root, filesystem_error);
    const auto path = std::filesystem::weakly_canonical(
        package_root / relative_path, filesystem_error);
    const auto relative = filesystem_error ? std::filesystem::path{}
                                            : path.lexically_relative(root);
    if (filesystem_error || path == root || relative.empty() || relative == ".." ||
        relative.string().starts_with(".." + std::string(1, std::filesystem::path::preferred_separator))) {
        error = "map package contains a path outside its root: " + relative_path.generic_string();
        return false;
    }
    if (!std::filesystem::is_regular_file(path, filesystem_error) || filesystem_error) {
        error = "map package is missing file: " + relative_path.generic_string();
        return false;
    }
    return true;
}

} // namespace

PreviewRuntime::PreviewRuntime() : impl_(std::make_unique<Impl>()) {}

PreviewRuntime::~PreviewRuntime() {
    impl_->audio_device.close();
    if (impl_->controller != nullptr) SDL_GameControllerClose(impl_->controller);
    if (impl_->context != nullptr) {
        for (const auto& [id, texture] : impl_->texture_handles) {
            const auto handle = static_cast<GLuint>(texture.handle);
            glDeleteTextures(1, &handle);
        }
        impl_->texture_handles.clear();
    }
    if (impl_->context != nullptr) SDL_GL_DeleteContext(impl_->context);
    if (impl_->window != nullptr) SDL_DestroyWindow(impl_->window);
    if (impl_->sdl_initialized) {
        IMG_Quit();
        SDL_Quit();
    }
}

bool PreviewRuntime::load(const PreviewRuntimeOptions& options) {
    options_ = options;
    manifest_.reset();
    scene_.reset();
    map_.reset();
    replay_.reset();
    replay_player_.reset();
    input_ = {};
    character_.reset();
    triggers_.reset();
    errors_.clear();
    stats_ = {};
    impl_->packets.clear();
    impl_->last_frame_packets.clear();
    impl_->texture_sources.clear();
    impl_->texture_handles.clear();
    impl_->vector_assets.clear();
    impl_->vector_geometry_cache.clear();
    impl_->animation_clips.clear();
    impl_->animation_instances.clear();
    impl_->animation_state_machines.clear();
    impl_->animation_parameters.clear();
    impl_->evaluation_cache_valid = false;
    impl_->animation_evaluation_cache.clear();
    impl_->node_evaluation_cache.clear();
    impl_->packet_base_transforms.clear();
    impl_->packet_sort_keys.clear();
    impl_->packet_bounds.clear();
    impl_->packet_bounds_dynamic.clear();
    impl_->trigger_actor_bounds.clear();
    impl_->entity_simulations.clear();
    impl_->animated_visual_components.clear();
    impl_->mechanic_instances.clear();
    impl_->gameplay_events.clear();
    impl_->animation_marker_events.clear();
    impl_->behavior_actions.clear();
    impl_->pending_transformations.clear();
    impl_->packet_indices_by_instance.clear();
    impl_->chunk_index_ready = false;
    impl_->audio_clip.reset();
    impl_->audio_bus = "master";
    impl_->audio_bus_gain = 1.0F;
    impl_->audio_gain = 1.0F;
    impl_->audio_pan = 0.0F;
    impl_->audio_loop = false;

    const bool valid_map_id = core::ResourceId::is_valid(options_.map_id.value);
    const bool valid_scene_id = options_.scene_id.has_value() &&
        core::ResourceId::is_valid(options_.scene_id->value);
    const bool valid_package_scene_id = options_.package_scene_id.has_value() &&
        core::ResourceId::is_valid(options_.package_scene_id->value);
    const bool package_mode = options_.package_root.has_value();
    if ((!package_mode && options_.project_root.empty()) ||
        (package_mode && !options_.project_root.empty()) ||
        (options_.scene_id.has_value() && !valid_scene_id) ||
        (options_.package_scene_id.has_value() && !valid_package_scene_id) ||
        (!package_mode && options_.package_scene_id.has_value()) ||
        (!package_mode && !options_.scene_id.has_value() && !valid_map_id) ||
        (package_mode && (options_.scene_id.has_value() || valid_map_id)) ||
        (options_.scene_id.has_value() && valid_map_id)) {
        errors_.push_back(package_mode
                              ? "package root requires no map or scene selection"
                              : "project and exactly one valid map or scene id are required");
        return false;
    }

    // This is intentionally before SDL_Init and window creation.
    project::ManifestResult loaded_project;
    if (package_mode) {
        const auto map_manifest_path =
            *options_.package_root / project::map_package_manifest_filename;
        const auto scene_manifest_path =
            *options_.package_root / project::scene_package_manifest_filename;
        std::error_code filesystem_error;
        const bool has_map_manifest = std::filesystem::is_regular_file(
            map_manifest_path, filesystem_error);
        filesystem_error.clear();
        const bool has_scene_manifest = std::filesystem::is_regular_file(
            scene_manifest_path, filesystem_error);
        if (has_map_manifest == has_scene_manifest) {
            errors_.push_back(
                "package requires exactly one map-package.json or scene-package.json manifest");
            return false;
        }
        const auto validate_resources = [&](const auto& resources) {
            for (const auto& resource : resources) {
                std::string path_error;
                if (!package_path_is_file(*options_.package_root,
                                          resource.document_path,
                                          path_error)) {
                    errors_.push_back(path_error);
                    return false;
                }
                for (const auto& payload : resource.payload_paths) {
                    if (!package_path_is_file(*options_.package_root, payload,
                                              path_error)) {
                        errors_.push_back(path_error);
                        return false;
                    }
                }
            }
            return true;
        };
        std::ifstream manifest_file(
            has_map_manifest ? map_manifest_path : scene_manifest_path,
            std::ios::binary);
        const std::string manifest_text{
            std::istreambuf_iterator<char>(manifest_file), std::istreambuf_iterator<char>()};
        if (has_map_manifest) {
            if (options_.package_scene_id) {
                errors_.push_back("a map package cannot select a scene");
                return false;
            }
            const auto package =
                project::parse_map_package_manifest(manifest_text);
            if (!package.ok()) {
                append_errors(errors_, package.errors);
                return false;
            }
            if (!project::runtime_can_load_map_package(*package.manifest)) {
                errors_.push_back(
                    "map package requires an unsupported runtime version or schema");
                return false;
            }
            if (!validate_resources(package.manifest->resources)) return false;
            loaded_project.manifest = project::ProjectManifest{
                .schema_version = project::current_schema_version,
                .id = package.manifest->id,
                .name = package.manifest->name};
            options_.map_id = package.manifest->root_map.id;
        } else {
            const auto package =
                project::parse_scene_package_manifest(manifest_text);
            if (!package.ok()) {
                append_errors(errors_, package.errors);
                return false;
            }
            if (!project::runtime_can_load_scene_package(*package.manifest)) {
                errors_.push_back(
                    "scene package requires an unsupported runtime version or schema");
                return false;
            }
            if (!validate_resources(package.manifest->resources)) return false;
            loaded_project.manifest = project::ProjectManifest{
                .schema_version = project::current_schema_version,
                .id = package.manifest->id,
                .name = package.manifest->name};
            options_.scene_id = options_.package_scene_id.value_or(
                package.manifest->root_scene.id);
        }
        loaded_project.manifest->directories = {};
        options_.project_root = *options_.package_root;
    } else {
        loaded_project = project::load_project(options_.project_root);
        if (!loaded_project.ok()) {
            append_errors(errors_, loaded_project.errors);
            return false;
        }
    }
    if (loaded_project.manifest->runtime) {
        const auto& settings = *loaded_project.manifest->runtime;
        if (!options_.enable_character) {
            options_.enable_character = settings.character.enabled;
        }
        if (!options_.character_spawn && settings.character.spawn) {
            options_.character_spawn = settings.character.spawn;
        }
        if (!options_.character_actions &&
            !settings.character.actions[0].empty() &&
            !settings.character.actions[1].empty() &&
            !settings.character.actions[2].empty()) {
            options_.character_actions = PreviewRuntimeOptions::CharacterActions{
                .left = settings.character.actions[0],
                .right = settings.character.actions[1],
                .jump = settings.character.actions[2]};
        }
        if (!options_.follow_character) {
            options_.follow_character = settings.camera.follow_character;
        }
        if (!options_.camera_limits && settings.camera.limits) {
            options_.camera_limits = settings.camera.limits;
        }
        if (!options_.audio_wav && settings.audio) {
            const auto audio = project::load_audio(
                options_.project_root, *loaded_project.manifest,
                project::audio_document_path(*loaded_project.manifest,
                                             *settings.audio));
            if (!audio.ok()) {
                append_errors(errors_, audio.errors);
                return false;
            }
            if (audio.audio->events.empty()) {
                errors_.push_back("runtime.audio: document has no events");
                return false;
            }
            const auto& event = audio.audio->events.front();
            options_.audio_wav = options_.project_root /
                event.source;
            impl_->audio_bus = event.bus;
            impl_->audio_gain = event.volume;
            impl_->audio_loop = event.loop;
            const auto bus = std::ranges::find(
                audio.audio->buses, event.bus, &project::AudioBus::id);
            impl_->audio_bus_gain = bus == audio.audio->buses.end()
                ? 1.0F : bus->volume;
            if (event.spatial) {
                const auto spatial = resolve_spatial_audio(
                    event.spatial->position.x, event.spatial->position.y,
                    event.spatial->minimum_distance,
                    event.spatial->maximum_distance);
                impl_->audio_gain *= spatial.attenuation;
                impl_->audio_pan = spatial.pan;
            }
        }
    }
    std::optional<project::SceneDocument> loaded_scene;
    std::optional<project::MapDocument> selected_map;
    if (options_.scene_id) {
        auto scene = project::load_scene(
            options_.project_root, *loaded_project.manifest,
            project::scene_document_path(*loaded_project.manifest, *options_.scene_id));
        if (!scene.ok()) {
            append_errors(errors_, scene.errors);
            return false;
        }
        const auto composition = project::compose_scene_maps(
            options_.project_root, *loaded_project.manifest, *scene.asset);
        if (!composition.ok()) {
            append_errors(errors_, composition.errors);
            return false;
        }
        selected_map = *composition.map;
        loaded_scene = std::move(scene.asset);
    } else {
        auto loaded_map = project::load_map(
            options_.project_root, *loaded_project.manifest,
            project::map_document_path(
                *loaded_project.manifest, options_.map_id));
        if (!loaded_map.ok()) {
            append_errors(errors_, loaded_map.errors);
            return false;
        }
        selected_map = std::move(loaded_map.asset);
    }

    std::optional<project::InputDocument> loaded_input;
    if (options_.input_id ||
        (options_.character_actions && options_.input_actions.empty())) {
        const auto input_id = options_.input_id.value_or(core::ResourceId{.value = "default"});
        if (!core::ResourceId::is_valid(input_id.value)) {
            errors_.push_back("input id is invalid");
            return false;
        }
        const auto input_path = project::input_document_path(*loaded_project.manifest, input_id);
        std::error_code input_error;
        const bool input_exists = std::filesystem::exists(
            options_.project_root / input_path, input_error);
        if (input_error) {
            errors_.push_back("input: could not inspect input document");
            return false;
        }
        if (options_.input_id || input_exists) {
            auto input = project::load_input(options_.project_root,
                                             *loaded_project.manifest, input_path);
            if (!input.ok()) {
                append_errors(errors_, input.errors);
                return false;
            }
            loaded_input = std::move(input.input);
        }
    }

    if (options_.replay_id) {
        auto loaded_replay = project::load_replay(
            options_.project_root, *loaded_project.manifest,
            project::replay_document_path(*loaded_project.manifest, *options_.replay_id));
        if (!loaded_replay.ok()) {
            append_errors(errors_, loaded_replay.errors);
            return false;
        }
        replay_ = std::move(loaded_replay.asset);
    }
    if (options_.audio_wav) {
        const auto loaded_audio = load_pcm_wav(*options_.audio_wav);
        if (!loaded_audio.ok()) {
            errors_.push_back("audio: " + loaded_audio.error);
            return false;
        }
        impl_->audio_clip = std::move(loaded_audio.clip);
    }

    manifest_ = std::move(loaded_project.manifest);
    scene_ = std::move(loaded_scene);
    map_ = std::move(selected_map);
    impl_->project_root = options_.project_root;
    impl_->manifest = &*manifest_;
    impl_->errors = &errors_;
    const auto animation_directory = options_.project_root /
        manifest_->directories.assets / "animations";
    std::error_code directory_error;
    const bool animations_exist =
        std::filesystem::exists(animation_directory, directory_error);
    if (directory_error) {
        errors_.push_back("animations: could not inspect animation directory");
        return false;
    }
    if (animations_exist) {
        for (const auto& entry : std::filesystem::directory_iterator(
                 animation_directory, directory_error)) {
            if (directory_error) break;
            const auto filename = entry.path().filename().string();
            if (!entry.is_regular_file(directory_error) ||
                filename.size() <= std::string_view{".animation.json"}.size() ||
                filename.compare(filename.size() -
                                     std::string_view{".animation.json"}.size(),
                                 std::string_view{".animation.json"}.size(),
                                 ".animation.json") != 0) {
                continue;
            }
            auto animation = project::load_animation(
                options_.project_root, *manifest_,
                std::filesystem::relative(entry.path(), options_.project_root));
            if (!animation.ok()) {
                append_errors(errors_, animation.errors);
                return false;
            }
            const auto [_, inserted] = impl_->animation_clips.emplace(
                animation.asset->document.id.value, std::move(*animation.asset));
            if (!inserted) {
                errors_.push_back("animations: duplicate resource id");
                return false;
            }
        }
    }
    if (directory_error) {
        errors_.push_back("animations: could not enumerate animation documents");
        return false;
    }
    for (const auto& instance : map_->instances) {
        if (!instance.prefab) continue;
        const auto prefab = std::ranges::find(
            map_->prefabs, instance.prefab->id.value,
            &project::PrefabDefinition::id);
        if (prefab == map_->prefabs.end() || !prefab->mechanic) continue;
        auto graph = project::load_mechanic_graph(
            options_.project_root, *manifest_,
            project::mechanic_graph_document_path(
                *manifest_, prefab->mechanic->id));
        if (!graph.ok()) {
            append_errors(errors_, graph.errors);
            return false;
        }
        auto compiled = physics::compile_mechanic_graph(
            *graph.asset, *map_, prefab->mechanic_overrides,
            instance.transform);
        if (!compiled.ok()) {
            append_errors(errors_, compiled.errors);
            return false;
        }
        std::vector<const physics::MechanicBodyDescription*> visual_bodies;
        for (const auto& body : compiled.plan->bodies)
            if (body.visual_entity && body.visual_entity->id == prefab->entity.id)
                visual_bodies.push_back(&body);
        if (visual_bodies.size() > 1U) {
            errors_.push_back(
                "prefab mechanic has multiple bodies for its visual entity: " +
                prefab->id);
            return false;
        }
        Impl::MechanicInstanceSimulation runtime_mechanic;
        if (!visual_bodies.empty()) {
            runtime_mechanic.visual_binding = Impl::MechanicVisualBinding{
                .body_node_id = visual_bodies.front()->node_id,
                .initial_position = visual_bodies.front()->position,
                .initial_rotation_degrees =
                    visual_bodies.front()->rotation_degrees};
        }
        if (!runtime_mechanic.simulation.load(std::move(*compiled.plan))) {
            errors_.push_back(
                "could not create mechanic simulation for instance: " +
                instance.id);
            return false;
        }
        runtime_mechanic.simulation.play();
        impl_->mechanic_instances.emplace(
            instance.id, std::move(runtime_mechanic));
    }
    for (const auto& instance : map_->instances) {
        std::vector<project::MapProperty> properties;
        std::vector<project::AnimationParameter> animation_parameters;
        if (instance.prefab) {
            const auto prefab = std::find_if(
                map_->prefabs.begin(), map_->prefabs.end(),
                [&](const auto& candidate) {
                    return candidate.id == instance.prefab->id.value;
                });
            if (prefab != map_->prefabs.end()) properties = prefab->overrides;
        }
        for (const auto& property : instance.properties) {
            const auto existing = std::find_if(
                properties.begin(), properties.end(),
                [&](const auto& candidate) { return candidate.id == property.id; });
            if (existing != properties.end()) existing->value = property.value;
            else properties.push_back(property);
        }
        for (const auto& property : properties) {
            constexpr std::string_view parameter_prefix = "animationParameter.";
            if (property.id.starts_with(parameter_prefix)) {
                const auto parameter_id = property.id.substr(parameter_prefix.size());
                if (!core::ResourceId::is_valid(parameter_id)) {
                    errors_.push_back("instances.animationParameter: invalid parameter id");
                    return false;
                }
                if (const auto* value = std::get_if<bool>(&property.value))
                    animation_parameters.push_back({parameter_id, *value});
                else if (const auto* value = std::get_if<float>(&property.value))
                    animation_parameters.push_back({parameter_id, *value});
                else {
                    errors_.push_back(
                        "instances.animationParameter: expected bool or float");
                    return false;
                }
                continue;
            }
            if (property.id != "animation") continue;
            const auto* reference = std::get_if<project::ResourceReference>(
                &property.value);
            if (reference == nullptr || reference->expected_type != "animation") {
                errors_.push_back(
                    "instances.animation: expected an animation resource reference");
                return false;
            }
            if (!impl_->animation_clips.contains(reference->id.value)) {
                errors_.push_back("instances.animation: referenced clip is missing");
                return false;
            }
            impl_->animation_instances.emplace(instance.id, reference->id.value);
        }
        impl_->animation_parameters.emplace(instance.id, std::move(animation_parameters));
    }
    triggers_ = std::make_unique<TriggerRuntime>(*map_);
    impl_->chunk_index_ready = impl_->chunk_index.rebuild(*map_);
    if (replay_) replay_player_ = std::make_unique<ReplayPlayer>(*replay_);
    if (!physics_.create() || !physics_.load_map_collisions(*map_)) {
        errors_.push_back("could not create the map physics world");
        map_.reset();
        manifest_.reset();
        return false;
    }
    if (!options_.input_actions.empty() || loaded_input) {
        if (!options_.input_actions.empty()) {
            if (!input_.configure(options_.input_actions)) {
                errors_.push_back("could not configure input actions");
                return false;
            }
        } else {
            if (!input_.configure(loaded_input->actions)) {
                errors_.push_back("could not configure persisted input actions");
                return false;
            }
        }
    }
    if (options_.character_actions) {
        const auto& actions = *options_.character_actions;
        const auto declared = [&](const std::string& id) {
            return core::ResourceId::is_valid(id) &&
                std::ranges::any_of(input_.actions(), [&](const auto& action) {
                    return action.id == id;
                });
        };
        if (!declared(actions.left) || !declared(actions.right) ||
            !declared(actions.jump)) {
            errors_.push_back(
                "character actions must name three declared semantic actions");
            return false;
        }
    }
    if (options_.enable_character) {
        character_ = std::make_unique<CharacterController>();
        if (!character_->create(
                physics_, options_.character_spawn.value_or(core::Vec2{}))) {
            errors_.push_back("could not create the runtime character");
            return false;
        }
    }

    const auto ensure_texture = [&](const project::ResourceReference& reference) {
        if (impl_->texture_sources.contains(reference.id.value)) return true;
        auto texture = project::load_texture_asset(
            options_.project_root, *manifest_,
            project::texture_document_path(*manifest_, reference.id));
        if (!texture.ok()) {
            append_errors(errors_, texture.errors);
            return false;
        }
        impl_->texture_sources.emplace(reference.id.value,
            Impl::TextureSource{.path = options_.project_root / texture.asset->source,
                                .width = texture.asset->width,
                                .height = texture.asset->height,
                                .view = texture.asset->view});
        return true;
    };

    for (const auto& instance : map_->instances) {
        std::optional<core::ResourceId> entity_id;
        if (instance.entity) {
            entity_id = instance.entity->id;
        } else if (instance.prefab) {
            const auto prefab = std::find_if(map_->prefabs.begin(), map_->prefabs.end(),
                [&](const auto& candidate) { return candidate.id == instance.prefab->id.value; });
            if (prefab != map_->prefabs.end()) entity_id = prefab->entity.id;
        }
        if (!entity_id) continue;
        auto entity = project::load_entity(
            options_.project_root, *manifest_,
            project::entity_document_path(*manifest_, *entity_id));
        if (!entity.ok()) {
            append_errors(errors_, entity.errors);
            return false;
        }
        auto resolved_entity = std::move(*entity.entity);
        const auto layer = std::find_if(map_->layers.begin(), map_->layers.end(),
            [&](const auto& candidate) { return candidate.id == instance.layer_id; });
        const float layer_depth = layer == map_->layers.end() ? 0.0F : layer->depth;
        if (!resolve_constraints(resolved_entity.nodes, resolved_entity.constraints)) {
            errors_.push_back("entity constraints could not be resolved");
            return false;
        }
        if (!resolve_ik_chains(resolved_entity.nodes, resolved_entity.ik_chains)) {
            errors_.push_back("entity IK chains could not be resolved");
            return false;
        }
        if (resolved_entity.animation_state_machine) {
            impl_->animation_state_machines.emplace(
                instance.id, *resolved_entity.animation_state_machine);
        }
        Impl::EntitySimulation simulation{
            .entity_id = *entity_id,
            .mesh = resolved_entity.deformation_mesh,
            .xpbd = resolved_entity.xpbd,
            .nodes = resolved_entity.nodes,
            .constraints = resolved_entity.constraints,
            .ik_chains = resolved_entity.ik_chains,
            .instance_transform = instance.transform,
            .layer_depth = layer_depth};
        if (resolved_entity.behavior) {
            auto behavior = project::load_behavior_graph(
                options_.project_root, *manifest_,
                project::behavior_graph_document_path(
                    *manifest_, resolved_entity.behavior->id));
            if (!behavior.ok()) {
                append_errors(errors_, behavior.errors);
                return false;
            }
            simulation.behavior.emplace(std::move(*behavior.asset));
        }
        if (simulation.xpbd) {
            simulation.previous_xpbd_positions.reserve(simulation.xpbd->particles.size());
            simulation.interpolated_xpbd_positions.reserve(simulation.xpbd->particles.size());
            for (const auto& particle : simulation.xpbd->particles) {
                simulation.previous_xpbd_positions.push_back(particle.position);
                simulation.interpolated_xpbd_positions.push_back(particle.position);
            }
        }
        simulation.poses.reserve(resolved_entity.nodes.size());
        for (const auto& node : resolved_entity.nodes)
            simulation.poses.push_back({.node_id = node.id,
                                        .transform = node.transform});
        impl_->entity_simulations.emplace(instance.id, std::move(simulation));
        for (std::size_t node_index = 0; node_index < resolved_entity.nodes.size(); ++node_index) {
            const auto& node = resolved_entity.nodes[node_index];
            if (!node.visible) continue;
            if (node.drawable.kind == project::EntityDrawableKind::vector &&
                node.drawable.resource) {
                const auto vector_id = node.drawable.resource->id.value;
                project::VectorAsset drawable;
                const auto cached_vector = impl_->vector_assets.find(vector_id);
                if (cached_vector != impl_->vector_assets.end()) {
                    drawable = cached_vector->second;
                } else {
                    auto vector = project::load_vector_asset(
                        options_.project_root, *manifest_,
                        project::vector_document_path(*manifest_,
                                                      node.drawable.resource->id));
                    if (!vector.ok()) {
                        append_errors(errors_, vector.errors);
                        return false;
                    }
                    drawable = std::move(*vector.asset);
                    if (drawable.source_kind == project::VectorSourceKind::linked_svg) {
                        auto converted = render::convert_svg_to_native(
                            options_.project_root / drawable.source,
                            drawable.document.id, drawable.document.name);
                        if (!converted.ok()) {
                            append_errors(errors_, converted.errors);
                            return false;
                        }
                        drawable = std::move(*converted.asset);
                    }
                    impl_->vector_assets.emplace(vector_id, drawable);
                }
                auto geometry = impl_->vector_geometry_cache.get_or_build(drawable);
                if (!geometry.ok()) {
                    errors_.insert(errors_.end(), geometry.errors.begin(), geometry.errors.end());
                    return false;
                }
                std::optional<project::MaterialDefinition> material;
                if (node.drawable.material) {
                    auto loaded_material = project::load_material(
                        options_.project_root, *manifest_,
                        project::material_document_path(*manifest_,
                                                       node.drawable.material->id));
                if (!loaded_material.ok()) {
                        append_errors(errors_, loaded_material.errors);
                        return false;
                    }
                    material = std::move(*loaded_material.asset);
                }
                for (auto& packet : geometry.packets) {
                    if (packet.image_fill && !ensure_texture(packet.image_fill->texture))
                        return false;
                    if (material && material->texture &&
                        !ensure_texture(*material->texture)) return false;
                    if (material) apply_material(packet, *material);
                    if (material && material->texture) {
                        packet.image_fill = project::VectorImageFill{
                            .texture = *material->texture,
                            .transform = material->uv_transform,
                            .opacity = material->opacity};
                        packet.fill_color.reset();
                        generate_planar_uvs(packet);
                    }
                    transform_packet(packet, resolved_entity, node_index, instance.transform);
                    packet.node_id = instance.id + ":" + node.id + ":" + packet.node_id;
                    impl_->packet_base_transforms.emplace(
                        packet.node_id, Impl::PacketBaseTransform{
                            .local_position = node.transform.position,
                            .rotation_degrees = node.transform.rotation_degrees,
                            .scale = node.transform.scale,
                            .world_origin = apply_node_transform(
                                {0.0F, 0.0F}, resolved_entity, node_index,
                                instance.transform)});
                    impl_->packet_sort_keys.emplace(
                        packet.node_id, Impl::PacketSortKey{layer_depth, node.z_order});
                    impl_->packets.push_back(std::move(packet));
                }
            } else if (node.drawable.kind ==
                           project::EntityDrawableKind::visual_component &&
                       node.drawable.resource) {
                auto component = project::load_visual_component(
                    options_.project_root, *manifest_,
                    project::visual_component_document_path(
                        *manifest_, node.drawable.resource->id));
                if (!component.ok()) {
                    append_errors(errors_, component.errors);
                    return false;
                }
                const auto component_instance =
                    node.drawable.component_instance.value_or(
                        project::VisualComponentInstance{});
                impl_->animated_visual_components.emplace(
                    instance.id + ":" + node.id,
                    Impl::AnimatedVisualComponent{
                        .component = *component.asset,
                        .instance = component_instance,
                        .entity = resolved_entity,
                        .node_index = node_index,
                        .instance_transform = instance.transform,
                        .instance_id = instance.id,
                        .node_id = node.id});
                auto visual = render::resolve_visual_component(
                    options_.project_root, *manifest_, *component.asset,
                    component_instance);
                if (!visual.ok()) {
                    errors_.insert(errors_.end(), visual.errors.begin(),
                                   visual.errors.end());
                    return false;
                }
                for (auto& packet : visual.packets) {
                    if (packet.image_fill &&
                        !ensure_texture(packet.image_fill->texture)) return false;
                    transform_packet(packet, resolved_entity, node_index,
                                     instance.transform);
                    packet.node_id = instance.id + ":" + node.id + ":" +
                        packet.node_id;
                    impl_->packet_base_transforms.emplace(
                        packet.node_id, Impl::PacketBaseTransform{
                            .local_position = node.transform.position,
                            .rotation_degrees = node.transform.rotation_degrees,
                            .scale = node.transform.scale,
                            .world_origin = apply_node_transform(
                                {0.0F, 0.0F}, resolved_entity, node_index,
                                instance.transform)});
                    impl_->packet_sort_keys.emplace(
                        packet.node_id,
                        Impl::PacketSortKey{layer_depth, node.z_order});
                    impl_->packets.push_back(std::move(packet));
                }
            } else if (node.drawable.kind == project::EntityDrawableKind::texture) {
                if (!node.drawable.resource || !ensure_texture(*node.drawable.resource))
                    return false;
                const auto& source = impl_->texture_sources.at(node.drawable.resource->id.value);
                auto geometry = render::build_raster_view_draw_packets({
                    .node_id = node.id,
                    .texture = *node.drawable.resource,
                    .source_width = source.width,
                    .source_height = source.height,
                    .pixels_per_unit = static_cast<float>(
                        manifest_->pixels_per_unit),
                    .view = source.view,
                });
                if (!geometry.ok()) {
                    errors_.insert(errors_.end(), geometry.errors.begin(),
                                   geometry.errors.end());
                    return false;
                }
                auto packet = std::move(geometry.packets.front());
                if (node.drawable.material) {
                    auto loaded_material = project::load_material(
                        options_.project_root, *manifest_,
                        project::material_document_path(
                            *manifest_, node.drawable.material->id));
                    if (!loaded_material.ok()) {
                        append_errors(errors_, loaded_material.errors);
                        return false;
                    }
                    apply_material(packet, *loaded_material.asset);
                }
                transform_packet(packet, resolved_entity, node_index, instance.transform);
                packet.node_id = instance.id + ":" + node.id;
                impl_->packet_base_transforms.emplace(
                    packet.node_id, Impl::PacketBaseTransform{
                        .local_position = node.transform.position,
                        .rotation_degrees = node.transform.rotation_degrees,
                        .scale = node.transform.scale,
                        .world_origin = apply_node_transform(
                            {0.0F, 0.0F}, resolved_entity, node_index,
                            instance.transform)});
                impl_->packet_sort_keys.emplace(
                    packet.node_id, Impl::PacketSortKey{layer_depth, node.z_order});
                impl_->packets.push_back(std::move(packet));
            }
        }
    }
    stats_.deformation_instances = static_cast<std::size_t>(std::ranges::count_if(
        impl_->entity_simulations, [](const auto& item) {
            return item.second.mesh.has_value();
        }));
    stats_.vector_geometry_cache_entries = impl_->vector_geometry_cache.size();
    std::stable_sort(impl_->packets.begin(), impl_->packets.end(),
                     [&](const auto& left, const auto& right) {
                         const auto left_key = impl_->packet_sort_keys.at(left.node_id);
                         const auto right_key = impl_->packet_sort_keys.at(right.node_id);
                         if (left_key.layer_depth != right_key.layer_depth)
                             return left_key.layer_depth < right_key.layer_depth;
                         if (left_key.z_order != right_key.z_order)
                             return left_key.z_order < right_key.z_order;
                         return left.node_id < right.node_id;
                     });
    for (std::size_t index = 0; index < impl_->packets.size(); ++index) {
        const auto separator = impl_->packets[index].node_id.find(':');
        if (separator != std::string::npos)
            impl_->packet_indices_by_instance[impl_->packets[index].node_id.substr(0, separator)]
                .push_back(index);
    }
    impl_->packet_bounds.reserve(impl_->packets.size());
    impl_->packet_bounds_dynamic.reserve(impl_->packets.size());
    for (const auto& packet : impl_->packets) {
        impl_->packet_bounds.push_back(packet_bounds_for(packet));
        const auto separator = packet.node_id.find(':');
        const auto instance_id = separator == std::string::npos
            ? std::string{} : packet.node_id.substr(0, separator);
        const auto simulation = impl_->entity_simulations.find(instance_id);
        const bool dynamic = simulation != impl_->entity_simulations.end() &&
            (simulation->second.mesh.has_value() ||
             !simulation->second.constraints.empty() ||
             !simulation->second.ik_chains.empty() ||
             impl_->animation_instances.contains(instance_id) ||
             impl_->animation_state_machines.contains(instance_id) ||
             impl_->mechanic_instances.contains(instance_id));
        impl_->packet_bounds_dynamic.push_back(dynamic);
    }
    for (const auto& instance : map_->instances) {
        const auto participates = std::ranges::find(
            instance.properties, "triggerActor", &project::MapProperty::id);
        if (participates != instance.properties.end()) {
            const auto* enabled = std::get_if<bool>(&participates->value);
            if (enabled != nullptr && !*enabled) continue;
        }
        std::optional<core::Rect> bounds;
        const auto explicit_extents = std::ranges::find(
            instance.properties, "triggerHalfExtents",
            &project::MapProperty::id);
        if (explicit_extents != instance.properties.end()) {
            if (const auto* half_extents =
                    std::get_if<core::Vec2>(&explicit_extents->value))
                bounds = core::Rect{
                    {instance.transform.position.x - half_extents->x,
                     instance.transform.position.y - half_extents->y},
                    {half_extents->x * 2.0F, half_extents->y * 2.0F}};
        }
        if (!bounds) {
            const auto packet_indices =
                impl_->packet_indices_by_instance.find(instance.id);
            if (packet_indices != impl_->packet_indices_by_instance.end()) {
                for (const auto packet_index : packet_indices->second) {
                    if (packet_index >= impl_->packet_bounds.size()) continue;
                    const auto& packet = impl_->packet_bounds[packet_index];
                    if (!bounds) {
                        bounds = core::Rect{
                            packet.minimum,
                            {packet.maximum.x - packet.minimum.x,
                             packet.maximum.y - packet.minimum.y}};
                    } else {
                        const auto minimum = core::Vec2{
                            std::min(bounds->origin.x, packet.minimum.x),
                            std::min(bounds->origin.y, packet.minimum.y)};
                        const auto maximum = core::Vec2{
                            std::max(bounds->origin.x + bounds->size.x,
                                     packet.maximum.x),
                            std::max(bounds->origin.y + bounds->size.y,
                                     packet.maximum.y)};
                        *bounds = {minimum,
                                   {maximum.x - minimum.x,
                                    maximum.y - minimum.y}};
                    }
                }
            }
        }
        impl_->trigger_actor_bounds.emplace(
            instance.id,
            bounds.value_or(core::Rect{
                {instance.transform.position.x - 0.5F,
                 instance.transform.position.y - 0.5F},
                {1.0F, 1.0F}}));
    }
    return true;
}

bool PreviewRuntime::run() {
    if (!loaded()) return false;
    SDL_SetMainReady();
    const auto sdl_flags = SDL_INIT_VIDEO |
        (options_.enable_character ? SDL_INIT_GAMECONTROLLER : 0U);
    const auto headless_flags = SDL_INIT_TIMER |
        (options_.enable_character ? SDL_INIT_GAMECONTROLLER : 0U);
    bool headless = options_.mode == RuntimeMode::smoke_test;
    if (headless) {
        if (SDL_Init(headless_flags) != 0) {
            errors_.push_back(SDL_GetError());
            return false;
        }
    } else if (SDL_Init(sdl_flags) != 0) {
        if (options_.mode == RuntimeMode::interactive) {
            errors_.push_back(SDL_GetError());
            return false;
        }
        SDL_Quit();
        SDL_SetMainReady();
        if (SDL_Init(headless_flags) != 0) {
            errors_.push_back(SDL_GetError());
            return false;
        }
        headless = true;
    }
    impl_->sdl_initialized = true;
    if (options_.enable_character) {
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (!SDL_IsGameController(index)) continue;
            impl_->controller = SDL_GameControllerOpen(index);
            if (impl_->controller != nullptr) break;
        }
    }
    if (!headless && !impl_->texture_sources.empty() &&
        (IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        errors_.push_back(IMG_GetError());
        return false;
    }

    if (!headless) {
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#elif defined(_WIN32)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    const auto flags = SDL_WINDOW_OPENGL |
        (options_.mode == RuntimeMode::interactive ? 0U : SDL_WINDOW_HIDDEN);
    impl_->window = SDL_CreateWindow("Vertex Loom Preview Runtime",
                                     SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     options_.width, options_.height, flags);
    if (impl_->window == nullptr) {
        errors_.push_back(SDL_GetError());
        return false;
    }
    impl_->context = SDL_GL_CreateContext(impl_->window);
    if (impl_->context == nullptr) {
        errors_.push_back(SDL_GetError());
        return false;
    }
    if (!impl_->renderer.initialize()) {
        errors_.push_back(impl_->renderer.initialization_error());
        return false;
    }
    if (options_.mode == RuntimeMode::benchmark) SDL_GL_SetSwapInterval(0);
    }
    impl_->camera.set_viewport(options_.width, options_.height);
    impl_->camera.set_limits(options_.camera_limits);
    if (!headless && impl_->audio_clip) {
        if (!impl_->audio_mixer.configure(impl_->audio_clip->sample_rate, 2U) ||
            !impl_->audio_mixer.set_bus_gain(impl_->audio_bus,
                                             impl_->audio_bus_gain) ||
            !impl_->audio_device.open(impl_->audio_clip->sample_rate,
                                      2U) ||
            !impl_->audio_mixer.play(*impl_->audio_clip, impl_->audio_bus,
                                     impl_->audio_gain, impl_->audio_pan,
                                     impl_->audio_loop)) {
            errors_.push_back("audio: " + (impl_->audio_device.error().empty()
                ? std::string("could not start PCM playback")
                : impl_->audio_device.error()));
            return false;
        }
    }

    if (!headless) for (const auto& [id, source] : impl_->texture_sources) {
        const auto image = render::load_png(source.path);
        if (!image.ok()) {
            errors_.push_back("texture " + id + ": " + image.error->message);
            return false;
        }
        GLuint handle = 0U;
        glGenTextures(1, &handle);
        if (handle == 0U) {
            errors_.push_back("could not create OpenGL texture " + id);
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, handle);
        const auto filter = source.view &&
                source.view->filter == project::RasterFilter::nearest
            ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     static_cast<GLsizei>(image.image->width),
                     static_cast<GLsizei>(image.image->height), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image.image->rgba8.data());
        impl_->texture_handles.emplace(id,
            render::OpenGLTextureHandle{.handle = handle,
                                        .width = image.image->width,
                                        .height = image.image->height});
    }

    const std::size_t limit = options_.frame_limit != 0U
        ? options_.frame_limit
        : (options_.mode == RuntimeMode::smoke_test ? 1U
           : options_.mode == RuntimeMode::benchmark ? 600U : 0U);
    constexpr double fixed_time_step = 1.0 / 60.0;
    const auto performance_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    auto previous_counter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    const auto start_counter = previous_counter;
    std::vector<double> frame_times_ms;
    frame_times_ms.reserve(limit == 0U ? 256U : limit);
    bool running = true;
    bool stop_requested = false;
    while (running && !stop_requested && (limit == 0U || stats_.frames < limit)) {
        const auto frame_start = SDL_GetPerformanceCounter();
        SDL_Event event{};
        if (!input_.actions().empty() && !replay_player_) input_.begin_frame();
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_MOUSEWHEEL && options_.mode == RuntimeMode::interactive)
                impl_->camera.zoom_at(
                    {static_cast<float>(event.wheel.mouseX),
                     static_cast<float>(event.wheel.mouseY)},
                    std::pow(1.1F, static_cast<float>(event.wheel.y)));
            if (input_.actions().empty() || replay_player_) continue;
            if (event.type == SDL_KEYDOWN)
                input_.set_keyboard_modifiers(event.key.keysym.mod);
            if (event.type == SDL_KEYDOWN)
                input_.press(InputDevice::keyboard, event.key.keysym.sym,
                             event.key.repeat != 0);
            else if (event.type == SDL_KEYUP)
                input_.set_keyboard_modifiers(event.key.keysym.mod);
            if (event.type == SDL_KEYUP)
                input_.release(InputDevice::keyboard, event.key.keysym.sym);
            else if (event.type == SDL_CONTROLLERBUTTONDOWN)
                input_.press(InputDevice::gamepad, event.cbutton.button);
            else if (event.type == SDL_CONTROLLERBUTTONUP)
                input_.release(InputDevice::gamepad, event.cbutton.button);
            else if (event.type == SDL_CONTROLLERAXISMOTION)
                input_.set_axis(InputDevice::gamepad, event.caxis.axis,
                                static_cast<float>(event.caxis.value) / 32767.0F);
        }
        const auto current_counter = SDL_GetPerformanceCounter();
        const auto elapsed = options_.mode == RuntimeMode::interactive
            ? std::min(0.25, static_cast<double>(current_counter - previous_counter) /
                       performance_frequency)
            : fixed_time_step;
        previous_counter = current_counter;
        accumulator += elapsed;
        const auto step_physics = [&]() {
            if (replay_player_) {
                if (!replay_player_->advance(stats_.physics_steps, input_)) {
                    errors_.push_back("replay frame order is invalid");
                    return false;
                }
                stats_.replay_events += replay_player_->events().size();
                if (replay_player_->checkpoint()) ++stats_.replay_checkpoints;
            }
            impl_->behavior_actions.clear();
            for (const auto& definition : input_.actions()) {
                if (!input_.held(definition.id) && !input_.pressed(definition.id)) continue;
                for (auto& [instance_id, simulation] : impl_->entity_simulations) {
                    if (!simulation.behavior) continue;
                    auto actions = simulation.behavior->evaluate(
                        {BehaviorSignalSource::action, definition.id, {}},
                        static_cast<float>(fixed_time_step));
                    impl_->apply_behavior_actions(
                        instance_id, actions, static_cast<float>(fixed_time_step));
                    stats_.behavior_actions += actions.size();
                }
            }
            impl_->flush_transformations();
            if (character_) {
                CharacterControlFrame controls;
                if (options_.character_actions) {
                    const auto& actions = *options_.character_actions;
                    controls.horizontal =
                        (input_.held(actions.right) ? 1.0F : 0.0F) -
                        (input_.held(actions.left) ? 1.0F : 0.0F);
                    controls.jump_pressed = input_.pressed(actions.jump);
                }
                character_->update(controls,
                                   static_cast<float>(fixed_time_step));
            }
            for (auto& [instance_id, mechanic] : impl_->mechanic_instances) {
                const auto previous_steps = mechanic.simulation.step_count();
                if (!mechanic.simulation.update(
                        static_cast<float>(fixed_time_step))) {
                    errors_.push_back(
                        "mechanic simulation step failed for instance: " +
                        instance_id);
                    return false;
                }
                stats_.mechanic_steps +=
                    mechanic.simulation.step_count() - previous_steps;
            }
            for (auto& [instance_id, simulation] : impl_->entity_simulations) {
                if (!simulation.xpbd) continue;
                simulation.previous_xpbd_positions.clear();
                simulation.previous_xpbd_positions.reserve(simulation.xpbd->particles.size());
                for (const auto& particle : simulation.xpbd->particles)
                    simulation.previous_xpbd_positions.push_back(particle.position);
                const auto result = project::solve_xpbd_substep(
                    *simulation.xpbd, static_cast<float>(fixed_time_step), 4);
                if (!result.ok()) {
                    append_errors(errors_, result.errors);
                    return false;
                }
                (void)instance_id;
                ++stats_.xpbd_steps;
            }
            if (!physics_.step(static_cast<float>(fixed_time_step))) {
                errors_.push_back("physics step failed");
                return false;
            }
            ++stats_.physics_steps;
            impl_->gameplay_events.clear();
            std::vector<TriggerActor> trigger_actors;
            trigger_actors.reserve(impl_->trigger_actor_bounds.size() +
                                   (character_ ? 1U : 0U));
            for (const auto& [instance_id, base_bounds] :
                 impl_->trigger_actor_bounds) {
                auto bounds = base_bounds;
                const auto mechanic =
                    impl_->mechanic_instances.find(instance_id);
                if (mechanic != impl_->mechanic_instances.end() &&
                    mechanic->second.visual_binding) {
                    const auto& binding = *mechanic->second.visual_binding;
                    const auto body = std::ranges::find(
                        mechanic->second.simulation.body_states(),
                        binding.body_node_id,
                        &physics::MechanicBodyState::node_id);
                    if (body != mechanic->second.simulation.body_states().end()) {
                        bounds.origin.x += body->position.x -
                            binding.initial_position.x;
                        bounds.origin.y += body->position.y -
                            binding.initial_position.y;
                    }
                }
                trigger_actors.push_back({instance_id, bounds});
            }
            if (character_) {
                const auto position = character_->position();
                stats_.character_x = position.x;
                stats_.character_y = position.y;
                trigger_actors.push_back({
                    "runtime-character",
                    {{position.x - 0.5F, position.y - 0.5F}, {1.0F, 1.0F}}});
            }
            if (triggers_) {
                impl_->gameplay_events = triggers_->update(trigger_actors);
                stats_.gameplay_events += impl_->gameplay_events.size();
                if (options_.gameplay_event_handler) {
                    for (const auto& event : impl_->gameplay_events) {
                        if (!options_.gameplay_event_handler(event)) {
                            stop_requested = true;
                            break;
                        }
                    }
                }
            }
            return true;
        };
        if (options_.mode == RuntimeMode::interactive) {
            while (!stop_requested && accumulator >= fixed_time_step) {
                if (!step_physics()) return false;
                accumulator -= fixed_time_step;
            }
        } else if (!step_physics()) {
            return false;
        }

        impl_->camera.set_follow_target(
            options_.follow_character && character_
                ? std::optional<core::Vec2>{character_->position()}
                : std::nullopt);
        impl_->camera.update(static_cast<float>(elapsed));

        const auto bounds = impl_->camera.world_bounds();
        const auto interpolation_alpha = options_.mode == RuntimeMode::interactive
            ? static_cast<float>(std::clamp(accumulator / fixed_time_step, 0.0, 1.0))
            : 1.0F;
        for (auto& [instance_id, simulation] : impl_->entity_simulations) {
            (void)instance_id;
            if (!simulation.xpbd) continue;
            simulation.interpolated_xpbd_positions.clear();
            simulation.interpolated_xpbd_positions.reserve(simulation.xpbd->particles.size());
            for (std::size_t index = 0; index < simulation.xpbd->particles.size(); ++index) {
                const auto current = simulation.xpbd->particles[index].position;
                const auto previous = index < simulation.previous_xpbd_positions.size()
                    ? simulation.previous_xpbd_positions[index] : current;
                simulation.interpolated_xpbd_positions.push_back({
                    previous.x + (current.x - previous.x) * interpolation_alpha,
                    previous.y + (current.y - previous.y) * interpolation_alpha});
            }
        }
        const auto animation_time = static_cast<float>(stats_.physics_steps) *
            static_cast<float>(fixed_time_step);
        const auto previous_animation_time = animation_time -
            static_cast<float>(fixed_time_step);
        const auto append_marker_events = [&](const std::string& instance_id,
                                              const std::string& clip_id,
                                              const std::vector<project::AnimationMarkerHit>& markers) {
            for (const auto& marker : markers)
                impl_->animation_marker_events.push_back({
                    .instance_id = instance_id,
                    .clip_id = {.value = clip_id},
                    .marker = marker});
        };
        for (const auto& [instance_id, clip_id] : impl_->animation_instances) {
            const auto clip = impl_->animation_clips.find(clip_id);
            if (clip == impl_->animation_clips.end()) continue;
            append_marker_events(instance_id, clip_id,
                project::animation_markers_between(
                    clip->second, previous_animation_time, animation_time));
        }
        for (const auto& [instance_id, machine] : impl_->animation_state_machines) {
            const auto previous = evaluate_instance_state(
                instance_id, previous_animation_time);
            const auto current = evaluate_instance_state(instance_id, animation_time);
            if (!previous || !current) continue;
            const auto clip = impl_->animation_clips.find(current->clip_id.value);
            if (clip == impl_->animation_clips.end()) continue;
            if (previous->state_id == current->state_id &&
                previous->clip_id == current->clip_id) {
                auto end_time = current->local_time;
                if (clip->second.loop && end_time < previous->local_time)
                    end_time += clip->second.duration;
                append_marker_events(instance_id, current->clip_id.value,
                    project::animation_markers_between(
                        clip->second, previous->local_time, end_time));
                continue;
            }
            const auto previous_clip = impl_->animation_clips.find(previous->clip_id.value);
            if (previous_clip != impl_->animation_clips.end()) {
                const auto* transition = project::select_animation_transition(
                    machine, previous->state_id,
                    impl_->animation_parameters.at(instance_id),
                    previous_clip->second.duration > 0.0F
                        ? previous->local_time / previous_clip->second.duration : 1.0F);
                auto old_end = previous->local_time;
                if (transition && transition->exit_time)
                    old_end = previous_clip->second.duration * *transition->exit_time;
                append_marker_events(instance_id, previous->clip_id.value,
                    project::animation_markers_between(
                        previous_clip->second, previous->local_time, old_end));
            }
            append_marker_events(instance_id, current->clip_id.value,
                project::animation_markers_between(
                    clip->second, 0.0F, current->local_time));
        }
        stats_.animation_marker_events = impl_->animation_marker_events.size();
        std::unordered_map<std::string, std::vector<project::EntityNode>> evaluated_nodes;
        std::unordered_map<std::string, render::VectorDrawPacket>
            animated_visual_packets;
        for (const auto& [key, visual_component] :
             impl_->animated_visual_components) {
            (void)key;
            const auto evaluation = evaluate_instance_animation(
                visual_component.instance_id, animation_time);
            if (!evaluation || !evaluation->ok() ||
                !std::ranges::any_of(
                    evaluation->properties, [&](const auto& property) {
                        return property.binding.node_id ==
                                visual_component.node_id &&
                            property.binding.component_id ==
                                visual_component.component.document.id.value;
                    }))
                continue;
            auto resolved = render::resolve_animated_visual_component(
                options_.project_root, *manifest_, visual_component.component,
                visual_component.instance, visual_component.node_id,
                *evaluation);
            if (!resolved.ok()) {
                errors_.insert(errors_.end(), resolved.errors.begin(),
                               resolved.errors.end());
                return false;
            }
            for (auto& packet : resolved.packets) {
                transform_packet(packet, visual_component.entity,
                                 visual_component.node_index,
                                 visual_component.instance_transform);
                packet.node_id = visual_component.instance_id + ":" +
                    visual_component.node_id + ":" + packet.node_id;
                animated_visual_packets.emplace(packet.node_id,
                                                 std::move(packet));
            }
        }
        const auto animate_entity_packet = [&](render::VectorDrawPacket packet) {
            const auto animated_visual =
                animated_visual_packets.find(packet.node_id);
            if (animated_visual != animated_visual_packets.end())
                packet = animated_visual->second;
            const auto separator = packet.node_id.find(':');
            if (separator == std::string::npos) return packet;
            const auto instance_id = packet.node_id.substr(0, separator);
            const auto simulation = impl_->entity_simulations.find(instance_id);
            if (simulation == impl_->entity_simulations.end()) return packet;
            const bool dynamic_instance = simulation->second.mesh.has_value() ||
                !simulation->second.constraints.empty() ||
                !simulation->second.ik_chains.empty() ||
                impl_->animation_instances.contains(instance_id) ||
                impl_->animation_state_machines.contains(instance_id);
            if (!dynamic_instance) return packet;
            if (simulation->second.mesh &&
                deformation_topology_matches(*simulation->second.mesh, packet)) {
                std::optional<project::MeshDeformationResult> deformation;
                if (!simulation->second.xpbd)
                    deformation = evaluate_instance_deformation(instance_id, animation_time);
                const auto* positions = deformation && deformation->ok()
                    ? &deformation->positions : &simulation->second.interpolated_xpbd_positions;
                if (!positions->empty() && positions->size() == packet.fill_vertices.size()) {
                    for (std::size_t index = 0; index < packet.fill_vertices.size(); ++index)
                        packet.fill_vertices[index] = apply_transform(
                            (*positions)[index],
                            simulation->second.instance_transform);
                    if (packet.outline.size() == positions->size())
                        for (std::size_t index = 0; index < packet.outline.size(); ++index)
                            packet.outline[index] = apply_transform(
                                (*positions)[index],
                                simulation->second.instance_transform);
                    ++stats_.deformed_packets;
                }
            }
            const auto second_separator = packet.node_id.find(':', separator + 1U);
            const auto node_id = packet.node_id.substr(
                separator + 1U,
                second_separator == std::string::npos
                    ? std::string::npos
                    : second_separator - separator - 1U);
            const auto nodes_entry = evaluated_nodes.find(instance_id);
            if (nodes_entry == evaluated_nodes.end()) {
                const auto nodes = evaluate_instance_nodes(instance_id, animation_time);
                if (nodes) evaluated_nodes.emplace(instance_id, *nodes);
            }
            const auto resolved_nodes = evaluated_nodes.find(instance_id);
            const project::EntityNode* resolved_node = nullptr;
            if (resolved_nodes != evaluated_nodes.end()) {
                const auto found_node = std::find_if(
                    resolved_nodes->second.begin(), resolved_nodes->second.end(),
                    [&](const auto& candidate) { return candidate.id == node_id; });
                if (found_node != resolved_nodes->second.end()) resolved_node = &*found_node;
            }
            const auto evaluation = evaluate_instance_animation(
                instance_id, animation_time);
            const auto base_transform = impl_->packet_base_transforms.find(packet.node_id);
            if (base_transform == impl_->packet_base_transforms.end()) return packet;
            std::optional<core::Vec2> position;
            std::optional<float> rotation_degrees;
            std::optional<core::Vec2> scale;
            std::optional<core::Vec2> image_position;
            std::optional<core::Vec2> image_scale;
            std::optional<core::Vec2> image_pivot;
            std::optional<float> image_rotation_degrees;
            std::optional<core::Color> color;
            std::optional<float> opacity;
            project::AnimationComposition color_composition =
                project::AnimationComposition::replace;
            project::AnimationComposition opacity_composition =
                project::AnimationComposition::replace;
            if (evaluation && evaluation->ok()) for (const auto& property : evaluation->properties) {
                if (property.binding.node_id != node_id) continue;
                if (property.binding.component_id == "transform" &&
                    property.binding.property_id == "position") {
                    if (const auto* value = std::get_if<core::Vec2>(&property.value))
                        position = *value;
                } else if (property.binding.component_id == "transform" &&
                           property.binding.property_id == "rotationDegrees") {
                    if (const auto* value = std::get_if<float>(&property.value))
                        rotation_degrees = *value;
                } else if (property.binding.component_id == "transform" &&
                           property.binding.property_id == "scale") {
                    if (const auto* value = std::get_if<core::Vec2>(&property.value))
                        scale = *value;
                } else if ((property.binding.component_id == "material" ||
                            property.binding.component_id == "fill") &&
                           property.binding.property_id == "color") {
                    if (const auto* value = std::get_if<core::Color>(&property.value))
                        color = *value,
                        color_composition = property.composition;
                } else if ((property.binding.component_id == "material" ||
                            property.binding.component_id == "imageFill") &&
                           property.binding.property_id == "opacity") {
                    if (const auto* value = std::get_if<float>(&property.value))
                        opacity = *value,
                        opacity_composition = property.composition;
                } else if (property.binding.component_id == "imageFill" &&
                           property.binding.property_id == "position") {
                    if (const auto* value = std::get_if<core::Vec2>(&property.value))
                        image_position = *value;
                } else if (property.binding.component_id == "imageFill" &&
                           property.binding.property_id == "scale") {
                    if (const auto* value = std::get_if<core::Vec2>(&property.value))
                        image_scale = *value;
                } else if (property.binding.component_id == "imageFill" &&
                           property.binding.property_id == "rotationDegrees") {
                    if (const auto* value = std::get_if<float>(&property.value))
                        image_rotation_degrees = *value;
                } else if (property.binding.component_id == "imageFill" &&
                           property.binding.property_id == "pivot") {
                    if (const auto* value = std::get_if<core::Vec2>(&property.value))
                        image_pivot = *value;
                }
            }
            if (resolved_node) {
                position = resolved_node->transform.position;
                rotation_degrees = resolved_node->transform.rotation_degrees;
                scale = resolved_node->transform.scale;
            }
            if (!position && !rotation_degrees && !scale && !color && !opacity &&
                !image_position && !image_scale && !image_rotation_degrees &&
                !image_pivot)
                return packet;
            if (color && packet.fill_color) {
                if (color_composition == project::AnimationComposition::additive) {
                    packet.fill_color->red += color->red;
                    packet.fill_color->green += color->green;
                    packet.fill_color->blue += color->blue;
                    packet.fill_color->alpha += color->alpha;
                } else {
                    packet.fill_color = *color;
                }
            }
            if (opacity) {
                if (packet.fill_color) {
                    packet.fill_color->alpha = opacity_composition ==
                        project::AnimationComposition::additive
                        ? packet.fill_color->alpha + *opacity : *opacity;
                }
                if (packet.image_fill) {
                    packet.image_fill->opacity = opacity_composition ==
                        project::AnimationComposition::additive
                        ? packet.image_fill->opacity + *opacity : *opacity;
                }
            }
            if (packet.image_fill) {
                if (image_position) packet.image_fill->transform.position = *image_position;
                if (image_scale) packet.image_fill->transform.scale = *image_scale;
                if (image_rotation_degrees)
                    packet.image_fill->transform.rotation_degrees = *image_rotation_degrees;
                if (image_pivot) packet.image_fill->transform.pivot = *image_pivot;
            }
            if (!position && !rotation_degrees && !scale) return packet;
            const auto& base = base_transform->second;
            const auto target_position = position.value_or(base.local_position);
            const auto target_rotation = rotation_degrees.value_or(base.rotation_degrees);
            const auto target_scale = scale.value_or(base.scale);
            const auto scale_x = std::abs(base.scale.x) > 1.0e-6F
                ? target_scale.x / base.scale.x : 1.0F;
            const auto scale_y = std::abs(base.scale.y) > 1.0e-6F
                ? target_scale.y / base.scale.y : 1.0F;
            const auto radians = (target_rotation - base.rotation_degrees) *
                0.017453292519943295F;
            const auto cosine = std::cos(radians);
            const auto sine = std::sin(radians);
            const core::Vec2 position_delta{
                target_position.x - base.local_position.x,
                target_position.y - base.local_position.y};
            const auto animate_point = [&](core::Vec2& point) {
                point.x -= base.world_origin.x;
                point.y -= base.world_origin.y;
                point.x *= scale_x;
                point.y *= scale_y;
                const auto rotated_x = point.x * cosine - point.y * sine;
                const auto rotated_y = point.x * sine + point.y * cosine;
                point.x = rotated_x + base.world_origin.x + position_delta.x;
                point.y = rotated_y + base.world_origin.y + position_delta.y;
            };
            for (auto& point : packet.outline) animate_point(point);
            for (auto& point : packet.fill_vertices) animate_point(point);
            return packet;
        };
        const auto animate_packet = [&](render::VectorDrawPacket packet) {
            const auto separator = packet.node_id.find(':');
            const auto instance_id = separator == std::string::npos
                ? std::string{} : packet.node_id.substr(0, separator);
            return impl_->apply_mechanic_pose(
                animate_entity_packet(std::move(packet)), instance_id);
        };
        std::vector<render::VectorDrawPacket> visible_packets;
        bool direct_render = impl_->packets.size() == impl_->packet_bounds.size() &&
            impl_->packets.size() == impl_->packet_bounds_dynamic.size();
        if (direct_render) {
            for (std::size_t packet_index = 0; packet_index < impl_->packets.size(); ++packet_index) {
                if (impl_->packet_bounds_dynamic[packet_index] ||
                    !packet_bounds_visible(impl_->packet_bounds[packet_index], bounds)) {
                    direct_render = false;
                    break;
                }
            }
        }
        if (!direct_render) {
            const auto append_visible_packet = [&](const std::size_t packet_index) {
                const bool dynamic = packet_index >= impl_->packet_bounds_dynamic.size() ||
                    impl_->packet_bounds_dynamic[packet_index];
                if (!dynamic) {
                    if (packet_index < impl_->packet_bounds.size() &&
                        packet_bounds_visible(impl_->packet_bounds[packet_index], bounds))
                        visible_packets.push_back(impl_->packets[packet_index]);
                    return;
                }
                const auto packet = animate_packet(impl_->packets[packet_index]);
                if (packet_visible(packet, bounds)) visible_packets.push_back(packet);
            };
            if (impl_->chunk_index_ready) {
                const auto candidate_instances = impl_->chunk_index.query(bounds);
                stats_.culling_candidates += candidate_instances.size();
                for (const auto& instance_id : candidate_instances) {
                    const auto packet_indices = impl_->packet_indices_by_instance.find(instance_id);
                    if (packet_indices == impl_->packet_indices_by_instance.end()) continue;
                    for (const auto packet_index : packet_indices->second)
                        append_visible_packet(packet_index);
                }
            } else {
                visible_packets.reserve(impl_->packets.size());
                for (std::size_t packet_index = 0; packet_index < impl_->packets.size(); ++packet_index)
                    append_visible_packet(packet_index);
            }
        }
        const auto render_packets = direct_render
            ? std::span<const render::VectorDrawPacket>(impl_->packets)
            : std::span<const render::VectorDrawPacket>(visible_packets);
        impl_->last_frame_packets.assign(render_packets.begin(), render_packets.end());
        if (direct_render) ++stats_.direct_render_frames;
        stats_.culled_packets += impl_->packets.size() - render_packets.size();
        if (headless) {
            stats_.visible_instances = render_packets.size();
            ++stats_.frames;
            frame_times_ms.push_back(static_cast<double>(
                SDL_GetPerformanceCounter() - frame_start) /
                performance_frequency * 1000.0);
            continue;
        }
        glViewport(0, 0, options_.width, options_.height);
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const auto render_stats = impl_->renderer.draw(
            render_packets,
            {.width = options_.width, .height = options_.height, .world_bounds = bounds},
            [this](const core::ResourceId& id) -> std::optional<render::OpenGLTextureHandle> {
                const auto texture = impl_->texture_handles.find(id.value);
                if (texture == impl_->texture_handles.end()) return std::nullopt;
                return texture->second;
            });
        if (!render_stats.ok()) {
            errors_.insert(errors_.end(), render_stats.errors.begin(), render_stats.errors.end());
            return false;
        }
        stats_.visible_instances = render_stats.packets_drawn;
        stats_.draw_calls += render_stats.draw_calls;
        stats_.triangles += render_stats.triangles_drawn;
        ++stats_.frames;
        SDL_GL_SwapWindow(impl_->window);
        if (!headless && impl_->audio_clip) {
            const auto audio = impl_->audio_mixer.mix(1024);
            if (!audio.empty() && !impl_->audio_device.submit(audio)) {
                errors_.push_back("audio: could not queue PCM samples");
                return false;
            }
        }
        frame_times_ms.push_back(static_cast<double>(SDL_GetPerformanceCounter() - frame_start) /
                                 performance_frequency * 1000.0);
    }
    stats_.elapsed_ms = static_cast<double>(SDL_GetPerformanceCounter() - start_counter) /
        performance_frequency * 1000.0;
    if (!frame_times_ms.empty()) {
        std::ranges::sort(frame_times_ms);
        const auto index = std::min(frame_times_ms.size() - 1U,
            static_cast<std::size_t>(std::ceil(
                static_cast<double>(frame_times_ms.size()) * 0.95)) - 1U);
        stats_.p95_frame_ms = frame_times_ms[index];
    }
    return true;
}

std::size_t PreviewRuntime::animation_count() const noexcept {
    return impl_ ? impl_->animation_clips.size() : 0U;
}

std::size_t PreviewRuntime::mechanic_instance_count() const noexcept {
    return impl_ ? impl_->mechanic_instances.size() : 0U;
}

std::optional<std::vector<physics::MechanicBodyState>>
PreviewRuntime::mechanic_body_states(const std::string& instance_id) const {
    if (!impl_) return std::nullopt;
    const auto mechanic = impl_->mechanic_instances.find(instance_id);
    if (mechanic == impl_->mechanic_instances.end()) return std::nullopt;
    return mechanic->second.simulation.body_states();
}

const std::vector<GameplayEvent>& PreviewRuntime::gameplay_events() const noexcept {
    static const std::vector<GameplayEvent> empty;
    return impl_ ? impl_->gameplay_events : empty;
}

const std::vector<AnimationMarkerEvent>&
PreviewRuntime::animation_marker_events() const noexcept {
    static const std::vector<AnimationMarkerEvent> empty;
    return impl_ ? impl_->animation_marker_events : empty;
}

std::vector<std::string> PreviewRuntime::packet_order() const {
    if (!impl_) return {};
    std::vector<std::string> result;
    result.reserve(impl_->packets.size());
    for (const auto& packet : impl_->packets) result.push_back(packet.node_id);
    return result;
}

const std::vector<render::VectorDrawPacket>&
PreviewRuntime::last_frame_packets() const noexcept {
    static const std::vector<render::VectorDrawPacket> empty;
    return impl_ ? impl_->last_frame_packets : empty;
}

std::optional<project::EvaluationResult> PreviewRuntime::evaluate_animation(
    const core::ResourceId& animation_id, const float time) const {
    if (!impl_) return std::nullopt;
    const auto found = impl_->animation_clips.find(animation_id.value);
    if (found == impl_->animation_clips.end()) return std::nullopt;
    return project::evaluate_animation(found->second, time);
}

std::vector<project::AnimationMarkerHit> PreviewRuntime::animation_markers(
    const core::ResourceId& animation_id, const float from_time,
    const float to_time) const {
    if (!impl_) return {};
    const auto found = impl_->animation_clips.find(animation_id.value);
    if (found == impl_->animation_clips.end()) return {};
    return project::animation_markers_between(found->second, from_time, to_time);
}

std::optional<project::EvaluationResult> PreviewRuntime::evaluate_instance_animation(
    const std::string& instance_id, const float time) const {
    if (!impl_) return std::nullopt;
    impl_->begin_evaluation_cache(time);
    const auto cached = impl_->animation_evaluation_cache.find(instance_id);
    if (cached != impl_->animation_evaluation_cache.end()) return cached->second;
    std::optional<project::EvaluationResult> result;
    const auto machine = impl_->animation_state_machines.find(instance_id);
    if (machine != impl_->animation_state_machines.end()) {
        const auto parameters = impl_->animation_parameters.find(instance_id);
        const std::vector<project::AnimationParameter> empty_parameters;
        const auto& values = parameters == impl_->animation_parameters.end()
            ? empty_parameters : parameters->second;
        const auto resolved = resolve_state_machine_animation(
            machine->second, impl_->animation_clips, values, time);
        if (resolved)
            result = evaluate_animation({.value = resolved->clip_id}, resolved->local_time);
    } else {
        const auto animation = impl_->animation_instances.find(instance_id);
        if (animation != impl_->animation_instances.end())
            result = evaluate_animation({.value = animation->second}, time);
    }
    impl_->animation_evaluation_cache.emplace(instance_id, result);
    return result;
}

std::optional<AnimationStateEvaluation>
PreviewRuntime::evaluate_instance_state(const std::string& instance_id,
                                        const float time) const {
    if (!impl_) return std::nullopt;
    const auto machine = impl_->animation_state_machines.find(instance_id);
    if (machine == impl_->animation_state_machines.end()) return std::nullopt;
    const auto parameters = impl_->animation_parameters.find(instance_id);
    const std::vector<project::AnimationParameter> empty_parameters;
    const auto& values = parameters == impl_->animation_parameters.end()
        ? empty_parameters : parameters->second;
    const auto resolved = resolve_state_machine_animation(
        machine->second, impl_->animation_clips, values, time);
    if (!resolved) return std::nullopt;
    return AnimationStateEvaluation{
        .state_id = resolved->state_id,
        .clip_id = {.value = resolved->clip_id},
        .local_time = resolved->local_time};
}

std::optional<project::MeshDeformationResult>
PreviewRuntime::evaluate_instance_deformation(const std::string& instance_id) const {
    return evaluate_instance_deformation(instance_id, 0.0F);
}

std::optional<std::vector<project::EntityNode>>
PreviewRuntime::evaluate_instance_nodes(const std::string& instance_id,
                                        const float time) const {
    if (!impl_) return std::nullopt;
    impl_->begin_evaluation_cache(time);
    const auto cached = impl_->node_evaluation_cache.find(instance_id);
    if (cached != impl_->node_evaluation_cache.end()) return cached->second;
    const auto found = impl_->entity_simulations.find(instance_id);
    if (found == impl_->entity_simulations.end()) {
        impl_->node_evaluation_cache.emplace(instance_id, std::nullopt);
        return std::nullopt;
    }
    auto nodes = found->second.nodes;
    const auto animation = evaluate_instance_animation(instance_id, time);
    if (animation && animation->ok()) apply_animation_to_nodes(nodes, *animation);
    if (!resolve_constraints(nodes, found->second.constraints) ||
        !resolve_ik_chains(nodes, found->second.ik_chains)) {
        impl_->node_evaluation_cache.emplace(instance_id, std::nullopt);
        return std::nullopt;
    }
    impl_->node_evaluation_cache.emplace(instance_id, nodes);
    return nodes;
}

std::optional<project::MeshDeformationResult>
PreviewRuntime::evaluate_instance_deformation(const std::string& instance_id,
                                              const float time) const {
    if (!impl_) return std::nullopt;
    const auto found = impl_->entity_simulations.find(instance_id);
    if (found == impl_->entity_simulations.end() || !found->second.mesh)
        return std::nullopt;
    if (found->second.xpbd) {
        project::MeshDeformationResult result;
        result.positions.reserve(found->second.xpbd->particles.size());
        for (const auto& particle : found->second.xpbd->particles)
            result.positions.push_back(particle.position);
        return result;
    }
    const auto nodes = evaluate_instance_nodes(instance_id, time);
    if (!nodes) return std::nullopt;
    std::vector<project::DeformationPose> poses;
    poses.reserve(nodes->size());
    for (const auto& node : *nodes)
        poses.push_back({.node_id = node.id, .transform = node.transform});
    return project::deform_mesh(*found->second.mesh, poses);
}

std::optional<project::XpbdSystem>
PreviewRuntime::instance_xpbd_state(const std::string& instance_id) const {
    if (!impl_) return std::nullopt;
    const auto found = impl_->entity_simulations.find(instance_id);
    if (found == impl_->entity_simulations.end() || !found->second.xpbd)
        return std::nullopt;
    return found->second.xpbd;
}

std::optional<std::vector<BehaviorAction>>
PreviewRuntime::evaluate_instance_behavior(const std::string& instance_id,
                                           const BehaviorSignal& signal,
                                           const float fixed_step_seconds) {
    if (!impl_) return std::nullopt;
    const auto found = impl_->entity_simulations.find(instance_id);
    if (found == impl_->entity_simulations.end() || !found->second.behavior)
        return std::nullopt;
    auto actions = found->second.behavior->evaluate(signal, fixed_step_seconds);
    impl_->apply_behavior_actions(instance_id, actions, fixed_step_seconds);
    impl_->flush_transformations();
    stats_.behavior_actions += actions.size();
    return actions;
}

std::optional<core::ResourceId> PreviewRuntime::instance_entity_id(
    const std::string& instance_id) const {
    if (!impl_) return std::nullopt;
    const auto found = impl_->entity_simulations.find(instance_id);
    if (found == impl_->entity_simulations.end()) return std::nullopt;
    return found->second.entity_id;
}

std::optional<project::BehaviorValue> PreviewRuntime::instance_property(
    const std::string& instance_id, const std::string_view property_id) const {
    if (!impl_) return std::nullopt;
    const auto simulation = impl_->entity_simulations.find(instance_id);
    if (simulation == impl_->entity_simulations.end()) return std::nullopt;
    const auto property = simulation->second.behavior_properties.find(
        std::string{property_id});
    return property == simulation->second.behavior_properties.end()
        ? std::nullopt : std::optional<project::BehaviorValue>{property->second};
}

bool PreviewRuntime::transform_instance(
    const std::string& instance_id,
    const core::ResourceId& transformation_id) {
    return impl_ && impl_->transform_entity_instance(
        instance_id, transformation_id);
}

const std::vector<BehaviorAction>& PreviewRuntime::behavior_actions() const noexcept {
    static const std::vector<BehaviorAction> empty;
    return impl_ ? impl_->behavior_actions : empty;
}

} // namespace fabric::runtime
