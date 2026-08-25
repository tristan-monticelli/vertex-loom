#include "fabric/project/mechanic_graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
             .default_value = 2.0F},
            {.id = "enabled", .name = "Enabled", .type = Type::boolean,
             .default_value = true}},
        .nodes = {
            {.id = "source", .type = "parameter",
             .ports = {{.id = "value", .name = "Value",
                        .direction = Direction::output,
                        .type = Type::scalar}}},
            {.id = "target", .type = "consumer",
             .ports = {{.id = "speed", .name = "Speed",
                        .direction = Direction::input,
                        .type = Type::scalar}},
             .properties = {{.id = "label", .value = std::string{"motor"}}}}},
        .connections = {{"source", "value", "target", "speed"}},
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
    invalid.connections.push_back({"target", "value", "source", "feedback"});
    report = fabric::project::validate_mechanic_graph(manifest(), invalid);
    CHECK_FALSE(report.ok());
    CHECK(has_code(report, fabric::project::ErrorCode::resource_cycle));

    invalid = graph();
    invalid.connections.push_back(invalid.connections.front());
    invalid.connections.push_back({"missing", "value", "target", "speed"});
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
