#include "fabric/project/animation_constraints.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("animation constraints are resolved in explicit order") {
    using namespace fabric::project;
    const std::vector<AnimationConstraint> constraints = {
        {"look", AnimationConstraintKind::look_at, "arm", "target", 20},
        {"copy", AnimationConstraintKind::copy_transform, "hand", "arm", 10},
    };
    REQUIRE(validate_animation_constraints(constraints).ok());
    const auto ordered = order_animation_constraints(constraints);
    REQUIRE(ordered.size() == 2);
    REQUIRE(ordered[0]->id == "copy");
    REQUIRE(ordered[1]->id == "look");
}

TEST_CASE("animation constraints reject dependency cycles") {
    using namespace fabric::project;
    const std::vector<AnimationConstraint> constraints = {
        {"a", AnimationConstraintKind::copy_transform, "a", "b", 1},
        {"b", AnimationConstraintKind::copy_transform, "b", "a", 2},
    };
    REQUIRE_FALSE(validate_animation_constraints(constraints).ok());
    REQUIRE(order_animation_constraints(constraints).empty());
}
