#include "fabric/project/animation_state_machine.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace fabric::project {
namespace {

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

const AnimationParameter* find_parameter(
    const std::vector<AnimationParameter>& parameters, std::string_view id) {
    for (const auto& parameter : parameters)
        if (parameter.id == id)
            return &parameter;
    return nullptr;
}

bool condition_matches(const AnimationCondition& condition,
                       const AnimationParameter& parameter) {
    if (condition.value.index() != parameter.value.index())
        return false;
    if (const auto* expected = std::get_if<bool>(&condition.value)) {
        const bool actual = std::get<bool>(parameter.value);
        switch (condition.operation) {
        case AnimationConditionOperator::equal: return actual == *expected;
        case AnimationConditionOperator::not_equal: return actual != *expected;
        default: return false;
        }
    }
    const float expected = std::get<float>(condition.value);
    const float actual = std::get<float>(parameter.value);
    switch (condition.operation) {
    case AnimationConditionOperator::equal: return actual == expected;
    case AnimationConditionOperator::not_equal: return actual != expected;
    case AnimationConditionOperator::less: return actual < expected;
    case AnimationConditionOperator::less_equal: return actual <= expected;
    case AnimationConditionOperator::greater: return actual > expected;
    case AnimationConditionOperator::greater_equal: return actual >= expected;
    }
    return false;
}

} // namespace

ValidationReport validate_animation_state_machine(const AnimationStateMachine& machine) {
    ValidationReport report;
    std::set<std::string> states;
    for (const auto& state : machine.states) {
        if (!core::ResourceId::is_valid(state.id) || !states.insert(state.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "states.id",
                  "state ids must be valid and unique");
        if (!core::ResourceId::is_valid(state.clip.id.value) ||
            state.clip.expected_type != "animation")
            error(report.errors, ErrorCode::resource_type_mismatch, "states.clip",
                  "state clips must be valid animation references");
    }
    if (machine.states.empty() || states.find(machine.initial_state) == states.end())
        error(report.errors, ErrorCode::missing_resource, "initialState",
              "initial state must reference a declared state");

    std::set<std::string> transitions;
    for (const auto& transition : machine.transitions) {
        if (!core::ResourceId::is_valid(transition.id) ||
            !transitions.insert(transition.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "transitions.id",
                  "transition ids must be valid and unique");
        if (states.find(transition.from_state) == states.end() ||
            states.find(transition.to_state) == states.end())
            error(report.errors, ErrorCode::missing_resource, "transitions.state",
                  "transition endpoints must reference declared states");
        if (transition.exit_time &&
            (!std::isfinite(*transition.exit_time) || *transition.exit_time < 0.0F ||
             *transition.exit_time > 1.0F))
            error(report.errors, ErrorCode::invalid_asset, "transitions.exitTime",
                  "exit time must be within normalized clip time");
        for (const auto& condition : transition.conditions)
            if (condition.parameter_id.empty())
                error(report.errors, ErrorCode::invalid_asset, "conditions.parameter",
                      "condition parameter id must not be empty");
    }
    return report;
}

const AnimationState* find_animation_state(const AnimationStateMachine& machine,
                                            std::string_view state_id) noexcept {
    for (const auto& state : machine.states)
        if (state.id == state_id)
            return &state;
    return nullptr;
}

const AnimationTransition* select_animation_transition(
    const AnimationStateMachine& machine, std::string_view current_state,
    const std::vector<AnimationParameter>& parameters, float normalized_time) noexcept {
    const AnimationTransition* selected = nullptr;
    for (const auto& transition : machine.transitions) {
        if (transition.from_state != current_state ||
            (transition.exit_time && normalized_time < *transition.exit_time))
            continue;
        bool matches = true;
        for (const auto& condition : transition.conditions) {
            const auto* parameter = find_parameter(parameters, condition.parameter_id);
            if (!parameter || !condition_matches(condition, *parameter)) {
                matches = false;
                break;
            }
        }
        if (!matches || (selected && transition.priority <= selected->priority))
            continue;
        selected = &transition;
    }
    return selected;
}

} // namespace fabric::project
