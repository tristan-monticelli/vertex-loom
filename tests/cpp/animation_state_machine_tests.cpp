#include "fabric/project/animation_state_machine.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::AnimationStateMachine machine() {
    using namespace fabric::project;
    return {
        .initial_state = "idle",
        .states = {{"idle", {{.value = "idle-clip"}, "animation"}},
                   {"run", {{.value = "run-clip"}, "animation"}}},
        .transitions = {{"start", "idle", "run",
                         {{"speed", AnimationConditionOperator::greater, 0.1F}},
                         std::nullopt, 1},
                        {"stop", "run", "idle",
                         {{"grounded", AnimationConditionOperator::equal, true}},
                         0.5F, 2}},
    };
}

TEST_CASE("animation state machine selects the highest priority matching transition") {
    const auto graph = machine();
    REQUIRE(fabric::project::validate_animation_state_machine(graph).ok());
    const std::vector<fabric::project::AnimationParameter> parameters = {
        {"speed", 1.0F}, {"grounded", true}};
    const auto* transition = fabric::project::select_animation_transition(
        graph, "idle", parameters, 0.0F);
    REQUIRE(transition != nullptr);
    REQUIRE(transition->id == "start");
    REQUIRE(fabric::project::select_animation_transition(
                graph, "run", parameters, 0.25F) == nullptr);
    transition = fabric::project::select_animation_transition(
        graph, "run", parameters, 0.5F);
    REQUIRE(transition != nullptr);
    REQUIRE(transition->id == "stop");
}

TEST_CASE("animation state machine rejects invalid graph references") {
    auto graph = machine();
    graph.initial_state = "missing";
    graph.transitions.front().to_state = "missing";
    graph.states.front().clip.expected_type = "texture";
    REQUIRE_FALSE(fabric::project::validate_animation_state_machine(graph).ok());
}

} // namespace
