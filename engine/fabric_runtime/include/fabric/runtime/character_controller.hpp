#pragma once

#include "fabric/physics/physics_world.hpp"

namespace fabric::runtime {

enum class LocomotionState { grounded, airborne };

struct CharacterControllerConfig {
    float horizontal_speed{5.0F};
    float jump_speed{7.0F};
    float ground_height{0.0F};
};

struct CharacterControlFrame {
    float horizontal{};
    bool jump_pressed{};
};

class CharacterController {
public:
    [[nodiscard]] bool create(physics::PhysicsWorld& world, core::Vec2 position,
                               CharacterControllerConfig config = {});
    void update(CharacterControlFrame, float time_step) noexcept;

    [[nodiscard]] bool valid() const noexcept { return world_ != nullptr && world_->character_valid(); }
    [[nodiscard]] core::Vec2 position() const noexcept;
    [[nodiscard]] LocomotionState state() const noexcept { return state_; }

private:
    physics::PhysicsWorld* world_{};
    CharacterControllerConfig config_{};
    LocomotionState state_{LocomotionState::grounded};
};

} // namespace fabric::runtime
