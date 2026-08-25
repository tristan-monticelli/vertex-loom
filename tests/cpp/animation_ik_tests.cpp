#include "fabric/project/animation_ik.hpp"

#include <cmath>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FABRIK reaches a reachable target deterministically") {
    using namespace fabric;
    const project::FabrikRequest request{
        .joints = {{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F}},
        .target = {1.0F, 1.0F},
        .max_iterations = 32,
        .tolerance = 1.0e-4F,
    };
    const auto first = project::solve_fabrik(request);
    const auto second = project::solve_fabrik(request);
    REQUIRE(first.ok());
    REQUIRE(first.converged);
    REQUIRE(first.joints == second.joints);
    REQUIRE(std::hypot(first.joints.back().x - 1.0F,
                       first.joints.back().y - 1.0F) <= request.tolerance);
    REQUIRE(first.joints.front() == request.joints.front());
}

TEST_CASE("FABRIK handles unreachable targets by stretching") {
    using namespace fabric;
    const auto result = project::solve_fabrik({
        .joints = {{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F}},
        .target = {5.0F, 0.0F},
    });
    REQUIRE(result.ok());
    REQUIRE(result.converged);
    REQUIRE(result.joints.back() == core::Vec2{2.0F, 0.0F});
}

TEST_CASE("FABRIK rejects degenerate chains") {
    const auto result = fabric::project::solve_fabrik({
        .joints = {{0.0F, 0.0F}, {0.0F, 0.0F}},
        .target = {1.0F, 0.0F},
    });
    REQUIRE_FALSE(result.ok());
}
