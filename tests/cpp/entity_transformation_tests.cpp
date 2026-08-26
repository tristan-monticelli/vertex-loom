#include "fabric/project/entity_transformation.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/manifest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace {
fabric::project::ProjectManifest manifest() {
    fabric::project::ProjectManifest value;
    value.id = {.value = "transformation-test"};
    value.name = "Transformation Test";
    return value;
}

fabric::project::EntityTransformation transformation() {
    fabric::project::EntityTransformation value;
    value.document.id = {.value = "hero-to-beast"};
    value.document.name = "Hero to Beast";
    value.source_entity = {{.value = "hero"}, "entity"};
    value.destination_entity = {{.value = "beast"}, "entity"};
    value.policy.properties = fabric::project::TransferMode::mapping;
    value.policy.mappings.push_back({fabric::project::TransferDomain::property,
                                     "health", "health"});
    return value;
}

std::filesystem::path temporary_root() {
    return std::filesystem::temp_directory_path() /
        ("fabric-entity-transformation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

fabric::project::EntityDefinition entity(std::string id) {
    fabric::project::EntityDefinition value;
    value.document.id = {.value = std::move(id)};
    value.document.name = value.document.id.value;
    return value;
}
}

TEST_CASE("EntityTransformation v1 round-trips every transfer policy") {
    const auto original = transformation();
    const auto parsed = fabric::project::parse_entity_transformation(
        manifest(), fabric::project::serialize_entity_transformation(original));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == original);
    const auto references =
        fabric::project::entity_transformation_resource_references(original);
    REQUIRE(references.size() == 1U);
    CHECK(references.front() == original.destination_entity);
}

TEST_CASE("EntityTransformation rejects identity and incomplete mappings") {
    auto identity = transformation();
    identity.destination_entity = identity.source_entity;
    CHECK_FALSE(fabric::project::validate_entity_transformation(
        manifest(), identity).ok());

    auto missing = transformation();
    missing.policy.mappings.clear();
    CHECK_FALSE(fabric::project::validate_entity_transformation(
        manifest(), missing).ok());

    auto invalid = transformation();
    invalid.policy.world_transform = fabric::project::TransferMode::mapping;
    CHECK_FALSE(fabric::project::validate_entity_transformation(
        manifest(), invalid).ok());
}

TEST_CASE("EntityTransformation refuses unknown persistent fields") {
    auto json = fabric::project::serialize_entity_transformation(transformation());
    json.insert(json.find("\"type\""), "\"mystery\":true,\n  ");
    const auto parsed = fabric::project::parse_entity_transformation(manifest(), json);
    CHECK_FALSE(parsed.ok());
}

TEST_CASE("project validation resolves both transformation endpoints") {
    const auto root = temporary_root();
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), entity("hero")).ok());
    REQUIRE(fabric::project::publish_entity_transformation(
        root, manifest(), transformation()).ok());

    const auto missing = fabric::project::validate_project(root);
    CHECK_FALSE(missing.ok());
    CHECK(std::ranges::any_of(missing.errors, [](const auto& issue) {
        return issue.code == fabric::project::ErrorCode::missing_resource &&
            issue.field == "hero-to-beast.destinationEntity";
    }));

    REQUIRE(fabric::project::publish_entity(
        root, manifest(), entity("beast")).ok());
    CHECK(fabric::project::validate_project(root).ok());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
