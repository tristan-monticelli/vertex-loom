#include "fabric/runtime/scene_session.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/map_package.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "scene-session-test"}, .name = "Scene Session Test"};
}

fabric::project::MapDocument map(const char* id) {
    return {.document = {.schema_version = 1, .type = "map",
                         .id = {.value = id}, .name = id},
            .layers = {{"world", "World", fabric::project::MapLayerKind::instances,
                        true, false, 0.0F}}};
}

fabric::project::SceneDocument scene(const char* id, const char* map_id) {
    return {.document = {.schema_version = 1, .type = "scene",
                         .id = {.value = id}, .name = id},
            .maps = {{{{.value = map_id}, "map"}, "world"}},
            .entry_map = fabric::project::ResourceReference{{.value = map_id}, "map"}};
}

TEST_CASE("scene runtime session transitions atomically between scenes") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-scene-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_entity(
        root, project_manifest,
        {.document = {.schema_version = 1, .type = "entity",
                      .id = {.value = "marker-entity"},
                      .name = "Marker Entity"}}).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map("map-a")).ok());
    auto target_map = map("map-b");
    target_map.instances.push_back({
        .id = "start-marker",
        .entity = fabric::project::ResourceReference{
            {.value = "marker-entity"}, "entity"},
        .layer_id = "world",
        .transform = {.position = {8.0F, 3.0F}},
        .properties = {{"sceneEntryPoint", std::string{"start"}}}});
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, target_map).ok());
    auto first = scene("scene-a", "map-a");
    first.transitions.push_back({"to-b", {{.value = "scene-b"}, "scene"}, "start",
                                 fabric::core::ResourceId{.value = "open-door"}});
    first.transitions.push_back({"to-missing-point",
                                 {{.value = "scene-b"}, "scene"}, "missing",
                                 std::nullopt});
    REQUIRE(fabric::project::publish_scene(root, project_manifest, first).ok());
    REQUIRE(fabric::project::publish_scene(root, project_manifest,
        scene("scene-b", "map-b")).ok());

    fabric::runtime::SceneRuntimeSession session;
    const auto loaded = session.load(root, {.value = "scene-a"});
    const auto diagnostic = session.errors().empty()
        ? std::string{"no error"} : session.errors().front();
    INFO(diagnostic);
    REQUIRE(loaded);
    REQUIRE(session.scene().has_value());
    REQUIRE(session.map().has_value());
    CHECK(session.map()->document.id.value == "scene-a");
    CHECK(session.scene()->transitions.front().event_id->value == "open-door");
    CHECK_FALSE(session.transition("to-missing-point"));
    CHECK(session.scene()->document.id.value == "scene-a");
    REQUIRE(session.transition_for_event({.value = "open-door"}));
    CHECK(session.scene()->document.id.value == "scene-b");
    CHECK(session.map()->document.id.value == "scene-b");
    REQUIRE(session.entry_point().has_value());
    CHECK(session.entry_point()->id == "start");
    CHECK(session.entry_point()->position == fabric::core::Vec2{8.0F, 3.0F});
    REQUIRE(session.load(root, {.value = "scene-a"}));
    REQUIRE(session.transition("to-b"));
    CHECK(session.scene()->document.id.value == "scene-b");
    CHECK(session.map()->document.id.value == "scene-b");
    CHECK_FALSE(session.transition("missing"));
    CHECK(session.scene()->document.id.value == "scene-b");
    CHECK_FALSE(session.transition_for_event({.value = "missing-event"}));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("scene runtime session transitions inside a published campaign") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-scene-package-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto package = root.string() + "-package";
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_entity(
        root, project_manifest,
        {.document = {.schema_version = 1, .type = "entity",
                      .id = {.value = "marker-entity"},
                      .name = "Marker Entity"}}).ok());
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, map("map-a")).ok());
    auto target_map = map("map-b");
    target_map.instances.push_back({
        .id = "start-marker",
        .entity = {{{.value = "marker-entity"}, "entity"}},
        .layer_id = "world",
        .transform = {.position = {4.0F, 2.0F}},
        .properties = {{"sceneEntryPoint", std::string{"start"}}}});
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, target_map).ok());
    auto first = scene("scene-a", "map-a");
    first.transitions.push_back(
        {"to-b", {{.value = "scene-b"}, "scene"}, "start",
         fabric::core::ResourceId{.value = "open-door"}});
    auto second = scene("scene-b", "map-b");
    second.transitions.push_back(
        {"to-a", {{.value = "scene-a"}, "scene"}, "start",
         std::nullopt});
    REQUIRE(fabric::project::publish_scene(
        root, project_manifest, first).ok());
    REQUIRE(fabric::project::publish_scene(
        root, project_manifest, second).ok());
    REQUIRE(fabric::project::publish_scene_package(
        root, {.value = "scene-a"}, package).ok());

    fabric::runtime::SceneRuntimeSession session;
    REQUIRE(session.load_package(package));
    CHECK(session.scene()->document.id.value == "scene-a");
    REQUIRE(session.transition_for_event({.value = "open-door"}));
    CHECK(session.scene()->document.id.value == "scene-b");
    REQUIRE(session.entry_point().has_value());
    CHECK(session.entry_point()->position == fabric::core::Vec2{4.0F, 2.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::remove_all(package, ignored);
}

} // namespace
