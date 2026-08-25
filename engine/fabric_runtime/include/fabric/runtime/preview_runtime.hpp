#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/physics/physics_world.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/mesh_deformation.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/replay.hpp"
#include "fabric/project/xpbd.hpp"
#include "fabric/runtime/character_controller.hpp"
#include "fabric/runtime/audio_mixer.hpp"
#include "fabric/runtime/input.hpp"
#include "fabric/runtime/trigger_runtime.hpp"

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
    std::optional<std::filesystem::path> audio_wav;
    RuntimeMode mode{RuntimeMode::interactive};
    std::int32_t width{1440};
    std::int32_t height{900};
    std::size_t frame_limit{};
};

struct PreviewRuntimeStats {
    std::size_t frames{};
    std::size_t physics_steps{};
    std::size_t xpbd_steps{};
    std::size_t deformation_instances{};
    std::size_t deformed_packets{};
    std::size_t vector_geometry_cache_entries{};
    std::size_t visible_instances{};
    std::size_t draw_calls{};
    std::size_t triangles{};
    double elapsed_ms{};
    double p95_frame_ms{};
    std::size_t replay_events{};
    std::size_t replay_checkpoints{};
    std::size_t gameplay_events{};
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
    [[nodiscard]] std::size_t animation_count() const noexcept;
    [[nodiscard]] std::optional<project::EvaluationResult> evaluate_animation(
        const core::ResourceId& animation_id, float time) const;
    [[nodiscard]] std::optional<project::EvaluationResult> evaluate_instance_animation(
        const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<project::MeshDeformationResult>
    evaluate_instance_deformation(const std::string& instance_id) const;
    [[nodiscard]] std::optional<project::MeshDeformationResult>
    evaluate_instance_deformation(const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<std::vector<project::EntityNode>>
    evaluate_instance_nodes(const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<project::XpbdSystem>
    instance_xpbd_state(const std::string& instance_id) const;

private:
    struct Impl;

    PreviewRuntimeOptions options_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    std::optional<project::ReplayDocument> replay_;
    std::unique_ptr<class ReplayPlayer> replay_player_;
    InputActionMap input_;
    std::unique_ptr<CharacterController> character_;
    std::unique_ptr<TriggerRuntime> triggers_;
    physics::PhysicsWorld physics_;
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> errors_;
    PreviewRuntimeStats stats_;
};

} // namespace fabric::runtime
