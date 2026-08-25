#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/project/entity.hpp"
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

    struct EntitySimulation {
        std::optional<project::DeformationMesh> mesh;
        std::optional<project::XpbdSystem> xpbd;
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
    std::unordered_map<std::string, TextureSource> texture_sources;
    std::unordered_map<std::string, render::OpenGLTextureHandle> texture_handles;
    std::unordered_map<std::string, project::AnimationClip> animation_clips;
    std::unordered_map<std::string, std::string> animation_instances;
    std::unordered_map<std::string, PacketBaseTransform> packet_base_transforms;
    std::unordered_map<std::string, EntitySimulation> entity_simulations;
    project::MapChunkIndex chunk_index;
    std::unordered_map<std::string, std::vector<std::size_t>> packet_indices_by_instance;
    bool chunk_index_ready{};
    bool sdl_initialized{};
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
    map_.reset();
    replay_.reset();
    replay_player_.reset();
    input_ = {};
    character_.reset();
    triggers_.reset();
    errors_.clear();
    stats_ = {};
    impl_->packets.clear();
    impl_->texture_sources.clear();
    impl_->texture_handles.clear();
    impl_->animation_clips.clear();
    impl_->animation_instances.clear();
    impl_->packet_base_transforms.clear();
    impl_->entity_simulations.clear();
    impl_->packet_indices_by_instance.clear();
    impl_->chunk_index_ready = false;
    impl_->audio_clip.reset();

    if (options_.project_root.empty() || !core::ResourceId::is_valid(options_.map_id.value)) {
        errors_.push_back("project and a valid map id are required");
        return false;
    }

    // This is intentionally before SDL_Init and window creation.
    auto loaded_project = project::load_project(options_.project_root);
    if (!loaded_project.ok()) {
        append_errors(errors_, loaded_project.errors);
        return false;
    }
    auto loaded_map = project::load_map(
        options_.project_root, *loaded_project.manifest,
        project::map_document_path(*loaded_project.manifest, options_.map_id));
    if (!loaded_map.ok()) {
        append_errors(errors_, loaded_map.errors);
        return false;
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
        if (!resolve_constraints(resolved_entity.nodes, resolved_entity.constraints)) {
            errors_.push_back("entity constraints could not be resolved");
            return false;
        }
        if (resolved_entity.deformation_mesh || resolved_entity.xpbd) {
            Impl::EntitySimulation simulation{
                .mesh = resolved_entity.deformation_mesh,
                .xpbd = resolved_entity.xpbd,
                .instance_transform = instance.transform};
            simulation.poses.reserve(resolved_entity.nodes.size());
            for (const auto& node : resolved_entity.nodes)
                simulation.poses.push_back({.node_id = node.id,
                                            .transform = node.transform});
            impl_->entity_simulations.emplace(instance.id, std::move(simulation));
        }
        for (std::size_t node_index = 0; node_index < resolved_entity.nodes.size(); ++node_index) {
            const auto& node = resolved_entity.nodes[node_index];
            if (node.drawable.kind == project::EntityDrawableKind::vector &&
                node.drawable.resource) {
                auto vector = project::load_vector_asset(
                    options_.project_root, *manifest_,
                    project::vector_document_path(*manifest_, node.drawable.resource->id));
                if (!vector.ok()) {
                    append_errors(errors_, vector.errors);
                    return false;
                }
                project::VectorAsset drawable = std::move(*vector.asset);
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
                auto geometry = render::build_native_draw_packets(drawable);
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
                impl_->packets.push_back(std::move(packet));
            }
        }
    }
    stats_.deformation_instances = impl_->entity_simulations.size();
    std::stable_sort(impl_->packets.begin(), impl_->packets.end(),
                     [](const auto& left, const auto& right) {
                         return left.node_id < right.node_id;
                     });
    for (std::size_t index = 0; index < impl_->packets.size(); ++index) {
        const auto separator = impl_->packets[index].node_id.find(':');
        if (separator != std::string::npos)
            impl_->packet_indices_by_instance[impl_->packets[index].node_id.substr(0, separator)]
                .push_back(index);
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
    if (impl_->context == nullptr || !impl_->renderer.initialize()) {
        errors_.push_back(SDL_GetError());
        return false;
    }
    impl_->camera.set_viewport(options_.width, options_.height);
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
    while (running && (limit == 0U || stats_.frames < limit)) {
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
        impl_->camera.update(static_cast<float>(elapsed));
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
            if (character_) {
                const auto position = character_->position();
                stats_.character_x = position.x;
                stats_.character_y = position.y;
                if (triggers_)
                    stats_.gameplay_events += triggers_->update(position).size();
            }
            return true;
        };
        if (options_.mode == RuntimeMode::interactive) {
            while (accumulator >= fixed_time_step) {
                if (!step_physics()) return false;
                accumulator -= fixed_time_step;
            }
        } else if (!step_physics()) {
            return false;
        }

        const auto bounds = impl_->camera.world_bounds();
        const auto animation_time = static_cast<float>(stats_.physics_steps) *
            static_cast<float>(fixed_time_step);
        const auto animate_packet = [&](render::VectorDrawPacket packet) {
            const auto separator = packet.node_id.find(':');
            if (separator == std::string::npos) return packet;
            const auto instance_id = packet.node_id.substr(0, separator);
            const auto simulation = impl_->entity_simulations.find(instance_id);
            if (simulation != impl_->entity_simulations.end() &&
                simulation->second.mesh &&
                deformation_topology_matches(*simulation->second.mesh, packet)) {
                const auto deformation = evaluate_instance_deformation(instance_id);
                if (deformation && deformation->ok() &&
                    deformation->positions.size() == packet.fill_vertices.size()) {
                    for (std::size_t index = 0; index < packet.fill_vertices.size(); ++index)
                        packet.fill_vertices[index] = apply_transform(
                            deformation->positions[index],
                            simulation->second.instance_transform);
                    if (packet.outline.size() == deformation->positions.size())
                        for (std::size_t index = 0; index < packet.outline.size(); ++index)
                            packet.outline[index] = apply_transform(
                                deformation->positions[index],
                                simulation->second.instance_transform);
                    ++stats_.deformed_packets;
                }
            }
            const auto evaluation = evaluate_instance_animation(
                instance_id, animation_time);
            if (!evaluation || !evaluation->ok()) return packet;
            const auto second_separator = packet.node_id.find(':', separator + 1U);
            const auto node_id = packet.node_id.substr(
                separator + 1U,
                second_separator == std::string::npos
                    ? std::string::npos
                    : second_separator - separator - 1U);
            const auto base_transform = impl_->packet_base_transforms.find(packet.node_id);
            if (base_transform == impl_->packet_base_transforms.end()) return packet;
            std::optional<core::Vec2> position;
            std::optional<float> rotation_degrees;
            std::optional<core::Vec2> scale;
            std::optional<core::Color> color;
            std::optional<float> opacity;
            for (const auto& property : evaluation->properties) {
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
                        color = *value;
                } else if (property.binding.component_id == "material" &&
                           property.binding.property_id == "opacity") {
                    if (const auto* value = std::get_if<float>(&property.value))
                        opacity = *value;
                }
            }
            if (!position && !rotation_degrees && !scale && !color && !opacity)
                return packet;
            if (color && packet.fill_color) packet.fill_color = *color;
            if (opacity) {
                if (packet.fill_color) packet.fill_color->alpha = *opacity;
                if (packet.image_fill) packet.image_fill->opacity = *opacity;
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
        if (impl_->chunk_index_ready) {
            const auto candidate_instances = impl_->chunk_index.query(bounds);
            for (const auto& instance_id : candidate_instances) {
                const auto packet_indices = impl_->packet_indices_by_instance.find(instance_id);
                if (packet_indices == impl_->packet_indices_by_instance.end()) continue;
                for (const auto packet_index : packet_indices->second) {
                    const auto packet = animate_packet(impl_->packets[packet_index]);
                    if (packet_visible(packet, bounds)) visible_packets.push_back(packet);
                }
            }
        } else {
            visible_packets.reserve(impl_->packets.size());
            for (const auto& source_packet : impl_->packets) {
                const auto packet = animate_packet(source_packet);
                if (packet_visible(packet, bounds)) visible_packets.push_back(packet);
            }
        }
        glViewport(0, 0, options_.width, options_.height);
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const auto render_stats = impl_->renderer.draw(
            std::span<const render::VectorDrawPacket>(visible_packets),
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

std::optional<project::EvaluationResult> PreviewRuntime::evaluate_animation(
    const core::ResourceId& animation_id, const float time) const {
    if (!impl_) return std::nullopt;
    const auto found = impl_->animation_clips.find(animation_id.value);
    if (found == impl_->animation_clips.end()) return std::nullopt;
    return project::evaluate_animation(found->second, time);
}

std::optional<project::EvaluationResult> PreviewRuntime::evaluate_instance_animation(
    const std::string& instance_id, const float time) const {
    if (!impl_) return std::nullopt;
    const auto animation = impl_->animation_instances.find(instance_id);
    if (animation == impl_->animation_instances.end()) return std::nullopt;
    return evaluate_animation({.value = animation->second}, time);
}

std::optional<project::MeshDeformationResult>
PreviewRuntime::evaluate_instance_deformation(const std::string& instance_id) const {
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
    return project::deform_mesh(*found->second.mesh, found->second.poses);
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
