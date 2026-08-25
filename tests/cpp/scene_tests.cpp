#include "fabric/project/scene.hpp"

#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "scene-test"}, .name = "Scene Test"};
}

fabric::project::SceneDocument scene() {
    return {.document = {.schema_version = 1, .type = "scene",
                         .id = {.value = "main-scene"}, .name = "Main Scene"},
            .maps = {{{{.value = "main-map"}, "map"}, "world"}},
            .entry_map = fabric::project::ResourceReference{{.value = "main-map"}, "map"},
            .transitions = {{"to-menu",
                             {{.value = "menu-scene"}, "scene"}, "start"}}};
}

TEST_CASE("scene documents round trip and publish atomically") {
    const auto source = scene();
    const auto parsed = fabric::project::parse_scene(
        manifest(), fabric::project::serialize_scene(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);

    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-scene-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "scenes");
    REQUIRE(fabric::project::publish_scene(root, manifest(), source).ok());
    REQUIRE(std::filesystem::is_regular_file(root / "scenes/main-scene.scene.json"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("scene validation requires the entry map to be declared") {
    auto invalid = scene();
    invalid.entry_map->id.value = "missing-map";
    REQUIRE_FALSE(fabric::project::validate_scene(manifest(), invalid).ok());
}

} // namespace
