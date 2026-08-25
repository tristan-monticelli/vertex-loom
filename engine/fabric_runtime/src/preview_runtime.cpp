#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/vector_asset.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <span>
#include <unordered_map>

namespace fabric::runtime {

struct PreviewRuntime::Impl {
    SDL_Window* window{};
    SDL_GLContext context{};
    render::OpenGLVectorRenderer renderer;
    std::vector<render::VectorDrawPacket> packets;
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

} // namespace

PreviewRuntime::PreviewRuntime() : impl_(std::make_unique<Impl>()) {}

PreviewRuntime::~PreviewRuntime() {
    if (impl_->context != nullptr) SDL_GL_DeleteContext(impl_->context);
    if (impl_->window != nullptr) SDL_DestroyWindow(impl_->window);
    if (impl_->sdl_initialized) SDL_Quit();
}

bool PreviewRuntime::load(const PreviewRuntimeOptions& options) {
    options_ = options;
    manifest_.reset();
    map_.reset();
    errors_.clear();
    stats_ = {};
    impl_->packets.clear();

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

    manifest_ = std::move(loaded_project.manifest);
    map_ = std::move(loaded_map.asset);
    if (!physics_.create() || !physics_.load_map_collisions(*map_)) {
        errors_.push_back("could not create the map physics world");
        map_.reset();
        manifest_.reset();
        return false;
    }

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
        for (std::size_t node_index = 0; node_index < entity.entity->nodes.size(); ++node_index) {
            const auto& node = entity.entity->nodes[node_index];
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
                    if (material) apply_material(packet, *material);
                    transform_packet(packet, *entity.entity, node_index, instance.transform);
                    packet.node_id = instance.id + ":" + node.id + ":" + packet.node_id;
                    impl_->packets.push_back(std::move(packet));
                }
            } else if (node.drawable.kind == project::EntityDrawableKind::texture) {
                // Texture upload is handled by the next resolver slice. Keep a
                // deterministic placeholder so the entity remains visible.
                constexpr std::array<core::Vec2, 4> square{
                    core::Vec2{-0.5F, -0.5F}, core::Vec2{0.5F, -0.5F},
                    core::Vec2{0.5F, 0.5F}, core::Vec2{-0.5F, 0.5F}};
                auto packet = render::VectorDrawPacket{
                    .node_id = node.id,
                    .fill_color = core::Color{0.7F, 0.7F, 0.7F, 1.0F},
                    .outline = std::vector<core::Vec2>(square.begin(), square.end()),
                    .fill_vertices = std::vector<core::Vec2>(square.begin(), square.end()),
                    .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
                    .closed_outline = true};
                transform_packet(packet, *entity.entity, node_index, instance.transform);
                packet.node_id = instance.id + ":" + node.id;
                impl_->packets.push_back(std::move(packet));
            }
        }
    }
    return true;
}

bool PreviewRuntime::run() {
    if (!loaded()) return false;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        errors_.push_back(SDL_GetError());
        return false;
    }
    impl_->sdl_initialized = true;

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

    const std::size_t limit = options_.frame_limit != 0U
        ? options_.frame_limit
        : (options_.mode == RuntimeMode::smoke_test ? 1U
           : options_.mode == RuntimeMode::benchmark ? 600U : 0U);
    bool running = true;
    while (running && (limit == 0U || stats_.frames < limit)) {
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) running = false;
        }
        if (!physics_.step(1.0F / 60.0F)) {
            errors_.push_back("physics step failed");
            return false;
        }

        const core::Rect bounds{{-static_cast<float>(options_.width) / 2.0F,
                                 -static_cast<float>(options_.height) / 2.0F},
                                {static_cast<float>(options_.width),
                                 static_cast<float>(options_.height)}};
        glViewport(0, 0, options_.width, options_.height);
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const auto render_stats = impl_->renderer.draw(
            std::span<const render::VectorDrawPacket>(impl_->packets),
            {.width = options_.width, .height = options_.height, .world_bounds = bounds});
        if (!render_stats.ok()) {
            errors_.insert(errors_.end(), render_stats.errors.begin(), render_stats.errors.end());
            return false;
        }
        stats_.visible_instances = render_stats.packets_drawn;
        stats_.draw_calls += render_stats.packets_drawn;
        stats_.triangles += render_stats.triangles_drawn;
        ++stats_.frames;
        SDL_GL_SwapWindow(impl_->window);
    }
    return true;
}

} // namespace fabric::runtime
