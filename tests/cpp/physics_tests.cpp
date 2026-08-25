#include "fabric/physics/physics_world.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Box2D physics world owns a fixed-step world") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create());
    REQUIRE(world.valid());
    REQUIRE(world.step(1.0F / 60.0F, 4));
    world.destroy();
    REQUIRE_FALSE(world.valid());
    REQUIRE_FALSE(world.step(1.0F / 60.0F));
}

TEST_CASE("Box2D physics world rejects invalid steps") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create());
    REQUIRE_FALSE(world.step(0.0F));
    REQUIRE_FALSE(world.step(1.0F / 60.0F, 0));
}
