#include "fabric/runtime/scene_session.hpp"

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
    REQUIRE(fabric::project::publish_map(root, project_manifest, map("map-a")).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map("map-b")).ok());
    auto first = scene("scene-a", "map-a");
    first.transitions.push_back({"to-b", {{.value = "scene-b"}, "scene"}, "start",
                                 fabric::core::ResourceId{.value = "open-door"}});
    REQUIRE(fabric::project::publish_scene(root, project_manifest, first).ok());
    REQUIRE(fabric::project::publish_scene(root, project_manifest,
        scene("scene-b", "map-b")).ok());

    fabric::runtime::SceneRuntimeSession session;
    REQUIRE(session.load(root, {.value = "scene-a"}));
    REQUIRE(session.scene().has_value());
    REQUIRE(session.map().has_value());
    CHECK(session.map()->document.id.value == "map-a");
    CHECK(session.scene()->transitions.front().event_id->value == "open-door");
    REQUIRE(session.transition_for_event({.value = "open-door"}));
    CHECK(session.scene()->document.id.value == "scene-b");
    CHECK(session.map()->document.id.value == "map-b");
    REQUIRE(session.load(root, {.value = "scene-a"}));
    REQUIRE(session.transition("to-b"));
    CHECK(session.scene()->document.id.value == "scene-b");
    CHECK(session.map()->document.id.value == "map-b");
    CHECK_FALSE(session.transition("missing"));
    CHECK(session.scene()->document.id.value == "scene-b");
    CHECK_FALSE(session.transition_for_event({.value = "missing-event"}));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
