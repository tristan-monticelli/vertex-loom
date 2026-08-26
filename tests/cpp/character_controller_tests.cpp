#include "fabric/runtime/character_controller.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("character controller maps actions to a deterministic mover") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create(0.0F, 0.0F));
    fabric::runtime::CharacterController controller;
    REQUIRE(controller.create(world, {0.0F, 0.0F}, {.horizontal_speed = 4.0F}));
    controller.update({.horizontal = 1.0F}, 1.0F / 60.0F);
    REQUIRE(world.step(1.0F / 60.0F));
    CHECK(controller.position().x > 0.0F);
    controller.update({.jump_pressed = true}, 1.0F / 60.0F);
    CHECK(controller.state() == fabric::runtime::LocomotionState::airborne);
}
