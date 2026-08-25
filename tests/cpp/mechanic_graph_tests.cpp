#include "fabric/project/mechanic_graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "mechanic-tests"},
            .name = "Mechanic Tests"};
}

fabric::project::MechanicGraph graph() {
    using Direction = fabric::project::MechanicPortDirection;
    using Type = fabric::project::MechanicValueType;
    return {
        .document = {.schema_version = 1,
                     .type = "mechanic",
                     .id = {.value = "rotating-platform"},
                     .name = "Rotating Platform"},
        .parameters = {
            {.id = "speed", .name = "Speed", .type = Type::scalar,
             .default_value = 2.0F, .target_node = "platform",
             .target_property = "rotation"},
            {.id = "position", .name = "Position", .type = Type::vec2,
             .default_value = fabric::core::Vec2{},
             .target_node = "platform", .target_property = "position"}},
        .nodes = {
            {.id = "platform", .type = "body",
             .ports = {{.id = "body", .name = "Body",
                        .direction = Direction::output,
                        .type = Type::body_handle}},
             .properties = {
                 {.id = "body-type", .value = std::string{"kinematic"}},
                 {.id = "position", .value = fabric::core::Vec2{}},
                 {.id = "size", .value = fabric::core::Vec2{4.0F, 0.5F}},
                 {.id = "rotation", .value = 0.0F},
                 {.id = "density", .value = 1.0F},
                 {.id = "friction", .value = 0.8F}}},
            {.id = "pivot", .type = "pivot",
             .ports = {{.id = "body", .name = "Body",
                        .direction = Direction::input,
                        .type = Type::body_handle},
                       {.id = "pivot", .name = "Pivot",
                        .direction = Direction::output,
                        .type = Type::pivot_handle}},
             .properties = {{.id = "position",
                             .value = fabric::core::Vec2{}}}}},
        .connections = {{"platform", "body", "pivot", "body"}},
    };
}

std::filesystem::path temporary_root(const std::string& prefix) {
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

bool has_code(const fabric::project::ValidationReport& report,
              const fabric::project::ErrorCode code) {
    return std::ranges::any_of(report.errors, [&](const auto& error) {
        return error.code == code;
    });
}

fabric::project::MechanicValue schema_default(
    const fabric::project::MechanicValueType type,
    const std::string_view property_id) {
    using Type = fabric::project::MechanicValueType;
    switch (type) {
    case Type::boolean: return false;
    case Type::integer: return std::int64_t{};
    case Type::scalar: return 0.0F;
    case Type::text:
        if (property_id == "body-type") return std::string{"kinematic"};
        if (property_id == "event-id") return std::string{"mechanic-event"};
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

fabric::project::MechanicNodeDefinition complete_node(
    const fabric::project::MechanicNodeKind kind, std::string id) {
    const auto& schema = fabric::project::mechanic_node_schema(kind);
    fabric::project::MechanicNodeDefinition node{
        .id = std::move(id), .type = std::string{schema.type}};
    for (const auto& port : schema.ports)
        node.ports.push_back({
            .id = std::string{port.id}, .name = std::string{port.id},
            .direction = port.direction, .type = port.type});
    for (const auto& property : schema.properties) {
        if (!property.required) continue;
        node.properties.push_back({
            .id = std::string{property.id},
            .value = schema_default(property.type, property.id)});
    }
    return node;
}

} // namespace

TEST_CASE("mechanic graph v1 round trips typed ports and parameters") {
    const auto source = graph();
    const auto serialized = fabric::project::serialize_mechanic_graph(source);
    const auto parsed = fabric::project::parse_mechanic_graph(
        manifest(), serialized);
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);

    auto unknown = serialized;
    unknown.insert(unknown.find('{') + 1U, "\n  \"script\": \"run\",");
    CHECK_FALSE(fabric::project::parse_mechanic_graph(
                    manifest(), unknown).ok());
}

TEST_CASE("all seven built-in mechanic node schemas are authorable") {
    auto source = graph();
    source.parameters.clear();
    source.nodes.clear();
    source.connections.clear();
    const std::array kinds{
        fabric::project::MechanicNodeKind::body,
        fabric::project::MechanicNodeKind::pivot,
        fabric::project::MechanicNodeKind::joint,
        fabric::project::MechanicNodeKind::motor,
        fabric::project::MechanicNodeKind::sensor,
        fabric::project::MechanicNodeKind::constraint,
        fabric::project::MechanicNodeKind::event};
    for (const auto kind : kinds) {
        const auto type = fabric::project::to_string(kind);
        REQUIRE(fabric::project::mechanic_node_kind(type) == kind);
        source.nodes.push_back(complete_node(kind, std::string{type}));
    }
    CHECK(fabric::project::validate_mechanic_graph(manifest(), source).ok());

    auto legacy_event = complete_node(
        fabric::project::MechanicNodeKind::event, "legacy-event");
    std::erase_if(legacy_event.ports, [](const auto& port) {
        return port.id == "active";
    });
    source.parameters.clear();
    source.nodes = {legacy_event};
    source.connections.clear();
    CHECK(fabric::project::validate_mechanic_graph(manifest(), source).ok());

    source.nodes.clear();
    for (const auto kind : kinds)
        source.nodes.push_back(complete_node(
            kind, std::string{fabric::project::to_string(kind)}));
    source.nodes.front().ports.pop_back();
    source.nodes.back().properties.front().value = std::string{"Bad event id"};
    const auto invalid = fabric::project::validate_mechanic_graph(
        manifest(), source);
    CHECK_FALSE(invalid.ok());
    CHECK(has_code(invalid, fabric::project::ErrorCode::missing_resource));
    CHECK(has_code(invalid, fabric::project::ErrorCode::invalid_resource_id));

    source = graph();
    source.nodes.front().properties[2].value =
        fabric::core::Vec2{-1.0F, 0.0F};
    source.nodes.push_back(complete_node(
        fabric::project::MechanicNodeKind::joint, "joint"));
    source.nodes.back().properties[1].value = 30.0F;
    source.nodes.back().properties[2].value = -30.0F;
    source.nodes.push_back(complete_node(
        fabric::project::MechanicNodeKind::motor, "motor"));
    source.nodes.back().properties[1].value = -1.0F;
    source.nodes.back().properties.push_back({"direction", std::int64_t{0}});
    source.nodes.back().properties.push_back({"acceleration", -1.0F});
    source.nodes.push_back(complete_node(
        fabric::project::MechanicNodeKind::sensor, "sensor"));
    source.nodes.back().properties[1].value = fabric::core::Vec2{};
    CHECK_FALSE(fabric::project::validate_mechanic_graph(
                    manifest(), source).ok());

    source = graph();
    auto invalid_event = complete_node(
        fabric::project::MechanicNodeKind::event, "event");
    invalid_event.properties.push_back({"mode", std::string{"listen"}});
    std::erase_if(invalid_event.ports, [](const auto& port) {
        return port.id == "active";
    });
    source.nodes.push_back(std::move(invalid_event));
    CHECK_FALSE(fabric::project::validate_mechanic_graph(
                    manifest(), source).ok());
}

TEST_CASE("mechanic graph rejects invalid typed connections and cycles") {
    auto invalid = graph();
    invalid.nodes[1].ports[0].type =
        fabric::project::MechanicValueType::boolean;
    auto report = fabric::project::validate_mechanic_graph(
        manifest(), invalid);
    CHECK_FALSE(report.ok());
    CHECK(has_code(report,
                   fabric::project::ErrorCode::resource_type_mismatch));

    invalid = graph();
    invalid.nodes[0].ports.push_back({
        .id = "feedback", .name = "Feedback",
        .direction = fabric::project::MechanicPortDirection::input,
        .type = fabric::project::MechanicValueType::scalar});
    invalid.nodes[1].ports.push_back({
        .id = "value", .name = "Value",
        .direction = fabric::project::MechanicPortDirection::output,
        .type = fabric::project::MechanicValueType::scalar});
    invalid.connections.push_back({"pivot", "value", "platform", "feedback"});
    report = fabric::project::validate_mechanic_graph(manifest(), invalid);
    CHECK_FALSE(report.ok());
    CHECK(has_code(report, fabric::project::ErrorCode::resource_cycle));

    invalid = graph();
    invalid.connections.push_back(invalid.connections.front());
    invalid.connections.push_back({"missing", "value", "pivot", "body"});
    report = fabric::project::validate_mechanic_graph(manifest(), invalid);
    CHECK_FALSE(report.ok());
    CHECK(has_code(report, fabric::project::ErrorCode::duplicate_resource));
    CHECK(has_code(report, fabric::project::ErrorCode::missing_resource));

    invalid = graph();
    invalid.parameters[0].default_value = true;
    invalid.nodes[1].properties[0].value =
        std::numeric_limits<float>::infinity();
    invalid.nodes[0].properties.push_back({
        .id = "resource",
        .value = fabric::project::ResourceReference{{.value = "Bad id"}, ""}});
    report = fabric::project::validate_mechanic_graph(manifest(), invalid);
    CHECK_FALSE(report.ok());
    CHECK(has_code(report,
                   fabric::project::ErrorCode::resource_type_mismatch));
    CHECK(has_code(report, fabric::project::ErrorCode::invalid_asset));

    invalid = graph();
    invalid.parameters[1].target_node = "platform";
    invalid.parameters[1].target_property = "rotation";
    report = fabric::project::validate_mechanic_graph(manifest(), invalid);
    CHECK_FALSE(report.ok());
    CHECK(has_code(report,
                   fabric::project::ErrorCode::resource_type_mismatch));
    CHECK(has_code(report, fabric::project::ErrorCode::duplicate_resource));
}

TEST_CASE("mechanic graph publication is atomic and registered headlessly") {
    const auto root = temporary_root("fabric-mechanic-graph");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source = graph();
    REQUIRE(fabric::project::publish_mechanic_graph(
                root, manifest(), source).ok());
    CHECK(fabric::project::validate_project(root).ok());

    source.parameters.push_back({
        .id = "body", .name = "Body",
        .type = fabric::project::MechanicValueType::resource,
        .default_value = fabric::project::ResourceReference{
            {.value = "missing-body"}, "entity"},
        .target_node = "platform", .target_property = "entity"});
    source.nodes.front().properties.push_back({
        .id = "entity",
        .value = fabric::project::ResourceReference{
            {.value = "missing-body"}, "entity"}});
    REQUIRE(fabric::project::publish_mechanic_graph(
                root, manifest(), source).ok());
    const auto missing = fabric::project::validate_project(root);
    CHECK_FALSE(missing.ok());
    CHECK(has_code(missing, fabric::project::ErrorCode::missing_resource));

    source.document.schema_version = 2;
    CHECK_FALSE(fabric::project::publish_mechanic_graph(
                    root, manifest(), source).ok());
    const auto persisted = fabric::project::load_mechanic_graph(
        root, manifest(), fabric::project::mechanic_graph_document_path(
            manifest(), source.document.id));
    REQUIRE(persisted.ok());
    CHECK(persisted.asset->document.schema_version == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
