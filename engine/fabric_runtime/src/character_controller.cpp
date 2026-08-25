#include "fabric/runtime/character_controller.hpp"

namespace fabric::runtime {

bool CharacterController::create(physics::PhysicsWorld& world, const core::Vec2 position,
                                  const CharacterControllerConfig config) {
    world_ = nullptr;
    config_ = config;
    state_ = LocomotionState::grounded;
    if (!world.create_character(position)) return false;
    world_ = &world;
    return true;
}

void CharacterController::update(const InputActionMap& input, const float) noexcept {
    if (!valid()) return;
    const auto current = world_->character_position();
    const auto left = input.held("move_left");
    const auto right = input.held("move_right");
    const auto horizontal = (right ? 1.0F : 0.0F) - (left ? 1.0F : 0.0F);
    const auto was_airborne = state_ == LocomotionState::airborne;
    auto velocity = core::Vec2{horizontal * config_.horizontal_speed,
                               was_airborne ? world_->character_velocity().y : 0.0F};
    if (!was_airborne && input.pressed("jump")) {
        velocity.y = config_.jump_speed;
        state_ = LocomotionState::airborne;
    }
    world_->set_character_velocity(velocity);
    if (was_airborne && current.y <= config_.ground_height)
        state_ = LocomotionState::grounded;
}

core::Vec2 CharacterController::position() const noexcept {
    return valid() ? world_->character_position() : core::Vec2{};
}

} // namespace fabric::runtime
