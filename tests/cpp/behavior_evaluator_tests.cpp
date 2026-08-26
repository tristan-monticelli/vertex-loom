#include "fabric/runtime/behavior_evaluator.hpp"
#include "fabric/runtime/replay_player.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
using namespace fabric;

project::BehaviorNodeDefinition source(std::string id, std::string type) {
    return {.id = std::move(id), .type = std::move(type),
            .ports = {{.id = "out", .direction = project::BehaviorPortDirection::output,
                       .type = project::BehaviorValueType::signal}},
            .properties = {{.id = "semantic_id", .value = std::string("advance")}}};
}

project::BehaviorGraph shared_graph() {
    project::BehaviorGraph graph;
    graph.document.id = {.value = "shared-controller"};
    graph.document.name = "Shared controller";
    graph.nodes = {source("player", "action_source"), source("monster", "ai_source"),
                   source("script", "event_source"),
                   {.id = "move", .type = "move",
                    .ports = {{.id = "in", .direction = project::BehaviorPortDirection::input,
                               .type = project::BehaviorValueType::signal},
                              {.id = "out", .direction = project::BehaviorPortDirection::output,
                               .type = project::BehaviorValueType::signal}},
                    .properties = {{.id = "vector", .value = core::Vec2{3.0F, 0.0F}}}}};
    for (const auto* id : {"player", "monster", "script"})
        graph.connections.push_back({.id = std::string(id) + "-move", .from_node = id,
                                     .from_port = "out", .to_node = "move", .to_port = "in"});
    return graph;
}
}

TEST_CASE("one BehaviorGraph produces the same action for player AI and map event") {
    const auto graph = shared_graph();
    REQUIRE(project::validate_behavior_graph({}, graph).ok());
    runtime::BehaviorEvaluator evaluator(graph);
    for (const auto source : {runtime::BehaviorSignalSource::action,
                              runtime::BehaviorSignalSource::ai_decision,
                              runtime::BehaviorSignalSource::map_event}) {
        const auto actions = evaluator.evaluate({source, "advance", {}}, 1.0F / 60.0F);
        REQUIRE(actions.size() == 1);
        CHECK(actions.front().kind == runtime::BehaviorActionKind::move);
        CHECK(actions.front().node_id == "move");
    }
}

TEST_CASE("Behavior evaluation stays deterministic after save and reload") {
    project::ProjectManifest manifest;
    manifest.id = {.value = "test-project"};
    manifest.name = "Test";
    const auto graph = shared_graph();
    const auto parsed = project::parse_behavior_graph(manifest,
        project::serialize_behavior_graph(graph));
    REQUIRE(parsed.ok());
    runtime::BehaviorEvaluator first(graph);
    runtime::BehaviorEvaluator second(*parsed.asset);
    const runtime::BehaviorSignal signal{runtime::BehaviorSignalSource::action,
                                         "advance", {}};
    CHECK(first.evaluate(signal, 1.0F / 60.0F) ==
          second.evaluate(signal, 1.0F / 60.0F));
    CHECK(first.trace() == second.trace());
}

TEST_CASE("Behavior evaluation stays deterministic through a reloaded replay") {
    project::ProjectManifest manifest;
    manifest.id = {.value = "test-project"};
    manifest.name = "Test";
    const auto parsed_graph = project::parse_behavior_graph(
        manifest, project::serialize_behavior_graph(shared_graph()));
    REQUIRE(parsed_graph.ok());
    project::ReplayDocument replay;
    replay.document.id = {.value = "behavior-replay"};
    replay.document.name = "Behavior Replay";
    replay.build = "test";
    replay.inputs = {{0, "advance", true, false},
                     {2, "advance", false, true},
                     {4, "advance", true, false}};
    const auto parsed_replay = project::parse_replay(
        manifest, project::serialize_replay(replay));
    REQUIRE(parsed_replay.ok());

    runtime::BehaviorEvaluator first(shared_graph());
    runtime::BehaviorEvaluator second(*parsed_graph.asset);
    runtime::ReplayPlayer first_player(replay);
    runtime::ReplayPlayer second_player(*parsed_replay.asset);
    runtime::InputActionMap first_input;
    runtime::InputActionMap second_input;
    REQUIRE(first_input.define_action("advance"));
    REQUIRE(second_input.define_action("advance"));
    std::vector<runtime::BehaviorAction> first_actions;
    std::vector<runtime::BehaviorAction> second_actions;
    for (std::uint64_t frame = 0; frame < 6; ++frame) {
        REQUIRE(first_player.advance(frame, first_input));
        REQUIRE(second_player.advance(frame, second_input));
        if (first_input.pressed("advance")) {
            auto actions = first.evaluate(
                {runtime::BehaviorSignalSource::action, "advance", {}},
                1.0F / 60.0F);
            first_actions.insert(first_actions.end(), actions.begin(), actions.end());
        }
        if (second_input.pressed("advance")) {
            auto actions = second.evaluate(
                {runtime::BehaviorSignalSource::action, "advance", {}},
                1.0F / 60.0F);
            second_actions.insert(second_actions.end(), actions.begin(), actions.end());
        }
    }
    CHECK(first_actions == second_actions);
    CHECK(first.trace() == second.trace());
}
