#include "fabric/runtime/preview_runtime.hpp"
#include "fabric/project/scene.hpp"

#include <iostream>
#include <optional>
#include <string_view>

namespace {

void usage() {
    std::cerr << "usage: game_runtime --project <path> (--map <id> | --scene <id>) "
                 "[--smoke-test [frames]] [--benchmark [frames]]\n";
}

} // namespace

int main(int argc, char** argv) {
    fabric::runtime::PreviewRuntimeOptions options;
    std::optional<fabric::core::ResourceId> scene_id;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--project" && index + 1 < argc) {
            options.project_root = argv[++index];
        } else if (argument == "--map" && index + 1 < argc) {
            options.map_id.value = argv[++index];
        } else if (argument == "--scene" && index + 1 < argc) {
            scene_id = fabric::core::ResourceId{argv[++index]};
        } else if (argument == "--smoke-test") {
            options.mode = fabric::runtime::RuntimeMode::smoke_test;
            if (index + 1 < argc && argv[index + 1][0] != '-')
                options.frame_limit = static_cast<std::size_t>(std::stoull(argv[++index]));
        } else if (argument == "--benchmark") {
            options.mode = fabric::runtime::RuntimeMode::benchmark;
            if (index + 1 < argc && argv[index + 1][0] != '-')
                options.frame_limit = static_cast<std::size_t>(std::stoull(argv[++index]));
        } else if (argument == "--help") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    if (scene_id) {
        const auto project = fabric::project::load_project(options.project_root);
        if (!project.ok()) {
            for (const auto& error : project.errors)
                std::cerr << "error: " << error.field << ": " << error.message << '\n';
            return 1;
        }
        const auto scene = fabric::project::load_scene(
            options.project_root, *project.manifest,
            fabric::project::scene_document_path(*project.manifest, *scene_id));
        if (!scene.ok() || !scene.asset->entry_map) {
            if (scene.ok()) std::cerr << "error: scene has no entry map\n";
            else for (const auto& error : scene.errors)
                std::cerr << "error: " << error.field << ": " << error.message << '\n';
            return 1;
        }
        options.map_id = scene.asset->entry_map->id;
    }

    fabric::runtime::PreviewRuntime runtime;
    if (!runtime.load(options) || !runtime.run()) {
        for (const auto& error : runtime.errors()) std::cerr << "error: " << error << '\n';
        return 1;
    }
    if (options.mode != fabric::runtime::RuntimeMode::interactive) {
        std::cout << "frames=" << runtime.stats().frames
                  << " physics_steps=" << runtime.stats().physics_steps
                  << " visible=" << runtime.stats().visible_instances
                  << " draw_calls=" << runtime.stats().draw_calls
                  << " triangles=" << runtime.stats().triangles
                  << " elapsed_ms=" << runtime.stats().elapsed_ms
                  << " p95_frame_ms=" << runtime.stats().p95_frame_ms << '\n';
    }
    return 0;
}
