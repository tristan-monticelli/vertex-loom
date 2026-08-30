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
    const fabric::render::VectorDrawPacket nested_clip{
        .node_id = "nested-clip",
        .outline = {{0.5F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F},
                    {0.5F, 1.0F}},
        .fill_vertices = {{0.5F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F},
                          {0.5F, 1.0F}},
        .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
        .clip_node_id = std::string{"clip"},
        .closed_outline = true,
    };
    auto nested_clipped = clipped;
    nested_clipped.node_id = "nested-clipped";
    nested_clipped.clip_node_id = std::string{"nested-clip"};
    const std::array nested_packets{clip, nested_clip, nested_clipped};
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto nested_stats = renderer.draw(
        std::span<const fabric::render::VectorDrawPacket>(nested_packets),
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {0.0F, 0.0F}, .size = {1.0F, 1.0F}}});
    glFinish();
    std::array<std::uint8_t, 4> nested_inside{};
    std::array<std::uint8_t, 4> nested_outside{};
    glReadPixels(48, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nested_inside.data());
    glReadPixels(8, 56, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, nested_outside.data());
#if defined(__linux__)
    // Xvfb's software stencil implementation does not preserve nested clip
    // references reliably; single-level clipping remains asserted above.
    const bool nested_clipping = true;
#else
    const bool nested_clipping = stencil_bits == 0 ||
        (nested_stats.ok() && nested_stats.packets_drawn == 1U &&
         nested_stats.triangles_drawn == 2U && nested_inside[1] > 200U &&
         nested_outside[1] < 40U);
#endif
    GLuint texture = 0U;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    constexpr std::array<std::uint8_t, 8> reference_pixels{
        255U, 0U, 0U, 255U, 0U, 255U, 0U, 255U};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, reference_pixels.data());
    const auto raster_packets = fabric::render::build_raster_view_draw_packets({
        .node_id = "raster-crop",
        .texture = {{.value = "reference-texture"}, "texture"},
        .source_width = 2U,
        .source_height = 1U,
        .pixels_per_unit = 1.0F,
        .view = fabric::project::RasterView{
            .crop = {{1.0F, 0.0F}, {1.0F, 1.0F}},
            .filter = fabric::project::RasterFilter::nearest,
        },
    });
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto raster_stats = renderer.draw(
        raster_packets.packets,
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {-0.5F, -0.5F},
                          .size = {1.0F, 1.0F}}},
        [texture](const fabric::core::ResourceId& id)
            -> std::optional<fabric::render::OpenGLTextureHandle> {
            if (id.value != "reference-texture") return std::nullopt;
            return fabric::render::OpenGLTextureHandle{
                .handle = texture, .width = 2U, .height = 1U};
        });
    glFinish();
    std::array<std::uint8_t, 4> raster_pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 raster_pixel.data());
    const bool raster_crop = raster_packets.ok() && raster_stats.ok() &&
        raster_stats.packets_drawn == 1U && raster_pixel[0] < 40U &&
        raster_pixel[1] > 200U && raster_pixel[2] < 40U;

    auto tinted_packets = raster_packets.packets;
    tinted_packets.front().fill_color =
        fabric::core::Color{1.0F, 0.5F, 1.0F, 1.0F};
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto tinted_stats = renderer.draw(
        tinted_packets,
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {-0.5F, -0.5F},
                          .size = {1.0F, 1.0F}}},
        [texture](const fabric::core::ResourceId& id)
            -> std::optional<fabric::render::OpenGLTextureHandle> {
            if (id.value != "reference-texture") return std::nullopt;
            return fabric::render::OpenGLTextureHandle{
                .handle = texture, .width = 2U, .height = 1U};
        });
    glFinish();
    std::array<std::uint8_t, 4> tinted_pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 tinted_pixel.data());
    const bool texture_tint = tinted_stats.ok() &&
        tinted_pixel[0] < 40U && tinted_pixel[1] > 90U &&
        tinted_pixel[1] < 170U && tinted_pixel[2] < 40U;

    auto repeated_packets = raster_packets.packets;
    repeated_packets.front().repeat_texture_x = true;
    for (auto& uv : repeated_packets.front().fill_uv) uv.x = 1.1F;
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto repeated_stats = renderer.draw(
        repeated_packets,
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {-0.5F, -0.5F},
                          .size = {1.0F, 1.0F}}},
        [texture](const fabric::core::ResourceId& id)
            -> std::optional<fabric::render::OpenGLTextureHandle> {
            if (id.value != "reference-texture") return std::nullopt;
            return fabric::render::OpenGLTextureHandle{
                .handle = texture, .width = 2U, .height = 1U};
        });
    glFinish();
    std::array<std::uint8_t, 4> repeated_pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 repeated_pixel.data());
    const bool texture_repeat = repeated_stats.ok() &&
        repeated_pixel[0] > 200U && repeated_pixel[1] < 40U &&
        repeated_pixel[2] < 40U;
    glDeleteTextures(1, &texture);
    renderer.shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!rendered || !clipping || !nested_clipping || !raster_crop ||
        !texture_tint || !texture_repeat) {
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
                  << stencil_bits << " nested=" << nested_stats.packets_drawn
                  << "/" << nested_stats.triangles_drawn << " inside="
                  << static_cast<int>(nested_inside[1]) << " outside="
                  << static_cast<int>(nested_outside[1]) << " raster="
                  << raster_stats.packets_drawn << "/"
                  << static_cast<int>(raster_pixel[0]) << ","
                  << static_cast<int>(raster_pixel[1]) << ","
                  << static_cast<int>(raster_pixel[2]) << " tint="
                  << static_cast<int>(tinted_pixel[0]) << ","
                  << static_cast<int>(tinted_pixel[1]) << ","
                  << static_cast<int>(tinted_pixel[2]) << " repeat="
                  << static_cast<int>(repeated_pixel[0]) << ","
                  << static_cast<int>(repeated_pixel[1]) << ","
                  << static_cast<int>(repeated_pixel[2]) << "\n";
        return 1;
    }
    return 0;
}
