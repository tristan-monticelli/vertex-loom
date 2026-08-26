#include "fabric/project/behavior_graph.hpp"
#include "fabric/project/entity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {
using namespace fabric::project;

ProjectManifest manifest() {
    ProjectManifest value;
    value.id = {.value = "test-project"};
    value.name = "Test";
    return value;
}

BehaviorNodeDefinition source(std::string type, std::string semantic) {
    return {.id = "source", .type = std::move(type),
            .ports = {{.id = "out", .direction = BehaviorPortDirection::output,
                       .type = BehaviorValueType::signal}},
            .properties = {{.id = "semantic_id", .value = std::move(semantic)}}};
}

BehaviorNodeDefinition action(std::string id = "action") {
    return {.id = std::move(id), .type = "move",
            .ports = {{.id = "in", .direction = BehaviorPortDirection::input,
                       .type = BehaviorValueType::signal},
                      {.id = "out", .direction = BehaviorPortDirection::output,
                       .type = BehaviorValueType::signal}},
            .properties = {{.id = "vector", .value = fabric::core::Vec2{2.0F, 0.0F}}}};
}

BehaviorGraph graph(std::string source_type = "action_source") {
    BehaviorGraph value;
    value.document.id = {.value = "shared-behavior"};
    value.document.name = "Shared behavior";
    value.parameters.push_back({.id = "speed", .type = BehaviorValueType::scalar,
                                .default_value = 2.0F});
    value.nodes = {source(std::move(source_type), "advance"), action()};
    value.connections.push_back({.id = "source-to-action", .from_node = "source",
                                 .from_port = "out", .to_node = "action",
                                 .to_port = "in"});
    return value;
}
}

TEST_CASE("BehaviorGraph v1 round-trips all source roles without role-specific schema") {
    for (const auto* source_type : {"action_source", "ai_source", "event_source",
                                    "trigger_source", "timer_source", "property_source"}) {
        const auto original = graph(source_type);
        const auto parsed = parse_behavior_graph(manifest(), serialize_behavior_graph(original));
        REQUIRE(parsed.ok());
        CHECK(*parsed.asset == original);
    }
}

TEST_CASE("BehaviorGraph rejects missing ports, incompatible types and cycles") {
    auto missing = graph();
    missing.connections.front().to_port = "absent";
    CHECK_FALSE(validate_behavior_graph(manifest(), missing).ok());

    auto incompatible = graph();
    incompatible.nodes.back().ports.front().type = BehaviorValueType::boolean;
    CHECK_FALSE(validate_behavior_graph(manifest(), incompatible).ok());

    auto cycle = graph();
    cycle.nodes.front().ports.push_back({.id = "in", .direction = BehaviorPortDirection::input,
                                        .type = BehaviorValueType::signal});
    cycle.connections.push_back({.id = "back", .from_node = "action",
                                 .from_port = "out", .to_node = "source",
                                 .to_port = "in"});
    const auto report = validate_behavior_graph(manifest(), cycle);
    CHECK_FALSE(report.ok());
    CHECK(std::ranges::any_of(report.errors, [](const auto& error) {
        return error.code == ErrorCode::resource_cycle;
    }));
}

TEST_CASE("Entity schema 3 attaches a behavior and schema 2 migrates") {
    EntityDefinition entity;
    entity.document.id = {.value = "actor"};
    entity.document.name = "Actor";
    entity.behavior = ResourceReference{{.value = "shared-behavior"}, "behavior"};
    const auto parsed = parse_entity(manifest(), serialize_entity(entity));
    REQUIRE(parsed.ok());
    REQUIRE(parsed.entity->behavior);
    CHECK(parsed.entity->behavior->id.value == "shared-behavior");

    const auto legacy = R"({"schemaVersion":2,"type":"entity","id":"legacy","name":"Legacy","nodes":[],"constraints":[],"deformationMesh":null,"xpbd":null,"ikChains":[],"animationStateMachine":null})";
    const auto migrated = parse_entity(manifest(), legacy);
    REQUIRE(migrated.ok());
    CHECK(migrated.entity->document.schema_version == current_entity_schema_version);
    CHECK_FALSE(migrated.entity->behavior);
}
