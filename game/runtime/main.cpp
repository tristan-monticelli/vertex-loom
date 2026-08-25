#include "fabric/runtime/preview_runtime.hpp"
#include "fabric/runtime/progress_store.hpp"
#include "fabric/project/scene.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void usage() {
    std::cerr << "usage: game_runtime --project <path> (--map <id> | --scene <id>) "
                 "[--replay <id>] "
                 "[--save-slot <slot>] "
                 "[--character] "
                 "[--audio <wav>] "
                 "[--smoke-test [frames]] [--benchmark [frames]]\n";
}

} // namespace

int main(int argc, char** argv) {
    fabric::runtime::PreviewRuntimeOptions options;
    std::optional<fabric::core::ResourceId> scene_id;
    std::optional<std::string> save_slot;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--project" && index + 1 < argc) {
            options.project_root = argv[++index];
        } else if (argument == "--map" && index + 1 < argc) {
            options.map_id.value = argv[++index];
        } else if (argument == "--scene" && index + 1 < argc) {
            scene_id = fabric::core::ResourceId{argv[++index]};
        } else if (argument == "--replay" && index + 1 < argc) {
            options.replay_id = fabric::core::ResourceId{argv[++index]};
        } else if (argument == "--save-slot" && index + 1 < argc) {
            save_slot = argv[++index];
        } else if (argument == "--character") {
            options.enable_character = true;
        } else if (argument == "--audio" && index + 1 < argc) {
            options.audio_wav = argv[++index];
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

    fabric::runtime::ProgressStore progress_store;
    if (save_slot) {
        if (!scene_id) {
            std::cerr << "error: --save-slot requires --scene\n";
            return 2;
        }
        if (!progress_store.configure_user_path("VertexLoom", "VertexLoom", *save_slot)) {
            for (const auto& error : progress_store.errors()) std::cerr << "error: " << error << '\n';
            return 1;
        }
        if (std::filesystem::exists(progress_store.path())) {
            fabric::project::ProgressSave existing;
            if (!progress_store.load(existing)) {
                for (const auto& error : progress_store.errors()) std::cerr << "error: " << error << '\n';
                return 1;
            }
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
    if (save_slot) {
        const fabric::project::ProgressSave progress{
            .schema_version = fabric::project::current_progress_save_schema_version,
            .build = "vertex-loom-runtime",
            .scene = {*scene_id, "scene"}};
        if (!progress_store.save(progress)) {
            for (const auto& error : progress_store.errors()) std::cerr << "error: " << error << '\n';
            return 1;
        }
    }
    if (options.mode != fabric::runtime::RuntimeMode::interactive) {
        std::cout << "frames=" << runtime.stats().frames
                  << " physics_steps=" << runtime.stats().physics_steps
                  << " visible=" << runtime.stats().visible_instances
                  << " draw_calls=" << runtime.stats().draw_calls
                  << " triangles=" << runtime.stats().triangles
                  << " elapsed_ms=" << runtime.stats().elapsed_ms
                  << " p95_frame_ms=" << runtime.stats().p95_frame_ms
                  << " replay_events=" << runtime.stats().replay_events
                  << " replay_checkpoints=" << runtime.stats().replay_checkpoints
                  << " character_x=" << runtime.stats().character_x
                  << " character_y=" << runtime.stats().character_y << '\n';
    }
    return 0;
}
