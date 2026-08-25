#include "fabric/physics/physics_world.hpp"
#include "fabric/project/map.hpp"

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

TEST_CASE("Box2D physics world loads map collision shapes and sensors") {
    fabric::project::MapDocument map;
    map.document.id = {.value = "physics-map"};
    map.document.name = "Physics Map";
    map.layers = {{"collision", "Collision", fabric::project::MapLayerKind::collision,
                   true, false, 0.0F}};
    map.collisions = {{fabric::project::CollisionShapeKind::circle, "collision", false,
                       {0.0F, 0.0F}, 1.0F, 0.0F, {}},
                      {fabric::project::CollisionShapeKind::polygon, "collision", true,
                       {}, 0.0F, 0.0F, {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}}};
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create());
    REQUIRE(world.load_map_collisions(map));
    REQUIRE(world.step(1.0F / 60.0F));
}

TEST_CASE("Box2D physics world exposes a dynamic character body") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create(0.0F, 0.0F));
    REQUIRE(world.create_character({0.0F, 0.0F}));
    REQUIRE(world.character_valid());
    world.set_character_velocity({3.0F, 0.0F});
    REQUIRE(world.step(1.0F / 60.0F));
    CHECK(world.character_position().x > 0.0F);
    CHECK(world.character_velocity().x > 0.0F);
}
