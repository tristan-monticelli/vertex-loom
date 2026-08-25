#include "fabric/render/opengl_vector_renderer.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::size_t packets{10000U};
    std::size_t frames{600U};
    std::int32_t width{1440};
    std::int32_t height{900};
    double minimum_fps{};
    std::filesystem::path report;
};

bool parse_positive(const char* value, std::size_t& output) {
    try {
        const auto parsed = std::stoull(value);
        if (parsed == 0U) return false;
        output = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_non_negative(const char* value, double& output) {
    try {
        const auto parsed = std::stod(value);
        if (!std::isfinite(parsed) || parsed < 0.0) return false;
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_options(const int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--packets" && index + 1 < argc) {
            if (!parse_positive(argv[++index], options.packets)) return false;
        } else if (argument == "--frames" && index + 1 < argc) {
            if (!parse_positive(argv[++index], options.frames)) return false;
        } else if (argument == "--min-fps" && index + 1 < argc) {
            if (!parse_non_negative(argv[++index], options.minimum_fps)) return false;
        } else if (argument == "--report" && index + 1 < argc) {
            options.report = argv[++index];
            if (options.report.empty()) return false;
        } else if (argument == "--help") {
            std::cout << "usage: fabric_render_benchmark [--packets N] [--frames N] [--min-fps N] [--report path]\n";
            return false;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) return 2;

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "error=SDL_Init message=" << SDL_GetError() << '\n';
        return 1;
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
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    auto* window = SDL_CreateWindow("Vertex Loom Render Benchmark",
                                    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    options.width, options.height,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::cerr << "error=SDL_CreateWindow message=" << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    auto context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << "error=SDL_GL_CreateContext message=" << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);

    fabric::render::OpenGLVectorRenderer renderer;
    if (!renderer.initialize()) {
        std::cerr << "error=renderer_initialize detail="
                  << renderer.initialization_error() << '\n';
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::vector<fabric::render::VectorDrawPacket> packets;
    packets.reserve(options.packets);
    const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(
        static_cast<double>(options.packets))));
    const auto rows = (options.packets + columns - 1U) / columns;
    const auto cell_width = static_cast<float>(options.width) /
        static_cast<float>(columns);
    const auto cell_height = static_cast<float>(options.height) /
        static_cast<float>(rows);
    const auto color = fabric::core::Color{0.25F, 0.65F, 0.95F, 1.0F};
    for (std::size_t index = 0; index < options.packets; ++index) {
        const auto column = index % columns;
        const auto row = index / columns;
        const auto left = static_cast<float>(column) * cell_width;
        const auto bottom = static_cast<float>(row) * cell_height;
        packets.push_back({
            .node_id = "benchmark-" + std::to_string(index),
            .fill_color = color,
            .outline = {{left, bottom}, {left + cell_width, bottom},
                        {left + cell_width, bottom + cell_height}, {left, bottom + cell_height}},
            .fill_vertices = {{left, bottom}, {left + cell_width, bottom},
                              {left + cell_width, bottom + cell_height}, {left, bottom + cell_height}},
            .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
            .closed_outline = true});
    }

    std::vector<double> frame_times;
    frame_times.reserve(options.frames);
    std::uint64_t total_draw_calls = 0U;
    std::uint64_t total_triangles = 0U;
    std::uint32_t packets_drawn = 0U;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t frame = 0; frame < options.frames; ++frame) {
        const auto frame_start = std::chrono::steady_clock::now();
        glViewport(0, 0, options.width, options.height);
        glClearColor(0.02F, 0.02F, 0.03F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const auto stats = renderer.draw(
            std::span<const fabric::render::VectorDrawPacket>(packets),
            {.width = options.width, .height = options.height,
             .world_bounds = {.origin = {0.0F, 0.0F},
                              .size = {static_cast<float>(options.width),
                                       static_cast<float>(options.height)}}});
        if (!stats.ok()) {
            for (const auto& error : stats.errors) std::cerr << "error=" << error << '\n';
            renderer.shutdown();
            SDL_GL_DeleteContext(context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        packets_drawn = stats.packets_drawn;
        total_draw_calls += stats.draw_calls;
        total_triangles += stats.triangles_drawn;
        SDL_GL_SwapWindow(window);
        frame_times.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - frame_start).count());
    }
    const auto elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::ranges::sort(frame_times);
    const auto p95_index = std::min(frame_times.size() - 1U,
        static_cast<std::size_t>(std::ceil(static_cast<double>(frame_times.size()) * 0.95)) - 1U);
    const auto p95 = frame_times[p95_index];
    const auto fps = p95 > 0.0 ? 1000.0 / p95 : 0.0;
    const bool passed = packets_drawn == options.packets && fps >= options.minimum_fps;
    if (!options.report.empty()) {
        std::ofstream report(options.report, std::ios::binary | std::ios::trunc);
        if (!report) {
            std::cerr << "error=report_open\n";
            renderer.shutdown();
            SDL_GL_DeleteContext(context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        report << "{\n"
               << "  \"packets\": " << options.packets << ",\n"
               << "  \"framesRequested\": " << options.frames << ",\n"
               << "  \"minimumFps\": " << options.minimum_fps << ",\n"
               << "  \"packetsDrawn\": " << packets_drawn << ",\n"
               << "  \"frames\": " << options.frames << ",\n"
               << "  \"drawCallsTotal\": " << total_draw_calls << ",\n"
               << "  \"trianglesTotal\": " << total_triangles << ",\n"
               << "  \"elapsedMs\": " << elapsed_ms << ",\n"
               << "  \"p95FrameMs\": " << p95 << ",\n"
               << "  \"fpsP95\": " << fps << ",\n"
               << "  \"passed\": " << (passed ? "true" : "false") << "\n"
               << "}\n";
        if (!report) {
            std::cerr << "error=report_write\n";
            renderer.shutdown();
            SDL_GL_DeleteContext(context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }
    std::cout << "packets=" << options.packets
              << " packets_drawn=" << packets_drawn
              << " frames=" << options.frames
              << " draw_calls_total=" << total_draw_calls
              << " triangles_total=" << total_triangles
              << " elapsed_ms=" << elapsed_ms
              << " p95_frame_ms=" << p95
              << " fps_p95=" << fps << '\n';
    renderer.shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return passed ? 0 : 1;
}
