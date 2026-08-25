#pragma once

#include "fabric/project/animation.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fabric::project {

using AnimationParameterValue = std::variant<float, bool>;

struct AnimationParameter {
    std::string id;
    AnimationParameterValue value{false};
    friend bool operator==(const AnimationParameter&, const AnimationParameter&) = default;
};

enum class AnimationConditionOperator {
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
};

struct AnimationCondition {
    std::string parameter_id;
    AnimationConditionOperator operation{AnimationConditionOperator::equal};
    AnimationParameterValue value{false};
    friend bool operator==(const AnimationCondition&, const AnimationCondition&) = default;
};

struct AnimationState {
    std::string id;
    ResourceReference clip;
    friend bool operator==(const AnimationState&, const AnimationState&) = default;
};

struct AnimationTransition {
    std::string id;
    std::string from_state;
    std::string to_state;
    std::vector<AnimationCondition> conditions;
    std::optional<float> exit_time;
    int priority{};
    friend bool operator==(const AnimationTransition&, const AnimationTransition&) = default;
};

struct AnimationStateMachine {
    std::string initial_state;
    std::vector<AnimationState> states;
    std::vector<AnimationTransition> transitions;
    friend bool operator==(const AnimationStateMachine&, const AnimationStateMachine&) = default;
};

[[nodiscard]] ValidationReport validate_animation_state_machine(
    const AnimationStateMachine&);
[[nodiscard]] const AnimationState* find_animation_state(
    const AnimationStateMachine&, std::string_view state_id) noexcept;
[[nodiscard]] const AnimationTransition* select_animation_transition(
    const AnimationStateMachine&, std::string_view current_state,
    const std::vector<AnimationParameter>& parameters,
    float normalized_time) noexcept;

} // namespace fabric::project
