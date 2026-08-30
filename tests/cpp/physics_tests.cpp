#include "fabric/physics/physics_world.hpp"
#include "fabric/physics/mechanic_plan.hpp"
#include "fabric/physics/mechanic_simulation.hpp"
#include "fabric/project/map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <ranges>

namespace {

fabric::project::MechanicValue default_value(
    const fabric::project::MechanicValueType type,
    const std::string_view id) {
    using Type = fabric::project::MechanicValueType;
    switch (type) {
    case Type::boolean: return false;
    case Type::integer: return std::int64_t{};
    case Type::scalar: return 0.0F;
    case Type::text:
        if (id == "body-type") return std::string{"kinematic"};
        if (id == "event-id") return std::string{"platform-start"};
        return std::string{"value"};
    case Type::vec2: return fabric::core::Vec2{1.0F, 1.0F};
    case Type::resource:
        return fabric::project::ResourceReference{{.value = "resource"}, "entity"};
    case Type::body_handle:
    case Type::pivot_handle:
    case Type::joint_handle:
        break;
    }
    return false;
}

fabric::project::MechanicNodeDefinition node(
    const fabric::project::MechanicNodeKind kind, std::string id) {
    const auto& schema = fabric::project::mechanic_node_schema(kind);
    fabric::project::MechanicNodeDefinition result{
        .id = std::move(id), .type = std::string{schema.type}};
    for (const auto& port : schema.ports)
        result.ports.push_back({
            .id = std::string{port.id}, .name = std::string{port.id},
            .direction = port.direction, .type = port.type});
    for (const auto& property : schema.properties) {
        if (!property.required) continue;
        result.properties.push_back({
            .id = std::string{property.id},
            .value = default_value(property.type, property.id)});
    }
    return result;
}

fabric::project::MechanicGraph complete_mechanic() {
    using Kind = fabric::project::MechanicNodeKind;
    fabric::project::MechanicGraph graph{
        .document = {.schema_version = 1,
                     .type = "mechanic",
                     .id = {.value = "platform-mechanic"},
                     .name = "Platform Mechanic"},
        .nodes = {node(Kind::body, "platform"),
                  node(Kind::pivot, "anchor"),
                  node(Kind::joint, "hinge"),
                  node(Kind::motor, "motor"),
                  node(Kind::sensor, "presence"),
                  node(Kind::constraint, "limit"),
                  node(Kind::event, "notify")}};
    graph.connections = {
        {"platform", "body", "anchor", "body"},
        {"platform", "body", "hinge", "body-a"},
        {"anchor", "pivot", "hinge", "pivot"},
        {"hinge", "joint", "motor", "joint"},
        {"platform", "body", "presence", "body"},
        {"presence", "active", "motor", "enabled"},
        {"platform", "body", "limit", "body"},
        {"anchor", "pivot", "limit", "pivot"},
        {"motor", "active", "notify", "trigger"}};
    graph.parameters = {{
        .id = "speed", .name = "Speed",
        .type = fabric::project::MechanicValueType::scalar,
        .default_value = 90.0F,
        .target_node = "motor", .target_property = "speed"}};
    return graph;
}

fabric::project::MapDocument mechanic_map() {
    fabric::project::MapDocument map;
    map.document.id = {.value = "mechanic-map"};
    map.document.name = "Mechanic Map";
    map.events = {{{.value = "platform-start"}, {}}};
    return map;
}

} // namespace

TEST_CASE("Box2D physics world owns a fixed-step world") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create());
    REQUIRE(world.valid());
    REQUIRE(world.step(1.0F / 60.0F, 4));
    world.destroy();
    REQUIRE_FALSE(world.valid());
    REQUIRE_FALSE(world.step(1.0F / 60.0F));
}

TEST_CASE("Box2D physics world rejects invalid steps") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create());
    REQUIRE_FALSE(world.step(0.0F));
    REQUIRE_FALSE(world.step(1.0F / 60.0F, 0));
}

TEST_CASE("Box2D physics world loads map collision shapes and sensors") {
    fabric::project::MapDocument map;
    map.document.id = {.value = "physics-map"};
    map.document.name = "Physics Map";
    map.layers = {{"collision", "Collision", fabric::project::MapLayerKind::collision,
                   true, false, 0.0F}};
    map.collisions = {{fabric::project::CollisionShapeKind::circle, "collision", false,
                       {0.0F, 0.0F}, 1.0F, 0.0F, {}},
                      {fabric::project::CollisionShapeKind::polygon, "collision", true,
                       {}, 0.0F, 0.0F, {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}}};
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create());
    REQUIRE(world.load_map_collisions(map));
    REQUIRE(world.step(1.0F / 60.0F));
}

TEST_CASE("Box2D physics world exposes a dynamic character body") {
    fabric::physics::PhysicsWorld world;
    REQUIRE(world.create(0.0F, 0.0F));
    REQUIRE(world.create_character({0.0F, 0.0F}));
    REQUIRE(world.character_valid());
    world.set_character_velocity({3.0F, 0.0F});
    REQUIRE(world.step(1.0F / 60.0F));
    CHECK(world.character_position().x > 0.0F);
    CHECK(world.character_velocity().x > 0.0F);
}

TEST_CASE("all mechanic nodes compile deterministically into fabric_physics") {
    const auto graph = complete_mechanic();
    const auto map = mechanic_map();
    const auto first = fabric::physics::compile_mechanic_graph(graph, map);
    const auto second = fabric::physics::compile_mechanic_graph(graph, map);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(*first.plan == *second.plan);
    CHECK(first.plan->bodies.size() == 1U);
    CHECK(first.plan->pivots.size() == 1U);
    CHECK(first.plan->joints.size() == 1U);
    CHECK(first.plan->motors.size() == 1U);
    CHECK(first.plan->sensors.size() == 1U);
    CHECK(first.plan->constraints.size() == 1U);
    CHECK(first.plan->events.size() == 1U);
    CHECK(first.plan->events.front().event_id.value == "platform-start");
    CHECK(first.plan->motors.front().enabled_source_node_id == "presence");
    CHECK(first.plan->motors.front().speed_degrees_per_second == 90.0F);
    CHECK(first.plan->motors.front().direction == 1);
    CHECK(first.plan->motors.front().acceleration_degrees_per_second_squared == 0.0F);
}

TEST_CASE("mechanic compilation rejects missing wiring and map events") {
    auto graph = complete_mechanic();
    auto map = mechanic_map();
    map.events.clear();
    CHECK_FALSE(fabric::physics::compile_mechanic_graph(graph, map).ok());

    map = mechanic_map();
    graph.connections.erase(std::ranges::find_if(
        graph.connections, [](const auto& connection) {
            return connection.to_node == "motor" &&
                   connection.to_port == "joint";
        }));
    CHECK_FALSE(fabric::physics::compile_mechanic_graph(graph, map).ok());
}

TEST_CASE("mechanic simulation owns ephemeral bodies and fixed-step controls") {
    auto graph = complete_mechanic();
    auto& body = graph.nodes.front();
    body.properties.front().value = std::string{"dynamic"};
    graph.connections.erase(std::ranges::find_if(
        graph.connections, [](const auto& connection) {
            return connection.to_node == "motor" &&
                   connection.to_port == "enabled";
        }));
    const auto compiled = fabric::physics::compile_mechanic_graph(
        graph, mechanic_map());
    REQUIRE(compiled.ok());
    fabric::physics::MechanicSimulation simulation;
    REQUIRE(simulation.load(*compiled.plan));
    REQUIRE(simulation.valid());
    REQUIRE_FALSE(simulation.playing());
    REQUIRE(simulation.step_once());
    CHECK(simulation.step_count() == 1U);
    REQUIRE(simulation.body_states().size() == 1U);
    simulation.play();
    REQUIRE_FALSE(simulation.step_once());
    REQUIRE(simulation.update(1.0F / 30.0F));
    CHECK(simulation.step_count() == 3U);
    simulation.pause();
    REQUIRE(simulation.reset());
    CHECK(simulation.step_count() == 0U);
    CHECK(simulation.body_states().front().position ==
          fabric::core::Vec2{1.0F, 1.0F});
}
