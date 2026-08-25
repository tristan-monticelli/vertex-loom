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
#include <string_view>

namespace {

struct Options {
    std::size_t instances{10000U};
    std::size_t frames{600U};
    double minimum_fps{};
    std::filesystem::path report;
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
        } else if (argument == "--help") {
            std::cout << "usage: fabric_runtime_benchmark [--instances N] [--frames N] [--min-fps N] [--report path]\n";
            return false;
        } else {
            return false;
        }
    }
    return true;
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

    const auto root = std::filesystem::temp_directory_path() /
        ("vertex-loom-runtime-benchmark-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    const auto fail = [&](const std::string_view message) {
        std::cerr << "error=" << message << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    };
    if (!fabric::project::create_project(root, project_manifest).ok())
        return fail("create_project");
    if (!fabric::project::publish_native_vector_asset(
            root, project_manifest, vector_asset()).ok())
        return fail("publish_vector");
    if (!fabric::project::publish_entity(root, project_manifest, entity()).ok())
        return fail("publish_entity");
    if (!fabric::project::publish_map(root, project_manifest, map(options.instances)).ok())
        return fail("publish_map");

    fabric::runtime::PreviewRuntime runtime;
    if (!runtime.load({.project_root = root,
                       .map_id = {.value = "benchmark-map"},
                       .mode = fabric::runtime::RuntimeMode::benchmark,
                       .frame_limit = options.frames}))
        return fail("runtime_load");
    if (!runtime.run()) return fail("runtime_run");

    const auto& stats = runtime.stats();
    const auto fps = stats.p95_frame_ms > 0.0 ? 1000.0 / stats.p95_frame_ms : 0.0;
    const bool passed = stats.visible_instances == options.instances &&
        fps >= options.minimum_fps;
    if (!options.report.empty()) {
        std::ofstream report(options.report, std::ios::binary | std::ios::trunc);
        if (!report) return fail("report_open");
        report << "{\n"
               << "  \"instances\": " << options.instances << ",\n"
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
              << " elapsed_ms=" << stats.elapsed_ms
              << " p95_frame_ms=" << stats.p95_frame_ms
              << " fps_p95=" << fps << '\n';
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return passed ? 0 : 1;
}
