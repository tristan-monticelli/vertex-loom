#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/render/vector_geometry.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/animation_ik.hpp"
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
        std::optional<project::DeformationMesh> mesh;
        std::optional<project::XpbdSystem> xpbd;
        std::vector<core::Vec2> previous_xpbd_positions;
        std::vector<core::Vec2> interpolated_xpbd_positions;
        std::vector<project::EntityNode> nodes;
        std::vector<project::AnimationConstraint> constraints;
        std::vector<project::FabrikChainDefinition> ik_chains;
        std::vector<project::DeformationPose> poses;
        core::Transform instance_transform;
    };

    SDL_Window* window{};
    SDL_GLContext context{};
    render::OpenGLVectorRenderer renderer;
    PcmAudioMixer audio_mixer;
    PcmAudioDevice audio_device;
    std::optional<PcmWavClip> audio_clip;
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
    std::unordered_map<std::string, EntitySimulation> entity_simulations;
    std::vector<GameplayEvent> gameplay_events;
    project::MapChunkIndex chunk_index;
    std::unordered_map<std::string, std::vector<std::size_t>> packet_indices_by_instance;
    bool chunk_index_ready{};
    bool sdl_initialized{};

    void begin_evaluation_cache(const float time) const {
        if (evaluation_cache_valid && evaluation_cache_time == time) return;
        evaluation_cache_valid = true;
        evaluation_cache_time = time;
        animation_evaluation_cache.clear();
        node_evaluation_cache.clear();
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
        return ResolvedAnimation{.clip_id = clip->first, .local_time = local_time};
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
    impl_->entity_simulations.clear();
    impl_->gameplay_events.clear();
    impl_->packet_indices_by_instance.clear();
    impl_->chunk_index_ready = false;
    impl_->audio_clip.reset();

    const bool valid_map_id = core::ResourceId::is_valid(options_.map_id.value);
    const bool valid_scene_id = options_.scene_id.has_value() &&
        core::ResourceId::is_valid(options_.scene_id->value);
    if (options_.project_root.empty() ||
        (options_.scene_id.has_value() && !valid_scene_id) ||
        (!options_.scene_id.has_value() && !valid_map_id) ||
        (options_.scene_id.has_value() && valid_map_id)) {
        errors_.push_back("project and exactly one valid map or scene id are required");
        return false;
    }

    // This is intentionally before SDL_Init and window creation.
    auto loaded_project = project::load_project(options_.project_root);
    if (!loaded_project.ok()) {
        append_errors(errors_, loaded_project.errors);
        return false;
    }
    std::optional<project::SceneDocument> loaded_scene;
    auto map_id = options_.map_id;
    if (options_.scene_id) {
        auto scene = project::load_scene(
            options_.project_root, *loaded_project.manifest,
            project::scene_document_path(*loaded_project.manifest, *options_.scene_id));
        if (!scene.ok()) {
            append_errors(errors_, scene.errors);
            return false;
        }
        if (!scene.asset->entry_map) {
            errors_.push_back("scene has no entry map");
            return false;
        }
        map_id = scene.asset->entry_map->id;
        loaded_scene = std::move(scene.asset);
    }
    auto loaded_map = project::load_map(
        options_.project_root, *loaded_project.manifest,
        project::map_document_path(*loaded_project.manifest, map_id));
    if (!loaded_map.ok()) {
        append_errors(errors_, loaded_map.errors);
        return false;
    }

    std::optional<project::InputDocument> loaded_input;
    if (options_.enable_character && options_.input_actions.empty()) {
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
    map_ = std::move(loaded_map.asset);
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
    if (options_.enable_character) {
        if (!options_.input_actions.empty()) {
            if (!input_.configure(options_.input_actions)) {
                errors_.push_back("could not configure character input actions");
                return false;
            }
        } else if (loaded_input) {
            if (!input_.configure(loaded_input->actions)) {
                errors_.push_back("could not configure persisted character input actions");
                return false;
            }
        } else {
            for (const auto action : {"move_left", "move_right", "jump"})
                if (!input_.define_action(action)) {
                    errors_.push_back("could not define character input action");
                    return false;
                }
            if (!input_.bind("move_left", {InputDevice::keyboard, SDLK_a}) ||
                !input_.bind("move_left", {InputDevice::keyboard, SDLK_LEFT}) ||
                !input_.bind("move_right", {InputDevice::keyboard, SDLK_d}) ||
                !input_.bind("move_right", {InputDevice::keyboard, SDLK_RIGHT}) ||
                !input_.bind("jump", {InputDevice::keyboard, SDLK_SPACE}) ||
                !input_.bind("move_left", {InputDevice::gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT}) ||
                !input_.bind("move_right", {InputDevice::gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT}) ||
                !input_.bind("jump", {InputDevice::gamepad, SDL_CONTROLLER_BUTTON_A})) {
                errors_.push_back("could not bind character input actions");
                return false;
            }
        }
        for (const auto action : {"move_left", "move_right", "jump"}) {
            const auto found = std::find_if(input_.actions().begin(), input_.actions().end(),
                [&](const auto& definition) { return definition.id == action; });
            if (found == input_.actions().end()) {
                errors_.push_back("character input action is missing: " + std::string(action));
                return false;
            }
        }
        character_ = std::make_unique<CharacterController>();
        if (!character_->create(physics_, {0.0F, 0.0F})) {
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
                                .height = texture.asset->height});
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
            .mesh = resolved_entity.deformation_mesh,
            .xpbd = resolved_entity.xpbd,
            .nodes = resolved_entity.nodes,
            .constraints = resolved_entity.constraints,
            .ik_chains = resolved_entity.ik_chains,
            .instance_transform = instance.transform};
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
            } else if (node.drawable.kind == project::EntityDrawableKind::texture) {
                // Texture upload is handled by the next resolver slice. Keep a
                // deterministic placeholder so the entity remains visible.
                constexpr std::array<core::Vec2, 4> uv_quad{
                    core::Vec2{0.0F, 0.0F}, core::Vec2{1.0F, 0.0F},
                    core::Vec2{1.0F, 1.0F}, core::Vec2{0.0F, 1.0F}};
                if (!node.drawable.resource || !ensure_texture(*node.drawable.resource))
                    return false;
                const auto& source = impl_->texture_sources.at(node.drawable.resource->id.value);
                const auto pixels_per_unit = static_cast<float>(manifest_->pixels_per_unit);
                const auto half_width = std::max(0.5F,
                    static_cast<float>(source.width) / pixels_per_unit * 0.5F);
                const auto half_height = std::max(0.5F,
                    static_cast<float>(source.height) / pixels_per_unit * 0.5F);
                const std::array<core::Vec2, 4> texture_quad{
                    core::Vec2{-half_width, -half_height},
                    core::Vec2{half_width, -half_height},
                    core::Vec2{half_width, half_height},
                    core::Vec2{-half_width, half_height}};
                auto packet = render::VectorDrawPacket{
                    .node_id = node.id,
                    .fill_color = core::Color{0.7F, 0.7F, 0.7F, 1.0F},
                    .image_fill = project::VectorImageFill{
                        .texture = *node.drawable.resource},
                    .outline = std::vector<core::Vec2>(texture_quad.begin(), texture_quad.end()),
                    .fill_vertices = std::vector<core::Vec2>(texture_quad.begin(), texture_quad.end()),
                    .fill_uv = std::vector<core::Vec2>(uv_quad.begin(), uv_quad.end()),
                    .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
                    .closed_outline = true};
                packet.fill_color.reset();
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
             impl_->animation_state_machines.contains(instance_id));
        impl_->packet_bounds_dynamic.push_back(dynamic);
    }
    return true;
}

bool PreviewRuntime::run() {
    if (!loaded()) return false;
    SDL_SetMainReady();
    const auto sdl_flags = SDL_INIT_VIDEO |
        (options_.enable_character ? SDL_INIT_GAMECONTROLLER : 0U);
    if (SDL_Init(sdl_flags) != 0) {
        errors_.push_back(SDL_GetError());
        return false;
    }
    impl_->sdl_initialized = true;
    if (options_.enable_character) {
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (!SDL_IsGameController(index)) continue;
            impl_->controller = SDL_GameControllerOpen(index);
            if (impl_->controller != nullptr) break;
        }
    }
    if (!impl_->texture_sources.empty() && (IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        errors_.push_back(IMG_GetError());
        return false;
    }

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
    impl_->camera.set_viewport(options_.width, options_.height);
    impl_->camera.set_limits(options_.camera_limits);
    if (impl_->audio_clip) {
        if (!impl_->audio_mixer.configure(impl_->audio_clip->sample_rate,
                                          impl_->audio_clip->channels) ||
            !impl_->audio_device.open(impl_->audio_clip->sample_rate,
                                      impl_->audio_clip->channels) ||
            !impl_->audio_mixer.play(*impl_->audio_clip)) {
            errors_.push_back("audio: " + (impl_->audio_device.error().empty()
                ? std::string("could not start PCM playback")
                : impl_->audio_device.error()));
            return false;
        }
    }

    for (const auto& [id, source] : impl_->texture_sources) {
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
        if (options_.enable_character && !replay_player_) input_.begin_frame();
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_MOUSEWHEEL && options_.mode == RuntimeMode::interactive)
                impl_->camera.zoom_at(
                    {static_cast<float>(event.wheel.mouseX),
                     static_cast<float>(event.wheel.mouseY)},
                    std::pow(1.1F, static_cast<float>(event.wheel.y)));
            if (!options_.enable_character || replay_player_) continue;
            if (event.type == SDL_KEYDOWN)
                input_.press(InputDevice::keyboard, event.key.keysym.sym,
                             event.key.repeat != 0);
            else if (event.type == SDL_KEYUP)
                input_.release(InputDevice::keyboard, event.key.keysym.sym);
            else if (event.type == SDL_CONTROLLERBUTTONDOWN)
                input_.press(InputDevice::gamepad, event.cbutton.button);
            else if (event.type == SDL_CONTROLLERBUTTONUP)
                input_.release(InputDevice::gamepad, event.cbutton.button);
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
            if (character_) character_->update(input_, static_cast<float>(fixed_time_step));
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
            if (character_) {
                const auto position = character_->position();
                stats_.character_x = position.x;
                stats_.character_y = position.y;
                if (triggers_) {
                    impl_->gameplay_events = triggers_->update(position);
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
        std::unordered_map<std::string, std::vector<project::EntityNode>> evaluated_nodes;
        const auto animate_packet = [&](render::VectorDrawPacket packet) {
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
                } else if (property.binding.component_id == "material" &&
                           property.binding.property_id == "color") {
                    if (const auto* value = std::get_if<core::Color>(&property.value))
                        color = *value,
                        color_composition = property.composition;
                } else if (property.binding.component_id == "material" &&
                           property.binding.property_id == "opacity") {
                    if (const auto* value = std::get_if<float>(&property.value))
                        opacity = *value,
                        opacity_composition = property.composition;
                }
            }
            if (resolved_node) {
                position = resolved_node->transform.position;
                rotation_degrees = resolved_node->transform.rotation_degrees;
                scale = resolved_node->transform.scale;
            }
            if (!position && !rotation_degrees && !scale && !color && !opacity)
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
        if (impl_->audio_clip) {
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

const std::vector<GameplayEvent>& PreviewRuntime::gameplay_events() const noexcept {
    static const std::vector<GameplayEvent> empty;
    return impl_ ? impl_->gameplay_events : empty;
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

} // namespace fabric::runtime
