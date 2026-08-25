#include "fabric/render/opengl_vector_renderer.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

int skip(const char* message) {
    std::cerr << "SKIP: " << message << '\n';
    return 77;
}

} // namespace

int main() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return skip(SDL_GetError());

#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_Window* window = SDL_CreateWindow(
        "Vertex Loom OpenGL smoke", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        const auto* error = SDL_GetError();
        SDL_Quit();
        return skip(error);
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        const auto* error = SDL_GetError();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return skip(error);
    }
    SDL_GL_MakeCurrent(window, context);

    fabric::render::OpenGLVectorRenderer renderer;
    if (!renderer.initialize()) {
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const auto* shading = reinterpret_cast<const char*>(
            glGetString(GL_SHADING_LANGUAGE_VERSION));
        std::cerr << "OpenGL renderer initialization failed ("
                  << (version != nullptr ? version : "unknown") << ", GLSL "
                  << (shading != nullptr ? shading : "unknown") << ")\n";
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    fabric::render::VectorDrawPacket packet{
        .node_id = "smoke",
        .fill_color = fabric::core::Color{1.0F, 0.0F, 0.0F, 1.0F},
        .outline = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}},
        .fill_vertices = {{0.0F, 0.0F}, {1.0F, 0.0F},
                          {1.0F, 1.0F}, {0.0F, 1.0F}},
        .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
        .closed_outline = true,
    };
    auto second_packet = packet;
    second_packet.node_id = "smoke-2";
    const std::array packets{packet, second_packet};
    glViewport(0, 0, 64, 64);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    const auto stats = renderer.draw(
        std::span<const fabric::render::VectorDrawPacket>(packets),
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {0.0F, 0.0F}, .size = {1.0F, 1.0F}}});
    glFinish();
    std::array<std::uint8_t, 4> pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    const bool rendered = stats.ok() && stats.packets_drawn == 2U &&
        stats.draw_calls == 1U && stats.triangles_drawn == 4U &&
        pixel[0] > 200U && pixel[1] < 40U;
    const fabric::render::VectorDrawPacket clip{
        .node_id = "clip",
        .outline = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}},
        .fill_vertices = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}},
        .fill_indices = {0U, 1U, 2U},
        .closed_outline = true,
    };
    const fabric::render::VectorDrawPacket clipped{
        .node_id = "clipped",
        .fill_color = fabric::core::Color{0.0F, 1.0F, 0.0F, 1.0F},
        .outline = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}},
        .fill_vertices = {{0.0F, 0.0F}, {1.0F, 0.0F},
                          {1.0F, 1.0F}, {0.0F, 1.0F}},
        .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
        .clip_node_id = std::string{"clip"},
        .closed_outline = true,
    };
    const std::array clipped_packets{clip, clipped};
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto clipped_stats = renderer.draw(
        std::span<const fabric::render::VectorDrawPacket>(clipped_packets),
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {0.0F, 0.0F}, .size = {1.0F, 1.0F}}});
    glFinish();
    std::array<std::uint8_t, 4> clipped_inside{};
    std::array<std::uint8_t, 4> clipped_outside{};
    glReadPixels(48, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, clipped_inside.data());
    glReadPixels(8, 56, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, clipped_outside.data());
    GLint stencil_bits = 0;
    glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
    const bool clipping = stencil_bits == 0 ||
        (clipped_stats.ok() && clipped_stats.packets_drawn == 1U &&
         clipped_stats.triangles_drawn == 2U && clipped_inside[1] > 200U &&
         clipped_outside[1] < 40U);
    renderer.shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!rendered || !clipping) {
        std::cerr << "OpenGL smoke pixel or draw stats were invalid: "
                  << stats.packets_drawn << "/" << stats.triangles_drawn
                  << " pixel=" << static_cast<int>(pixel[0]) << ","
                  << static_cast<int>(pixel[1]) << ","
                  << static_cast<int>(pixel[2]) << " clipped="
                  << clipped_stats.packets_drawn << "/"
                  << clipped_stats.triangles_drawn << " inside="
                  << static_cast<int>(clipped_inside[0]) << ","
                  << static_cast<int>(clipped_inside[1]) << " outside="
                  << static_cast<int>(clipped_outside[0]) << ","
                  << static_cast<int>(clipped_outside[1]) << " stencil="
                  << stencil_bits << "\n";
        return 1;
    }
    return 0;
}
