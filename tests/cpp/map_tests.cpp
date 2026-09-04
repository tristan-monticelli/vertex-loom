#include "fabric/project/map.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "map-test"},
            .name = "Map Test",
            .directories = {}};
}

fabric::project::MapDocument map() {
    using namespace fabric::project;
    return {
        .document = {.schema_version = 1,
                     .type = "map",
                     .id = {.value = "test-map"},
                     .name = "Test Map"},
        .layers = {{"instances", "Instances", MapLayerKind::instances, true, false, 0.0F},
                   {"collision", "Collision", MapLayerKind::collision, true, false, 0.0F},
                   {"triggers", "Triggers", MapLayerKind::triggers, true, false, 1.0F}},
        .prefabs = {{.id = "hero",
                     .entity = {{.value = "hero-entity"}, "entity"}}},
        .instances = {{"hero-1", std::nullopt,
                       ResourceReference{{.value = "hero"}, "prefab"}, "instances",
                       {.position = {65.0F, -1.0F}}, 1, -1, {}}},
        .collisions = {{CollisionShapeKind::circle, "collision", true,
                        {0.0F, 0.0F}, 2.0F, 0.0F, {}}},
        .triggers = {{"spawn", "triggers", 0, {.value = "on-spawn"}, {}}},
        .events = {{{.value = "on-spawn"}, {}}},
    };
}

TEST_CASE("map document round-trips and publishes atomically") {
    const auto source = map();
    const auto parsed = fabric::project::parse_map(
        manifest(), fabric::project::serialize_map(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.asset == source);

    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "maps");
    const auto published = fabric::project::publish_map(root, manifest(), source);
    REQUIRE(published.ok());
    REQUIRE(std::filesystem::is_regular_file(root / "maps/test-map.map.json"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map instance path follower is optional and round-trips") {
    auto source = map();
    source.instances.front().path_follower = fabric::project::PathFollowerState{
        .path = {{.value = "hero-path"}, "texturedPath"},
        .progress = 0.25F,
        .speed = 3.0F,
        .loop = false,
        .orient_to_path = true,
        .rotation_offset_degrees = 15.0F};
    const auto parsed = fabric::project::parse_map(
        manifest(), fabric::project::serialize_map(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.asset == source);
}

TEST_CASE("map validation rejects path followers on mechanic prefabs") {
    auto invalid = map();
    invalid.prefabs.front().mechanic =
        fabric::project::ResourceReference{{.value = "hero-mechanic"}, "mechanic"};
    invalid.instances.front().path_follower = fabric::project::PathFollowerState{
        .path = {{.value = "hero-path"}, "texturedPath"}};
    const auto report = fabric::project::validate_map(manifest(), invalid);
    REQUIRE_FALSE(report.ok());
    CHECK(std::any_of(report.errors.begin(), report.errors.end(), [](const auto& error) {
        return error.field == "instances.pathFollower";
    }));
}

TEST_CASE("map persists collision marker surfaces and derives eight positions") {
    auto source = map();
    fabric::project::CollisionMarkerConfig marker;
    marker.enabled = true;
    marker.visible_in_runtime = true;
    marker.shader.primary_color = {1.0F, 0.1F, 0.1F, 0.9F};
    source.collision_surfaces = {marker};
    source.collisions.front().marker_override = marker;
    const auto parsed = fabric::project::parse_map(
        manifest(), fabric::project::serialize_map(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);
    CHECK(fabric::project::collision_marker_positions(source.collisions.front()).size() == 8U);
}

TEST_CASE("map validation rejects chunk mismatches and missing layers") {
    auto invalid = map();
    invalid.instances.front().chunk_x = 0;
    invalid.instances.front().layer_id = "missing";
    REQUIRE_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
}

TEST_CASE("map validation rejects malformed instance animation bindings") {
    auto invalid = map();
    invalid.instances.front().properties.push_back({
        "animation", std::string{"not-a-resource-reference"}});
    REQUIRE_FALSE(fabric::project::validate_map(manifest(), invalid).ok());

    invalid = map();
    invalid.instances.front().properties.push_back({
        "animation", fabric::project::ResourceReference{
            {.value = "walk"}, "texture"}});
    REQUIRE_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
}

TEST_CASE("map validation requires closed sensor trigger zones") {
    auto invalid = map();
    invalid.collisions.front().sensor = false;
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());

    invalid = map();
    invalid.collisions.front().kind =
        fabric::project::CollisionShapeKind::chain;
    invalid.collisions.front().points = {{0.0F, 0.0F}, {1.0F, 0.0F}};
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());

    invalid = map();
    invalid.instances.front().properties.push_back(
        {"triggerHalfExtents", fabric::core::Vec2{-1.0F, 1.0F}});
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());

    auto valid = map();
    valid.instances.front().properties.push_back(
        {"triggerHalfExtents", fabric::core::Vec2{2.0F, 1.0F}});
    valid.instances.front().properties.push_back({"triggerActor", false});
    CHECK(fabric::project::validate_map(manifest(), valid).ok());
}

TEST_CASE("map prefabs round trip optional mechanic parameter overrides") {
    auto source = map();
    source.prefabs.front().mechanic = fabric::project::ResourceReference{
        {.value = "rotating-platform"}, "mechanic"};
    source.prefabs.front().mechanic_overrides = {
        {"speed", 120.0F},
        {"direction", std::int64_t{-1}},
        {"sensor-size", fabric::core::Vec2{8.0F, 3.0F}}};
    const auto parsed = fabric::project::parse_map(
        manifest(), fabric::project::serialize_map(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);

    auto invalid = source;
    invalid.prefabs.front().mechanic.reset();
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
    invalid = source;
    invalid.prefabs.front().mechanic_overrides.push_back({"speed", 30.0F});
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
    invalid = source;
    invalid.prefabs.front().mechanic_overrides.front().value =
        std::numeric_limits<float>::infinity();
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
    invalid = source;
    invalid.instances.front().transform.scale = {2.0F, 1.0F};
    CHECK_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
}

} // namespace
