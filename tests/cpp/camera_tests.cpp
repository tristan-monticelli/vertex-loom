#include "fabric/runtime/camera2d.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("camera computes viewport bounds and zooms around the cursor") {
    fabric::runtime::Camera2D camera;
    camera.set_viewport(1000, 500);
    camera.set_position({10.0F, 20.0F});
    const auto before = camera.screen_to_world({750.0F, 125.0F});
    camera.zoom_at({750.0F, 125.0F}, 2.0F);
    camera.update(1.0F);
    CHECK(camera.zoom() == 2.0F);
    CHECK(camera.screen_to_world({750.0F, 125.0F}).x == before.x);
    CHECK(camera.screen_to_world({750.0F, 125.0F}).y == before.y);
    const auto bounds = camera.world_bounds();
    CHECK(bounds.size.x == 500.0F);
    CHECK(bounds.size.y == 250.0F);
}

TEST_CASE("camera pans with interpolated motion") {
    fabric::runtime::Camera2D camera;
    camera.set_viewport(100, 100);
    camera.pan({10.0F, -4.0F});
    camera.update(1.0F / 60.0F);
    CHECK(camera.position().x > 0.0F);
    CHECK(camera.position().x < 10.0F);
    CHECK(camera.position().y < 0.0F);
}
