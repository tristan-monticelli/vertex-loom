#include "fabric/project/xpbd.hpp"

#include <cmath>

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

TEST_CASE("XPBD preserves triangle area and resolves unilateral collision") {
    using namespace fabric::project;
    XpbdSystem system{
        .particles = {{{0.0F, 0.0F}, 0.0F},
                      {{1.0F, 0.0F}, 0.0F},
                      {{0.0F, 2.0F}, 1.0F},
                      {{0.0F, -1.0F}, 1.0F}},
        .area_constraints = {{0, 1, 2, 0.5F, 0.0F, 0.0F}},
        .collision_constraints = {{3, {0.0F, 1.0F}, 0.0F, 0.0F, 0.0F}},
    };
    REQUIRE(solve_xpbd_substep(system, 1.0F / 60.0F, 24).ok());
    REQUIRE(system.particles[2].position.y == 1.0F);
    REQUIRE(system.particles[3].position.y == 0.0F);
    REQUIRE(system.collision_constraints.front().lambda > 0.0F);
}

TEST_CASE("XPBD bending constraint keeps chain endpoint distance") {
    using namespace fabric::project;
    XpbdSystem system{
        .particles = {{{0.0F, 0.0F}, 0.0F},
                      {{1.0F, 0.0F}, 1.0F},
                      {{1.0F, 1.0F}, 1.0F}},
        .bending_constraints = {{0, 1, 2, 2.0F, 0.0F, 0.0F}},
    };
    REQUIRE(solve_xpbd_substep(system, 1.0F / 60.0F, 24).ok());
    const auto& endpoint = system.particles[2].position;
    REQUIRE(std::abs(std::sqrt(endpoint.x * endpoint.x + endpoint.y * endpoint.y) - 2.0F) <= 1.0e-3F);
}
