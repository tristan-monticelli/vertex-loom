#include "fabric/project/map.hpp"

#include <chrono>
#include <filesystem>
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
        .prefabs = {{"hero", {{.value = "hero-entity"}, "entity"}, {}}},
        .instances = {{"hero-1", std::nullopt,
                       ResourceReference{{.value = "hero"}, "prefab"}, "instances",
                       {.position = {65.0F, -1.0F}}, 1, -1, {}}},
        .collisions = {{CollisionShapeKind::circle, "collision", false,
                        {0.0F, 0.0F}, 2.0F, 0.0F, {}}},
        .triggers = {{"spawn", "triggers", 0, {.value = "on-spawn"}, {}}},
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

TEST_CASE("map validation rejects chunk mismatches and missing layers") {
    auto invalid = map();
    invalid.instances.front().chunk_x = 0;
    invalid.instances.front().layer_id = "missing";
    REQUIRE_FALSE(fabric::project::validate_map(manifest(), invalid).ok());
}

} // namespace
