#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/physics/physics_world.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/replay.hpp"
#include "fabric/runtime/character_controller.hpp"
#include "fabric/runtime/input.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabric::runtime {

enum class RuntimeMode { interactive, smoke_test, benchmark };

struct PreviewRuntimeOptions {
    std::filesystem::path project_root;
    core::ResourceId map_id;
    std::optional<core::ResourceId> replay_id;
    bool enable_character{};
    RuntimeMode mode{RuntimeMode::interactive};
    std::int32_t width{1440};
    std::int32_t height{900};
    std::size_t frame_limit{};
};

struct PreviewRuntimeStats {
    std::size_t frames{};
    std::size_t physics_steps{};
    std::size_t visible_instances{};
    std::size_t draw_calls{};
    std::size_t triangles{};
    double elapsed_ms{};
    double p95_frame_ms{};
    std::size_t replay_events{};
    std::size_t replay_checkpoints{};
    float character_x{};
    float character_y{};
};

class PreviewRuntime {
public:
    PreviewRuntime();
    ~PreviewRuntime();
    PreviewRuntime(const PreviewRuntime&) = delete;
    PreviewRuntime& operator=(const PreviewRuntime&) = delete;

    // Performs every project and map validation before any SDL window exists.
    [[nodiscard]] bool load(const PreviewRuntimeOptions& options);
    [[nodiscard]] bool run();

    [[nodiscard]] bool loaded() const noexcept { return map_.has_value(); }
    [[nodiscard]] const std::optional<project::MapDocument>& map() const noexcept {
        return map_;
    }
    [[nodiscard]] const std::optional<project::ReplayDocument>& replay() const noexcept {
        return replay_;
    }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] const PreviewRuntimeStats& stats() const noexcept { return stats_; }

private:
    struct Impl;

    PreviewRuntimeOptions options_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    std::optional<project::ReplayDocument> replay_;
    std::unique_ptr<class ReplayPlayer> replay_player_;
    InputActionMap input_;
    std::unique_ptr<CharacterController> character_;
    physics::PhysicsWorld physics_;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> errors_;
    PreviewRuntimeStats stats_;
};

} // namespace fabric::runtime
