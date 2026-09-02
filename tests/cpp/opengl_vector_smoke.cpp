#include "fabric/render/opengl_vector_renderer.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path artifact_path(const std::string_view filename) {
    return std::filesystem::path{FABRIC_TEST_ARTIFACT_DIR} / filename;
}

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
    const bool nested_clipping = nested_stats.ok() &&
        nested_stats.packets_drawn == 1U && nested_inside[1] > 200U &&
        nested_outside[1] < 40U;
    GLuint texture = 0U;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    constexpr std::array<std::uint8_t, 8> reference_pixels{
        255U, 0U, 0U, 255U, 0U, 255U, 0U, 255U};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, reference_pixels.data());
    const fabric::render::VectorDrawPacket textured_stroke{
        .node_id = "textured-stroke",
        .stroke = fabric::project::VectorStroke{
            .color = fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F},
            .width = 0.2F,
            .image = fabric::project::VectorImageFill{
                .texture = {{.value = "reference-texture"}, "texture"}}},
        .stroke_vertices = {{-0.5F, -0.1F}, {0.5F, -0.1F},
                            {0.5F, 0.1F}, {-0.5F, 0.1F}},
        .stroke_indices = {0U, 1U, 2U, 0U, 2U, 3U},
        .stroke_image = fabric::project::VectorImageFill{
            .texture = {{.value = "reference-texture"}, "texture"}},
        .stroke_uv = {{0.0F, 0.0F}, {1.0F, 0.0F},
                      {1.0F, 1.0F}, {0.0F, 1.0F}},
    };
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto textured_stroke_stats = renderer.draw(
        std::span<const fabric::render::VectorDrawPacket>(&textured_stroke, 1),
        {.width = 64,
         .height = 64,
         .world_bounds = {.origin = {-0.5F, -0.5F}, .size = {1.0F, 1.0F}}},
        [texture](const fabric::core::ResourceId& id)
            -> std::optional<fabric::render::OpenGLTextureHandle> {
            if (id.value != "reference-texture") return std::nullopt;
            return fabric::render::OpenGLTextureHandle{
                .handle = texture, .width = 2U, .height = 1U};
        });
    glFinish();
    std::array<std::uint8_t, 4> stroke_left{};
    std::array<std::uint8_t, 4> stroke_right{};
    glReadPixels(16, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, stroke_left.data());
    glReadPixels(48, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, stroke_right.data());
    const bool textured_stroke_rendered = textured_stroke_stats.ok() &&
        textured_stroke_stats.packets_drawn == 1U &&
        stroke_left[0] > 180U && stroke_left[1] < 80U &&
        stroke_right[0] < 80U && stroke_right[1] > 180U;
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

    auto shader_packets = fabric::render::build_raster_view_draw_packets({
        .node_id = "shader-thread",
        .texture = {{.value = "reference-texture"}, "texture"},
        .source_width = 2U,
        .source_height = 1U,
        .pixels_per_unit = 2.0F,
    }).packets;
    shader_packets.front().shader = fabric::project::ShaderSurfaceSettings{
        .profile = fabric::project::SurfaceShaderProfile::thread,
        .classification = fabric::project::TextureClassification::beam,
        .primary_color = {0.1F, 0.4F, 1.0F, 1.0F},
        .effect_color = {1.0F, 0.8F, 0.1F, 1.0F},
        .holography = 1.0F,
    };
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const auto shader_stats = renderer.draw(
        shader_packets,
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
    std::array<std::uint8_t, 4> shader_left{};
    std::array<std::uint8_t, 4> shader_right{};
    glReadPixels(16, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 shader_left.data());
    glReadPixels(48, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 shader_right.data());
    std::vector<std::uint8_t> shader_capture(64U * 64U * 3U);
    std::vector<std::uint8_t> shader_rgba(64U * 64U * 4U);
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE,
                 shader_rgba.data());
    for (std::size_t index = 0; index < 64U * 64U; ++index) {
        shader_capture[index * 3U] = shader_rgba[index * 4U];
        shader_capture[index * 3U + 1U] = shader_rgba[index * 4U + 1U];
        shader_capture[index * 3U + 2U] = shader_rgba[index * 4U + 2U];
    }
    std::ofstream shader_screen{artifact_path("fabric-render-shader-smoke.ppm"),
                                std::ios::binary | std::ios::trunc};
    shader_screen << "P6\n64 64\n255\n";
    shader_screen.write(
        reinterpret_cast<const char*>(shader_capture.data()),
        static_cast<std::streamsize>(shader_capture.size()));
    const bool thread_shader_preserves_detail = shader_stats.ok() &&
        shader_left != shader_right &&
        (static_cast<int>(shader_left[0]) +
         static_cast<int>(shader_left[1]) +
         static_cast<int>(shader_left[2])) > 90 &&
        (static_cast<int>(shader_right[0]) +
         static_cast<int>(shader_right[1]) +
         static_cast<int>(shader_right[2])) > 90;

    const auto render_shader_pixel = [&](const fabric::project::ShaderSurfaceSettings& shader,
                                         const int x) {
        auto packets = shader_packets;
        packets.front().shader = shader;
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const auto draw = renderer.draw(
            packets,
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
        std::array<std::uint8_t, 4> value{};
        glReadPixels(x, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value.data());
        return std::pair{draw, value};
    };
    auto effect_red = *shader_packets.front().shader;
    effect_red.primary_color = {0.1F, 0.2F, 1.0F, 1.0F};
    effect_red.effect_color = {1.0F, 0.0F, 0.0F, 1.0F};
    effect_red.holography = 0.0F;
    effect_red.shine = 0.0F;
    auto effect_green = effect_red;
    effect_green.effect_color = {0.0F, 1.0F, 0.0F, 1.0F};
    const auto [effect_red_stats, effect_red_pixel] =
        render_shader_pixel(effect_red, 16);
    const auto [effect_green_stats, effect_green_pixel] =
        render_shader_pixel(effect_green, 16);
    const bool effect_disabled_without_holography = effect_red_stats.ok() &&
        effect_green_stats.ok() && effect_red_pixel == effect_green_pixel;

    effect_red.holography = 1.0F;
    effect_green.holography = 1.0F;
    const auto [holo_red_stats, holo_red_pixel] =
        render_shader_pixel(effect_red, 16);
    const auto [holo_green_stats, holo_green_pixel] =
        render_shader_pixel(effect_green, 16);
    const auto [holo_red_detail_stats, holo_red_detail] =
        render_shader_pixel(effect_red, 48);
    const bool holography_uses_selected_color = holo_red_stats.ok() &&
        holo_green_stats.ok() && holo_red_pixel[0] > holo_green_pixel[0] &&
        holo_green_pixel[1] > holo_red_pixel[1];
    const bool holography_preserves_detail = holo_red_detail_stats.ok() &&
        holo_red_detail != holo_red_pixel;

    auto neutral = effect_red;
    neutral.primary_color = {1.0F, 1.0F, 1.0F, 1.0F};
    neutral.effect_color = {1.0F, 1.0F, 1.0F, 1.0F};
    neutral.holography = 0.0F;
    const auto [neutral_left_stats, neutral_left] =
        render_shader_pixel(neutral, 16);
    const auto [neutral_right_stats, neutral_right] =
        render_shader_pixel(neutral, 48);
    const auto channels_are_neutral = [](const auto& pixel) {
        return std::abs(static_cast<int>(pixel[0]) - pixel[1]) <= 1 &&
            std::abs(static_cast<int>(pixel[1]) - pixel[2]) <= 1;
    };
    const bool neutral_thread_has_no_source_hue = neutral_left_stats.ok() &&
        neutral_right_stats.ok() && channels_are_neutral(neutral_left) &&
        channels_are_neutral(neutral_right) && neutral_left != neutral_right;

    auto preserve = neutral;
    preserve.profile = fabric::project::SurfaceShaderProfile::plastic;
    const auto [preserve_left_stats, preserve_left] =
        render_shader_pixel(preserve, 16);
    const auto [preserve_right_stats, preserve_right] =
        render_shader_pixel(preserve, 48);
    const bool image_color_mode_preserves_png = preserve_left_stats.ok() &&
        preserve_right_stats.ok() && preserve_left[0] > 200U &&
        preserve_left[1] < 40U && preserve_left[2] < 40U &&
        preserve_right[0] < 40U && preserve_right[1] > 200U &&
        preserve_right[2] < 40U;

    auto stacked_source = neutral;
    stacked_source.effects = {
        {.kind = fabric::project::SurfaceEffectKind::tint,
         .color = {1.0F, 1.0F, 1.0F, 1.0F}, .amount = 1.0F},
    };
    const auto [stacked_left_stats, stacked_left] =
        render_shader_pixel(stacked_source, 16);
    const auto [stacked_right_stats, stacked_right] =
        render_shader_pixel(stacked_source, 48);
    const bool effect_stack_preserves_png_detail =
        stacked_left_stats.ok() && stacked_right_stats.ok() &&
        std::abs(static_cast<int>(stacked_left[0]) - stacked_left[1]) <= 1 &&
        std::abs(static_cast<int>(stacked_left[1]) - stacked_left[2]) <= 1 &&
        std::abs(static_cast<int>(stacked_right[0]) - stacked_right[1]) <= 1 &&
        std::abs(static_cast<int>(stacked_right[1]) - stacked_right[2]) <= 1 &&
        stacked_left != stacked_right;

    auto cyan_stack = stacked_source;
    cyan_stack.effects.front().color = {0.1F, 0.8F, 1.0F, 1.0F};
    const auto [cyan_stack_stats, cyan_stack_pixel] =
        render_shader_pixel(cyan_stack, 16);
    const bool effect_stack_uses_selected_color =
        cyan_stack_stats.ok() && cyan_stack_pixel[2] > cyan_stack_pixel[0] &&
        cyan_stack_pixel[1] > cyan_stack_pixel[0];

    auto matte = effect_red;
    matte.primary_color = {0.15F, 0.15F, 0.15F, 1.0F};
    // Shine is driven by the selected effect color; use white here so the
    // test measures a visible, uniform lift independently of source hue.
    matte.effect_color = {1.0F, 1.0F, 1.0F, 1.0F};
    matte.shine = 0.0F;
    auto shiny = matte;
    shiny.shine = 1.0F;
    const auto [matte_left_stats, matte_left] = render_shader_pixel(matte, 16);
    const auto [matte_right_stats, matte_right] = render_shader_pixel(matte, 48);
    const auto [shiny_left_stats, shiny_left] = render_shader_pixel(shiny, 16);
    const auto [shiny_right_stats, shiny_right] = render_shader_pixel(shiny, 48);
    const auto rgb_sum = [](const std::array<std::uint8_t, 4>& value) {
        return static_cast<int>(value[0]) + static_cast<int>(value[1]) +
            static_cast<int>(value[2]);
    };
    const int shine_left_gain = rgb_sum(shiny_left) - rgb_sum(matte_left);
    const int shine_right_gain = rgb_sum(shiny_right) - rgb_sum(matte_right);
    const bool shine_is_uniform = matte_left_stats.ok() &&
        matte_right_stats.ok() && shiny_left_stats.ok() && shiny_right_stats.ok() &&
        shine_left_gain > 100 && shine_right_gain > 100 &&
        std::abs(shine_left_gain - shine_right_gain) <= 6;

    auto modular = neutral;
    modular.effects = {
        {.kind = fabric::project::SurfaceEffectKind::tint,
         .color = {1.0F, 0.15F, 0.15F, 1.0F}, .amount = 0.8F},
        {.kind = fabric::project::SurfaceEffectKind::holography,
         .color = {0.1F, 1.0F, 0.25F, 1.0F}, .amount = 1.0F,
         .scale = 1.5F},
        {.kind = fabric::project::SurfaceEffectKind::shine,
         .color = {0.15F, 0.25F, 1.0F, 1.0F}, .amount = 0.7F},
        {.kind = fabric::project::SurfaceEffectKind::tint,
         .enabled = false, .color = {0.0F, 0.0F, 0.0F, 1.0F}},
    };
    const auto [modular_stats, modular_pixel] =
        render_shader_pixel(modular, 16);
    auto reordered = modular;
    std::swap(reordered.effects[0], reordered.effects[1]);
    const auto [reordered_stats, reordered_pixel] =
        render_shader_pixel(reordered, 16);
    const bool modular_stack_is_ordered = modular_stats.ok() &&
        reordered_stats.ok() && modular.effects.size() > 2U &&
        modular_pixel != reordered_pixel;
    static_cast<void>(render_shader_pixel(modular, 16));
    std::vector<std::uint8_t> modular_rgba(64U * 64U * 4U);
    std::vector<std::uint8_t> modular_capture(64U * 64U * 3U);
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE,
                 modular_rgba.data());
    for (std::size_t index = 0; index < 64U * 64U; ++index) {
        modular_capture[index * 3U] = modular_rgba[index * 4U];
        modular_capture[index * 3U + 1U] = modular_rgba[index * 4U + 1U];
        modular_capture[index * 3U + 2U] = modular_rgba[index * 4U + 2U];
    }
    std::ofstream modular_screen{artifact_path("fabric-render-effect-stack-smoke.ppm"),
                                 std::ios::binary | std::ios::trunc};
    modular_screen << "P6\n64 64\n255\n";
    modular_screen.write(
        reinterpret_cast<const char*>(modular_capture.data()),
        static_cast<std::streamsize>(modular_capture.size()));

    glDeleteTextures(1, &texture);
    renderer.shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!rendered || !clipping || !nested_clipping ||
        !textured_stroke_rendered || !raster_crop || !texture_tint ||
        !texture_repeat || !thread_shader_preserves_detail ||
        !effect_disabled_without_holography ||
        !holography_uses_selected_color ||
        !holography_preserves_detail ||
        !neutral_thread_has_no_source_hue ||
        !image_color_mode_preserves_png || !effect_stack_preserves_png_detail ||
        !effect_stack_uses_selected_color || !shine_is_uniform ||
        !modular_stack_is_ordered) {
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
                  << static_cast<int>(nested_outside[1]) << " stroke="
                  << static_cast<int>(stroke_left[0]) << ","
                  << static_cast<int>(stroke_left[1]) << "/"
                  << static_cast<int>(stroke_right[0]) << ","
                  << static_cast<int>(stroke_right[1]) << " raster="
                  << raster_stats.packets_drawn << "/"
                  << static_cast<int>(raster_pixel[0]) << ","
                  << static_cast<int>(raster_pixel[1]) << ","
                  << static_cast<int>(raster_pixel[2]) << " tint="
                  << static_cast<int>(tinted_pixel[0]) << ","
                  << static_cast<int>(tinted_pixel[1]) << ","
                  << static_cast<int>(tinted_pixel[2]) << " repeat="
                  << static_cast<int>(repeated_pixel[0]) << ","
                  << static_cast<int>(repeated_pixel[1]) << ","
                  << static_cast<int>(repeated_pixel[2]) << " shader="
                  << static_cast<int>(shader_left[0]) << ","
                  << static_cast<int>(shader_left[1]) << ","
                  << static_cast<int>(shader_left[2]) << "/"
                  << static_cast<int>(shader_right[0]) << ","
                  << static_cast<int>(shader_right[1]) << ","
                  << static_cast<int>(shader_right[2]) << " effect="
                  << static_cast<int>(effect_red_pixel[0]) << ","
                  << static_cast<int>(effect_red_pixel[1]) << "/"
                  << static_cast<int>(effect_green_pixel[0]) << ","
                  << static_cast<int>(effect_green_pixel[1]) << " holo="
                  << static_cast<int>(holo_red_pixel[0]) << ","
                  << static_cast<int>(holo_red_pixel[1]) << "/"
                  << static_cast<int>(holo_green_pixel[0]) << ","
                  << static_cast<int>(holo_green_pixel[1]) << " neutral="
                  << static_cast<int>(neutral_left[0]) << ","
                  << static_cast<int>(neutral_left[1]) << ","
                  << static_cast<int>(neutral_left[2]) << "/"
                  << static_cast<int>(neutral_right[0]) << ","
                  << static_cast<int>(neutral_right[1]) << ","
                  << static_cast<int>(neutral_right[2]) << " preserve="
                  << static_cast<int>(preserve_left[0]) << ","
                  << static_cast<int>(preserve_left[1]) << "/"
                  << static_cast<int>(preserve_right[0]) << ","
                  << static_cast<int>(preserve_right[1]) << " shine="
                  << rgb_sum(matte_left) << "/" << rgb_sum(shiny_left) << ","
                  << rgb_sum(matte_right) << "/" << rgb_sum(shiny_right)
                  << " gains=" << shine_left_gain << "/" << shine_right_gain
                  << " modular=" << static_cast<int>(modular_pixel[0]) << ","
                  << static_cast<int>(modular_pixel[1]) << ","
                  << static_cast<int>(modular_pixel[2]) << " reordered="
                  << static_cast<int>(reordered_pixel[0]) << ","
                  << static_cast<int>(reordered_pixel[1]) << ","
                  << static_cast<int>(reordered_pixel[2])
                  << "\n";
        return 1;
    }
    return 0;
}
