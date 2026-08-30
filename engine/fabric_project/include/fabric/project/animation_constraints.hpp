#pragma once

#include "fabric/project/animation.hpp"

#include <optional>
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
    std::optional<core::Vec2> min_position;
    std::optional<core::Vec2> max_position;
    std::optional<float> min_rotation_degrees;
    std::optional<float> max_rotation_degrees;
    std::optional<core::Vec2> min_scale;
    std::optional<core::Vec2> max_scale;
    friend bool operator==(const AnimationConstraint&, const AnimationConstraint&) = default;
};

[[nodiscard]] ValidationReport validate_animation_constraints(
    const std::vector<AnimationConstraint>&);
[[nodiscard]] std::vector<const AnimationConstraint*> order_animation_constraints(
    const std::vector<AnimationConstraint>&);

} // namespace fabric::project
