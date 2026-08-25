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
    float rotation_degrees{};
    friend bool operator==(const MechanicBodyState&,
                           const MechanicBodyState&) = default;
};

enum class MechanicSignalKind { sensor, event };

struct MechanicSignalState {
    std::string node_id;
    MechanicSignalKind kind{MechanicSignalKind::sensor};
    core::ResourceId event_id;
    bool active{};
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

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] std::size_t step_count() const noexcept;
    [[nodiscard]] const std::vector<MechanicBodyState>& body_states() const noexcept;
    [[nodiscard]] const std::vector<MechanicSignalState>& signal_states() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fabric::physics
