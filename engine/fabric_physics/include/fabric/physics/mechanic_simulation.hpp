#pragma once

#include "fabric/physics/mechanic_plan.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fabric::physics {

struct MechanicBodyState {
    std::string node_id;
    core::Vec2 position;
    core::Vec2 size;
    float rotation_degrees{};
    friend bool operator==(const MechanicBodyState&,
                           const MechanicBodyState&) = default;
};

struct MechanicSensorState {
    std::string node_id;
    core::Vec2 position;
    core::Vec2 size;
    float rotation_degrees{};
    bool active{};
};

enum class MechanicSignalKind { sensor, event };

struct MechanicSignalState {
    std::string node_id;
    MechanicSignalKind kind{MechanicSignalKind::sensor};
    core::ResourceId event_id;
    bool active{};
    bool manually_active{};
    std::size_t physical_overlap_count{};
};

struct MechanicActivationState {
    std::string node_id;
    std::optional<std::string> source_node_id;
    bool active{};
};

enum class MechanicActivationTransition { begin, end };

struct MechanicDebugEvent {
    std::size_t step{};
    std::string node_id;
    std::optional<std::string> source_node_id;
    MechanicActivationTransition transition{MechanicActivationTransition::begin};
};

struct MechanicPreviewCharacterConfig {
    core::Vec2 position{0.0F, 1.0F};
    core::Vec2 size{0.75F, 1.5F};
    float density{1.0F};
    float friction{0.9F};
};

struct MechanicPreviewCharacterState {
    core::Vec2 position;
    core::Vec2 velocity;
    core::Vec2 size;
    float rotation_degrees{};
};

class MechanicSimulation {
public:
    MechanicSimulation();
    ~MechanicSimulation();
    MechanicSimulation(const MechanicSimulation&) = delete;
    MechanicSimulation& operator=(const MechanicSimulation&) = delete;
    MechanicSimulation(MechanicSimulation&&) noexcept;
    MechanicSimulation& operator=(MechanicSimulation&&) noexcept;

    [[nodiscard]] bool load(MechanicPlan plan);
    [[nodiscard]] bool reset();
    [[nodiscard]] bool update(float frame_seconds);
    [[nodiscard]] bool step_once();
    void play() noexcept;
    void pause() noexcept;
    [[nodiscard]] bool set_sensor_active(const core::ResourceId&, bool) noexcept;
    [[nodiscard]] bool set_event_active(const core::ResourceId&, bool) noexcept;
    [[nodiscard]] bool place_preview_character(MechanicPreviewCharacterConfig);
    [[nodiscard]] bool set_preview_character_velocity(core::Vec2) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] std::size_t step_count() const noexcept;
    [[nodiscard]] const std::vector<MechanicBodyState>& body_states() const noexcept;
    [[nodiscard]] const std::vector<MechanicSensorState>& sensor_states() const noexcept;
    [[nodiscard]] const std::vector<MechanicSignalState>& signal_states() const noexcept;
    [[nodiscard]] const std::vector<MechanicActivationState>& activation_states() const noexcept;
    [[nodiscard]] const std::vector<MechanicDebugEvent>& debug_events() const noexcept;
    [[nodiscard]] std::optional<MechanicPreviewCharacterState>
        preview_character_state() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fabric::physics
