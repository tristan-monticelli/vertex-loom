#include "fabric/project/entity.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/map_chunk_index.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/runtime/preview_runtime.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task_info.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

struct Options {
    std::size_t instances{10000U};
    std::size_t frames{600U};
    double minimum_fps{};
    std::filesystem::path report;
    std::filesystem::path project;
    std::filesystem::path package;
    std::string map_id;
};

bool positive(const char* value, std::size_t& output) {
    try {
        const auto parsed = std::stoull(value);
        if (parsed == 0U) return false;
        output = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool non_negative(const char* value, double& output) {
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
        if (argument == "--instances" && index + 1 < argc) {
            if (!positive(argv[++index], options.instances)) return false;
        } else if (argument == "--frames" && index + 1 < argc) {
            if (!positive(argv[++index], options.frames)) return false;
        } else if (argument == "--min-fps" && index + 1 < argc) {
            if (!non_negative(argv[++index], options.minimum_fps)) return false;
        } else if (argument == "--report" && index + 1 < argc) {
            options.report = argv[++index];
            if (options.report.empty()) return false;
        } else if (argument == "--project" && index + 1 < argc) {
            options.project = argv[++index];
            if (options.project.empty()) return false;
        } else if (argument == "--package" && index + 1 < argc) {
            options.package = argv[++index];
            if (options.package.empty()) return false;
        } else if (argument == "--map" && index + 1 < argc) {
            options.map_id = argv[++index];
            if (options.map_id.empty()) return false;
        } else if (argument == "--help") {
            std::cout << "usage: fabric_runtime_benchmark [--instances N] [--frames N] [--min-fps N] [--report path] [--project path --map id | --package path]\n";
            return false;
        } else {
            return false;
        }
    }
    return true;
}

std::uint64_t peak_memory_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                             sizeof(counters)) == 0) return 0U;
    return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0U;
    return static_cast<std::uint64_t>(info.resident_size_max);
#elif defined(__linux__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0U;
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#else
    return 0U;
#endif
}

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "runtime-benchmark"},
            .name = "Runtime Benchmark"};
}

fabric::project::VectorAsset vector_asset() {
    return {.document = {.schema_version = 2,
                         .type = "vector",
                         .id = {.value = "benchmark-vector"},
                         .name = "Benchmark Vector"},
            .source_kind = fabric::project::VectorSourceKind::native,
            .native = fabric::project::NativeVectorDefinition{
                .size = {2.0F, 2.0F},
                .nodes = {{.id = "shape",
                           .name = "Shape",
                           .shape = {.id = "shape",
                                     .bounds = {{-1.0F, -1.0F}, {2.0F, 2.0F}}},
                           .fill = {.kind = fabric::project::VectorFillKind::solid,
                                    .color = fabric::core::Color{0.25F, 0.65F, 0.95F, 1.0F}}}}}};
}

fabric::project::EntityDefinition entity() {
    return {.document = {.schema_version = 1,
                         .type = "entity",
                         .id = {.value = "benchmark-entity"},
                         .name = "Benchmark Entity"},
            .nodes = {{.id = "root",
                       .name = "Root",
                       .drawable = {.kind = fabric::project::EntityDrawableKind::vector,
                                     .resource = fabric::project::ResourceReference{
                                         {.value = "benchmark-vector"}, "vector"}}}}};
}

fabric::project::MapDocument map(const std::size_t instances) {
    fabric::project::MapDocument result{
        .document = {.schema_version = 1,
                     .type = "map",
                     .id = {.value = "benchmark-map"},
                     .name = "Benchmark Map"},
        .layers = {{"instances", "Instances",
                    fabric::project::MapLayerKind::instances, true, false, 0.0F}}};
    const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(
        static_cast<double>(instances))));
    constexpr float spacing = 7.0F;
    const auto origin = static_cast<float>(columns - 1U) * spacing * 0.5F;
    result.instances.reserve(instances);
    for (std::size_t index = 0; index < instances; ++index) {
        const auto position = fabric::core::Vec2{
            static_cast<float>(index % columns) * spacing - origin,
            static_cast<float>(index / columns) * spacing - origin};
        const auto chunk = fabric::project::MapChunkIndex::chunk_for(position);
        result.instances.push_back({
            .id = "instance-" + std::to_string(index),
            .entity = fabric::project::ResourceReference{
                {.value = "benchmark-entity"}, "entity"},
            .layer_id = "instances",
            .transform = {.position = position},
            .chunk_x = chunk.first,
            .chunk_y = chunk.second});
    }
    return result;
}

} // namespace

int main(const int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) return 2;
    const auto benchmark_start = std::chrono::steady_clock::now();

    const auto generated_root = std::filesystem::temp_directory_path() /
        ("vertex-loom-runtime-benchmark-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const bool external_scene = !options.project.empty() || !options.package.empty();
    if (!options.project.empty() && !options.package.empty()) return 2;
    if (!options.project.empty() && options.map_id.empty()) return 2;
    const auto root = options.project.empty()
        ? generated_root
        : std::filesystem::absolute(options.project);
    const auto package_root = options.package.empty()
        ? std::optional<std::filesystem::path>{}
        : std::optional<std::filesystem::path>{
              std::filesystem::absolute(options.package)};
    const auto project_manifest = manifest();
    const auto fail = [&](const std::string_view message) {
        std::cerr << "error=" << message << '\n';
        std::error_code ignored;
        if (!external_scene) std::filesystem::remove_all(root, ignored);
        return 1;
    };
    if (!external_scene) {
        if (!fabric::project::create_project(root, project_manifest).ok())
            return fail("create_project");
        if (!fabric::project::publish_native_vector_asset(
                root, project_manifest, vector_asset()).ok())
            return fail("publish_vector");
        if (!fabric::project::publish_entity(root, project_manifest, entity()).ok())
            return fail("publish_entity");
        if (!fabric::project::publish_map(root, project_manifest, map(options.instances)).ok())
            return fail("publish_map");
    }

    fabric::runtime::PreviewRuntime runtime;
    const auto load_start = std::chrono::steady_clock::now();
    if (!runtime.load({.project_root = root,
                       .package_root = package_root,
                       .map_id = {.value = external_scene
                           ? (options.map_id.empty() ? "" : options.map_id)
                           : "benchmark-map"},
                       .mode = fabric::runtime::RuntimeMode::benchmark,
                       .frame_limit = options.frames})) {
        for (const auto& error : runtime.errors())
            std::cerr << "detail=" << error << '\n';
        return fail("runtime_load");
    }
    const auto load_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - load_start).count();
    if (!runtime.run()) {
        for (const auto& error : runtime.errors()) std::cerr << "detail=" << error << '\n';
        return fail("runtime_run");
    }

    const auto& stats = runtime.stats();
    const auto fps = stats.p95_frame_ms > 0.0 ? 1000.0 / stats.p95_frame_ms : 0.0;
    const bool passed = (external_scene
        ? stats.visible_instances > 0U
        : stats.visible_instances == options.instances) &&
        fps >= options.minimum_fps;
    if (!options.report.empty()) {
        std::ofstream report(options.report, std::ios::binary | std::ios::trunc);
        if (!report) return fail("report_open");
        report << "{\n"
               << "  \"instances\": " << options.instances << ",\n"
               << "  \"project\": \"" << options.project.generic_string() << "\",\n"
               << "  \"package\": \"" << options.package.generic_string() << "\",\n"
               << "  \"benchmarkSetupMs\": "
               << std::chrono::duration<double, std::milli>(
                      load_start - benchmark_start).count() << ",\n"
               << "  \"loadMs\": " << load_ms << ",\n"
               << "  \"peakMemoryBytes\": " << peak_memory_bytes() << ",\n"
               << "  \"map\": \"" << options.map_id << "\",\n"
               << "  \"framesRequested\": " << options.frames << ",\n"
               << "  \"minimumFps\": " << options.minimum_fps << ",\n"
               << "  \"visibleInstances\": " << stats.visible_instances << ",\n"
               << "  \"cullingCandidates\": " << stats.culling_candidates << ",\n"
               << "  \"culledPackets\": " << stats.culled_packets << ",\n"
               << "  \"directRenderFrames\": " << stats.direct_render_frames << ",\n"
               << "  \"frames\": " << stats.frames << ",\n"
               << "  \"drawCallsTotal\": " << stats.draw_calls << ",\n"
               << "  \"trianglesTotal\": " << stats.triangles << ",\n"
               << "  \"elapsedMs\": " << stats.elapsed_ms << ",\n"
               << "  \"p95FrameMs\": " << stats.p95_frame_ms << ",\n"
               << "  \"fpsP95\": " << fps << ",\n"
               << "  \"passed\": " << (passed ? "true" : "false") << "\n"
               << "}\n";
        if (!report) return fail("report_write");
    }
    std::cout << "instances=" << options.instances
              << " visible=" << stats.visible_instances
              << " culling_candidates=" << stats.culling_candidates
              << " culled_packets=" << stats.culled_packets
              << " direct_render_frames=" << stats.direct_render_frames
              << " frames=" << stats.frames
              << " draw_calls_total=" << stats.draw_calls
              << " triangles_total=" << stats.triangles
              << " load_ms=" << load_ms
              << " peak_memory_bytes=" << peak_memory_bytes()
              << " elapsed_ms=" << stats.elapsed_ms
              << " p95_frame_ms=" << stats.p95_frame_ms
              << " fps_p95=" << fps << '\n';
    std::error_code ignored;
    if (!external_scene) std::filesystem::remove_all(root, ignored);
    return passed ? 0 : 1;
}
