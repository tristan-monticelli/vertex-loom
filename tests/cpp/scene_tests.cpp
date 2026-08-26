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
                             {{.value = "menu-scene"}, "scene"}, "start",
                             fabric::core::ResourceId{.value = "open-menu"}}}};
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

TEST_CASE("scene composition mounts every map and resolves unique entry points") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-scene-composition-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    fabric::project::MapDocument foreground{
        .document = {.schema_version = 1, .type = "map",
                     .id = {.value = "foreground"}, .name = "Foreground"},
        .layers = {{"actors", "Actors",
                    fabric::project::MapLayerKind::instances,
                    true, false, 2.0F}},
        .instances = {{
            .id = "spawn-marker",
            .entity = fabric::project::ResourceReference{
                {.value = "hero"}, "entity"},
            .layer_id = "actors",
            .transform = {.position = {4.0F, 6.0F}},
            .properties = {{"sceneEntryPoint", std::string{"start"}}}}},
        .events = {{{.value = "open-door"}, {}}}};
    fabric::project::MapDocument background{
        .document = {.schema_version = 1, .type = "map",
                     .id = {.value = "background"}, .name = "Background"},
        .layers = {{"decor", "Decor",
                    fabric::project::MapLayerKind::instances,
                    true, false, -2.0F}},
        .instances = {{
            .id = "mountain",
            .entity = fabric::project::ResourceReference{
                {.value = "mountain"}, "entity"},
            .layer_id = "decor"}},
        .events = {{{.value = "open-door"}, {}}}};
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, foreground).ok());
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, background).ok());
    auto source = scene();
    source.maps = {{{{.value = "background"}, "map"}, "back"},
                   {{{.value = "foreground"}, "map"}, "front"}};
    source.entry_map = fabric::project::ResourceReference{
        {.value = "foreground"}, "map"};
    const auto composed = fabric::project::compose_scene_maps(
        root, project_manifest, source);
    REQUIRE(composed.ok());
    REQUIRE(composed.map->layers.size() == 2U);
    CHECK(composed.map->layers[0].id == "back-decor");
    CHECK(composed.map->layers[1].id == "front-actors");
    REQUIRE(composed.map->instances.size() == 2U);
    CHECK(composed.map->instances[0].id == "back-mountain");
    CHECK(composed.map->instances[1].id == "front-spawn-marker");
    REQUIRE(composed.map->events.size() == 1U);
    REQUIRE(composed.entry_points.size() == 1U);
    CHECK(composed.entry_points.front().id == "start");
    CHECK(composed.entry_points.front().instance_id == "front-spawn-marker");
    CHECK(composed.entry_points.front().position ==
          fabric::core::Vec2{4.0F, 6.0F});

    source.maps[1].layer_id = "back";
    CHECK_FALSE(fabric::project::validate_scene(
        project_manifest, source).ok());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
