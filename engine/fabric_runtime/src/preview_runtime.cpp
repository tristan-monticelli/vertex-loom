#include "fabric/runtime/preview_runtime.hpp"

#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/project/manifest.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <span>

namespace fabric::runtime {

struct PreviewRuntime::Impl {
    SDL_Window* window{};
    SDL_GLContext context{};
    render::OpenGLVectorRenderer renderer;
    bool sdl_initialized{};
};

namespace {

void append_errors(std::vector<std::string>& output,
                   const std::vector<project::Error>& errors) {
    for (const auto& error : errors)
        output.push_back(error.field + ": " + error.message);
}

render::VectorDrawPacket instance_packet(const project::MapInstance& instance) {
    const auto half_width = std::max(0.25F, std::abs(instance.transform.scale.x) * 0.5F);
    const auto half_height = std::max(0.25F, std::abs(instance.transform.scale.y) * 0.5F);
    const auto x = instance.transform.position.x;
    const auto y = instance.transform.position.y;
    return {.node_id = instance.id,
            .fill_color = core::Color{0.25F, 0.65F, 1.0F, 1.0F},
            .outline = {{x - half_width, y - half_height},
                        {x + half_width, y - half_height},
                        {x + half_width, y + half_height},
                        {x - half_width, y + half_height}},
            .fill_vertices = {{x - half_width, y - half_height},
                              {x + half_width, y - half_height},
                              {x + half_width, y + half_height},
                              {x - half_width, y + half_height}},
            .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
            .closed_outline = true};
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

        std::vector<render::VectorDrawPacket> packets;
        packets.reserve(map_->instances.size());
        for (const auto& instance : map_->instances) packets.push_back(instance_packet(instance));
        const core::Rect bounds{{-static_cast<float>(options_.width) / 2.0F,
                                 -static_cast<float>(options_.height) / 2.0F},
                                {static_cast<float>(options_.width),
                                 static_cast<float>(options_.height)}};
        glViewport(0, 0, options_.width, options_.height);
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const auto render_stats = impl_->renderer.draw(
            std::span<const render::VectorDrawPacket>(packets),
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
