#include "fabric/project/xpbd.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("XPBD solves distance and pin constraints with quantized state") {
    using namespace fabric::project;
    XpbdSystem system{
        .particles = {{{0.0F, 0.0F}, 0.0F}, {{2.0F, 0.0F}, 1.0F}},
        .distance_constraints = {{0, 1, 1.0F, 0.0F, 0.0F}},
        .pin_constraints = {{0, {0.0F, 0.0F}, 0.0F, {}}},
    };
    const auto result = solve_xpbd_substep(system, 1.0F / 60.0F, 16);
    REQUIRE(result.ok());
    REQUIRE(system.particles[0].position == fabric::core::Vec2{0.0F, 0.0F});
    REQUIRE(system.particles[1].position.x == 1.0F);
    REQUIRE(system.particles[1].position.y == 0.0F);
    REQUIRE(system.distance_constraints.front().lambda != 0.0F);
}

TEST_CASE("XPBD rejects invalid indices and time steps") {
    using namespace fabric::project;
    XpbdSystem system{
        .particles = {{{0.0F, 0.0F}, 1.0F}},
        .distance_constraints = {{0, 2, 1.0F, 0.0F, 0.0F}},
    };
    REQUIRE_FALSE(validate_xpbd_system(system, 0.0F, 4).ok());
    REQUIRE_FALSE(solve_xpbd_substep(system, 1.0F / 60.0F, 4).ok());
}
