#include "fabric/editor/scene_session.hpp"

#include "fabric/project/map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "scene-editor-session"},
            .name = "Scene Editor Session"};
}

fabric::project::MapDocument map(const char* id) {
    return {.document = {.schema_version = 1, .type = "map",
                         .id = {.value = id}, .name = id},
            .layers = {{"instances", "Instances",
                        fabric::project::MapLayerKind::instances,
                        true, false, 0.0F}}};
}

fabric::project::SceneDocument empty_scene() {
    return {.document = {.schema_version = 1, .type = "scene",
                         .id = {.value = "campaign"},
                         .name = "Campaign"}};
}

} // namespace

TEST_CASE("scene editor session covers maps transitions history and recovery") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-scene-editor-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, map("map-a")).ok());
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, map("map-b")).ok());

    fabric::editor::SceneSession session;
    REQUIRE(session.create(root, empty_scene()));
    CHECK_FALSE(session.dirty());
    REQUIRE(session.add_map({{{.value = "map-a"}, "map"}, "world"}));
    REQUIRE(session.add_map({{{.value = "map-b"}, "map"}, "overlay"}));
    REQUIRE(session.set_entry_map(
        fabric::core::ResourceId{.value = "map-a"}));
    REQUIRE(session.add_transition({
        "continue", {{.value = "next-scene"}, "scene"}, "start",
        fabric::core::ResourceId{.value = "level-finished"}}));
    REQUIRE(session.set_map(
        1U, {{{.value = "map-b"}, "map"}, "foreground"}));
    REQUIRE(session.set_transition(
        0U, {"continue", {{.value = "next-scene"}, "scene"}, "door",
             fabric::core::ResourceId{.value = "door-opened"}}));
    CHECK(session.dirty());
    REQUIRE(session.can_undo());
    REQUIRE(session.undo());
    CHECK(session.scene()->transitions.front().entry_point == "start");
    REQUIRE(session.redo());
    CHECK(session.scene()->transitions.front().entry_point == "door");
    REQUIRE_FALSE(session.add_map(
        {{{.value = "map-a"}, "map"}, "duplicate"}));
    CHECK_FALSE(session.errors().empty());

    REQUIRE(session.save());
    CHECK_FALSE(session.dirty());
    REQUIRE(session.set_name("Recovered Campaign"));
    const auto changed = fabric::editor::AutosaveScheduler::Clock::now();
    CHECK(session.update_autosave(changed) ==
          fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(changed + std::chrono::seconds{31}) ==
          fabric::editor::AutosaveStatus::saved);

    fabric::editor::SceneSession recovered;
    REQUIRE(recovered.open(root, {.value = "campaign"}));
    REQUIRE(recovered.has_recovery());
    CHECK(recovered.scene()->document.name == "Campaign");
    REQUIRE(recovered.accept_recovery());
    CHECK(recovered.scene()->document.name == "Recovered Campaign");
    CHECK(recovered.dirty());
    REQUIRE(recovered.remove_transition(0U));
    REQUIRE(recovered.remove_map(0U));
    CHECK_FALSE(recovered.scene()->entry_map.has_value());
    REQUIRE(recovered.save());

    fabric::editor::SceneSession reloaded;
    REQUIRE(reloaded.open(root, {.value = "campaign"}));
    CHECK(*reloaded.scene() == *recovered.scene());
    CHECK(reloaded.scene()->maps.size() == 1U);
    CHECK(reloaded.scene()->transitions.empty());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
