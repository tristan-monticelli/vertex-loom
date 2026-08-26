#include "fabric/runtime/preview_runtime.hpp"
#include "fabric/runtime/progress_store.hpp"
#include "fabric/runtime/scene_session.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void usage() {
    std::cerr << "usage: game_runtime (--project <path> (--map <id> | --scene <id>) | "
                 "--package <path>) "
                 "[--replay <id>] "
                 "[--save-slot <slot>] "
                 "[--save-path <file>] "
                 "[--character] "
                 "[--input <id>] "
                 "[--follow-character] "
                 "[--camera-limits <x> <y> <width> <height>] "
                 "[--bind <action> <keyboard|gamepad> <code>]... "
                 "[--audio <wav>] "
                 "[--smoke-test [frames]] [--benchmark [frames]]\n";
}

bool parse_float(const char* value, float& output) {
    try {
        std::size_t consumed = 0;
        const std::string text(value);
        output = std::stof(text, &consumed);
        return consumed == text.size() && std::isfinite(output);
    } catch (...) {
        return false;
    }
}

bool parse_non_negative_int(const char* value, int& output) {
    try {
        std::size_t consumed = 0;
        const std::string text(value);
        output = std::stoi(text, &consumed);
        return consumed == text.size() && output >= 0;
    } catch (...) {
        return false;
    }
}

std::optional<fabric::runtime::InputDevice> parse_input_device(
    const std::string_view value) {
    if (value == "keyboard") return fabric::runtime::InputDevice::keyboard;
    if (value == "gamepad") return fabric::runtime::InputDevice::gamepad;
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    fabric::runtime::PreviewRuntimeOptions options;
    std::optional<fabric::core::ResourceId> scene_id;
    std::optional<std::string> save_slot;
    std::optional<std::filesystem::path> save_path;
    std::vector<fabric::runtime::InputActionDefinition> configured_actions;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--project" && index + 1 < argc) {
            options.project_root = argv[++index];
        } else if (argument == "--package" && index + 1 < argc) {
            options.package_root = std::filesystem::path(argv[++index]);
        } else if (argument == "--map" && index + 1 < argc) {
            options.map_id.value = argv[++index];
        } else if (argument == "--scene" && index + 1 < argc) {
            scene_id = fabric::core::ResourceId{argv[++index]};
        } else if (argument == "--replay" && index + 1 < argc) {
            options.replay_id = fabric::core::ResourceId{argv[++index]};
        } else if (argument == "--input" && index + 1 < argc) {
            options.input_id = fabric::core::ResourceId{argv[++index]};
        } else if (argument == "--save-slot" && index + 1 < argc) {
            save_slot = argv[++index];
        } else if (argument == "--save-path" && index + 1 < argc) {
            save_path = std::filesystem::path(argv[++index]);
        } else if (argument == "--character") {
            options.enable_character = true;
        } else if (argument == "--follow-character") {
            options.follow_character = true;
        } else if (argument == "--camera-limits" && index + 4 < argc) {
            float x{}, y{}, width{}, height{};
            if (!parse_float(argv[++index], x) || !parse_float(argv[++index], y) ||
                !parse_float(argv[++index], width) || !parse_float(argv[++index], height) ||
                width < 0.0F || height < 0.0F) {
                std::cerr << "error: --camera-limits expects finite x y width height\n";
                return 2;
            }
            options.camera_limits = fabric::core::Rect{{x, y}, {width, height}};
        } else if (argument == "--bind" && index + 3 < argc) {
            const std::string action(argv[++index]);
            const auto device = parse_input_device(argv[++index]);
            int code{};
            if (!fabric::core::ResourceId::is_valid(action) || !device ||
                !parse_non_negative_int(argv[++index], code)) {
                std::cerr << "error: --bind expects a valid action, keyboard/gamepad and code\n";
                return 2;
            }
            auto definition = std::find_if(configured_actions.begin(), configured_actions.end(),
                [&](const auto& candidate) { return candidate.id == action; });
            if (definition == configured_actions.end()) {
                configured_actions.push_back({action, {}});
                definition = std::prev(configured_actions.end());
            }
            const fabric::runtime::InputBinding binding{*device, code};
            if (std::find(definition->bindings.begin(), definition->bindings.end(), binding) !=
                definition->bindings.end()) {
                std::cerr << "error: duplicate --bind entry\n";
                return 2;
            }
            definition->bindings.push_back(binding);
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

    if (!configured_actions.empty()) options.input_actions = std::move(configured_actions);

    if (save_slot && save_path) {
        std::cerr << "error: --save-slot and --save-path are mutually exclusive\n";
        return 2;
    }

    fabric::runtime::ProgressStore progress_store;
    std::optional<fabric::project::ProgressSave> progress;
    if (save_slot || save_path) {
        if (options.package_root || options.project_root.empty()) {
            std::cerr << "error: progress resume requires --project\n";
            return 2;
        }
        const bool configured = save_slot
            ? progress_store.configure_user_path("VertexLoom", "VertexLoom", *save_slot)
            : progress_store.configure_path(*save_path);
        if (!configured) {
            for (const auto& error : progress_store.errors()) std::cerr << "error: " << error << '\n';
            return 1;
        }
        if (std::filesystem::exists(progress_store.path())) {
            fabric::project::ProgressSave existing;
            if (!progress_store.load(existing)) {
                for (const auto& error : progress_store.errors()) std::cerr << "error: " << error << '\n';
                return 1;
            }
            progress = std::move(existing);
            scene_id = progress->scene.id;
        } else {
            if (!scene_id) {
                std::cerr << "error: an absent progress save requires --scene\n";
                return 2;
            }
            progress = fabric::project::ProgressSave{
                .schema_version = fabric::project::current_progress_save_schema_version,
                .build = "vertex-loom-runtime",
                .scene = {*scene_id, "scene"}};
        }
        options.progress_properties = progress->properties;
    }

    fabric::runtime::SceneRuntimeSession scene_session;
    if (scene_id && !scene_session.load(options.project_root, *scene_id)) {
        for (const auto& error : scene_session.errors())
            std::cerr << "error: " << error << '\n';
        return 1;
    }

    std::unique_ptr<fabric::runtime::PreviewRuntime> runtime;
    bool scene_changed = false;
    do {
        scene_changed = false;
        if (scene_id) options.map_id = {};
        options.scene_id = scene_id
            ? std::optional<fabric::core::ResourceId>{scene_session.scene()->document.id}
            : std::nullopt;
        options.gameplay_event_handler = scene_id
            ? fabric::runtime::GameplayEventHandler([&](const auto& event) {
                if (event.kind != fabric::runtime::GameplayEventKind::entered) return true;
                if (!scene_session.transition_for_event(event.id)) return true;
                scene_changed = true;
                return false;
            })
            : fabric::runtime::GameplayEventHandler{};
        runtime = std::make_unique<fabric::runtime::PreviewRuntime>();
        if (!runtime->load(options) || !runtime->run()) {
            for (const auto& error : runtime->errors())
                std::cerr << "error: " << error << '\n';
            return 1;
        }
    } while (scene_id && scene_changed);
    if (progress) {
        progress->build = "vertex-loom-runtime";
        progress->scene = {scene_session.scene()->document.id, "scene"};
        progress->properties = runtime->progress_properties();
        if (!progress_store.save(*progress)) {
            for (const auto& error : progress_store.errors()) std::cerr << "error: " << error << '\n';
            return 1;
        }
    }
    if (options.mode != fabric::runtime::RuntimeMode::interactive) {
        std::cout << "frames=" << runtime->stats().frames
                  << " physics_steps=" << runtime->stats().physics_steps
                  << " visible=" << runtime->stats().visible_instances
                  << " culling_candidates=" << runtime->stats().culling_candidates
                  << " culled_packets=" << runtime->stats().culled_packets
                  << " direct_render_frames=" << runtime->stats().direct_render_frames
                  << " draw_calls=" << runtime->stats().draw_calls
                  << " triangles=" << runtime->stats().triangles
                  << " elapsed_ms=" << runtime->stats().elapsed_ms
                  << " p95_frame_ms=" << runtime->stats().p95_frame_ms
                  << " replay_events=" << runtime->stats().replay_events
                  << " replay_checkpoints=" << runtime->stats().replay_checkpoints
                  << " gameplay_events=" << runtime->stats().gameplay_events
                  << " character_x=" << runtime->stats().character_x
                  << " character_y=" << runtime->stats().character_y << '\n';
    }
    return 0;
}
