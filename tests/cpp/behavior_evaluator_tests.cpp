#include "fabric/runtime/behavior_evaluator.hpp"

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
