#pragma once

#include "fabric/project/animation.hpp"

#include <vector>

namespace fabric::project {

enum class AnimationConstraintKind { copy_transform, limits, look_at };

struct AnimationConstraint {
    std::string id;
    AnimationConstraintKind kind{AnimationConstraintKind::copy_transform};
    std::string target_node;
    std::string source_node;
    int order{};
    bool constrain_position{true};
    bool constrain_rotation{true};
    bool constrain_scale{true};
};

[[nodiscard]] ValidationReport validate_animation_constraints(
    const std::vector<AnimationConstraint>&);
[[nodiscard]] std::vector<const AnimationConstraint*> order_animation_constraints(
    const std::vector<AnimationConstraint>&);

} // namespace fabric::project
