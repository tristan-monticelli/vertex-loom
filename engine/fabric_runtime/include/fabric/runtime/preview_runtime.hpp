#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/physics/physics_world.hpp"
#include "fabric/physics/mechanic_simulation.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/map_package.hpp"
#include "fabric/project/progress_save.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/mesh_deformation.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/replay.hpp"
#include "fabric/project/scene.hpp"
#include "fabric/project/xpbd.hpp"
#include "fabric/render/vector_geometry.hpp"
#include "fabric/runtime/character_controller.hpp"
#include "fabric/runtime/audio_mixer.hpp"
#include "fabric/runtime/behavior_evaluator.hpp"
#include "fabric/runtime/input.hpp"
#include "fabric/runtime/trigger_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabric::runtime {

enum class RuntimeMode { interactive, smoke_test, benchmark };

using GameplayEventHandler = std::function<bool(const GameplayEvent&)>;

struct PreviewRuntimeOptions {
    std::filesystem::path project_root;
    std::optional<std::filesystem::path> package_root;
    std::optional<core::ResourceId> package_scene_id;
    core::ResourceId map_id;
    std::optional<core::ResourceId> scene_id;
    std::optional<core::ResourceId> replay_id;
    std::optional<core::ResourceId> input_id;
    GameplayEventHandler gameplay_event_handler;
    std::vector<InputActionDefinition> input_actions;
    bool enable_character{};
    std::optional<core::Vec2> character_spawn;
    bool follow_character{};
    std::optional<core::Rect> camera_limits;
    std::optional<std::filesystem::path> audio_wav;
    std::map<std::string, project::ProgressValue> progress_properties;
    RuntimeMode mode{RuntimeMode::interactive};
    std::int32_t width{1440};
    std::int32_t height{900};
    std::size_t frame_limit{};
};

struct PreviewRuntimeStats {
    std::size_t frames{};
    std::size_t physics_steps{};
    std::size_t mechanic_steps{};
    std::size_t xpbd_steps{};
    std::size_t deformation_instances{};
    std::size_t deformed_packets{};
    std::size_t vector_geometry_cache_entries{};
    std::size_t culling_candidates{};
    std::size_t culled_packets{};
    std::size_t direct_render_frames{};
    std::size_t visible_instances{};
    std::size_t draw_calls{};
    std::size_t triangles{};
    double elapsed_ms{};
    double p95_frame_ms{};
    std::size_t replay_events{};
    std::size_t replay_checkpoints{};
    std::size_t gameplay_events{};
    std::size_t animation_marker_events{};
    std::size_t behavior_actions{};
    float character_x{};
    float character_y{};
};

struct AnimationStateEvaluation {
    std::string state_id;
    core::ResourceId clip_id;
    float local_time{};
    friend bool operator==(const AnimationStateEvaluation&,
                           const AnimationStateEvaluation&) = default;
};

struct AnimationMarkerEvent {
    std::string instance_id;
    core::ResourceId clip_id;
    project::AnimationMarkerHit marker;
    friend bool operator==(const AnimationMarkerEvent&,
                           const AnimationMarkerEvent&) = default;
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
    [[nodiscard]] const std::optional<project::SceneDocument>& scene() const noexcept {
        return scene_;
    }
    [[nodiscard]] const std::optional<project::ReplayDocument>& replay() const noexcept {
        return replay_;
    }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] const PreviewRuntimeStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const std::map<std::string, project::ProgressValue>&
    progress_properties() const noexcept {
        return options_.progress_properties;
    }
    [[nodiscard]] const std::vector<GameplayEvent>& gameplay_events() const noexcept;
    [[nodiscard]] const std::vector<AnimationMarkerEvent>&
    animation_marker_events() const noexcept;
    [[nodiscard]] std::vector<std::string> packet_order() const;
    // Returns the packets submitted by the most recent frame. Empty before run().
    [[nodiscard]] const std::vector<render::VectorDrawPacket>&
    last_frame_packets() const noexcept;
    [[nodiscard]] std::size_t animation_count() const noexcept;
    [[nodiscard]] std::size_t mechanic_instance_count() const noexcept;
    [[nodiscard]] std::optional<std::vector<physics::MechanicBodyState>>
    mechanic_body_states(const std::string& instance_id) const;
    [[nodiscard]] std::optional<project::EvaluationResult> evaluate_animation(
        const core::ResourceId& animation_id, float time) const;
    [[nodiscard]] std::vector<project::AnimationMarkerHit> animation_markers(
        const core::ResourceId& animation_id, float from_time, float to_time) const;
    [[nodiscard]] std::optional<project::EvaluationResult> evaluate_instance_animation(
        const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<AnimationStateEvaluation>
    evaluate_instance_state(const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<project::MeshDeformationResult>
    evaluate_instance_deformation(const std::string& instance_id) const;
    [[nodiscard]] std::optional<project::MeshDeformationResult>
    evaluate_instance_deformation(const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<std::vector<project::EntityNode>>
    evaluate_instance_nodes(const std::string& instance_id, float time) const;
    [[nodiscard]] std::optional<project::XpbdSystem>
    instance_xpbd_state(const std::string& instance_id) const;
    [[nodiscard]] std::optional<std::vector<BehaviorAction>>
    evaluate_instance_behavior(const std::string& instance_id,
                               const BehaviorSignal& signal,
                               float fixed_step_seconds);
    [[nodiscard]] const std::vector<BehaviorAction>&
    behavior_actions() const noexcept;

private:
    struct Impl;

    PreviewRuntimeOptions options_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::SceneDocument> scene_;
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
