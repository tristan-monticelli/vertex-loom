#include "fabric/editor/map_session.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "session-map"}, .name = "Session Map"};
}

fabric::project::MapDocument map() {
    return {.document = {.schema_version = 1, .type = "map",
                         .id = {.value = "session"}, .name = "Session"},
            .layers = {{"instances", "Instances",
                        fabric::project::MapLayerKind::instances, true, false, 0.0F},
                       {"collision", "Collision",
                        fabric::project::MapLayerKind::collision, true, false, 0.0F},
                       {"triggers", "Triggers",
                        fabric::project::MapLayerKind::triggers, true, false, 0.0F}},
            .collisions = {{fabric::project::CollisionShapeKind::circle, "collision", true,
                            {}, 1.0F, 0.0F, {}}}};
}

fabric::project::MapDocument map_with_prefab() {
    auto result = map();
    result.layers.push_back({"gameplay", "Gameplay",
                             fabric::project::MapLayerKind::gameplay, true, false, 2.0F});
    result.prefabs.push_back({"hero-prefab",
                              fabric::project::ResourceReference{{.value = "entity"}, "entity"},
                              {}});
    return result;
}

fabric::project::MapInstance instance(std::string id, float x) {
    return {std::move(id),
            fabric::project::ResourceReference{{.value = "entity"}, "entity"},
            std::nullopt, "instances", {.position = {x, 0.0F}}, 0, 0, {}};
}

TEST_CASE("map session places, moves, saves and undoes instances") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, map()));
    REQUIRE(session.place_instance(instance("hero", 65.0F)));
    REQUIRE(session.map()->instances.front().chunk_x == 1);
    REQUIRE(session.set_instance_transform(
        {.value = "hero"}, {.position = {-1.0F, -65.0F}}));
    REQUIRE(session.map()->instances.front().chunk_x == -1);
    REQUIRE(session.map()->instances.front().chunk_y == -2);
    REQUIRE(session.undo());
    REQUIRE(session.map()->instances.front().chunk_x == 1);
    REQUIRE(session.redo());
    REQUIRE(session.declare_event({{.value = "on-enter"}, {}}));
    REQUIRE(session.add_trigger({"enter", "triggers", 0, {.value = "on-enter"}, {}}));
    REQUIRE_FALSE(session.remove_event({.value = "on-enter"}));
    REQUIRE(session.remove_trigger({.value = "enter"}));
    REQUIRE(session.remove_event({.value = "on-enter"}));
    REQUIRE(session.save());

    fabric::editor::MapSession reopened;
    REQUIRE(reopened.open(root, {.value = "session"}));
    REQUIRE(reopened.map()->instances.front().chunk_y == -2);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map session snaps placement on a configurable grid") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-snap-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, map()));
    REQUIRE(session.place_instance(instance("hero", 6.2F),
                                   {.enabled = true, .grid_size = 4.0F,
                                    .origin = {1.0F, -1.0F}}));
    CHECK(session.map()->instances.front().transform.position == fabric::core::Vec2{5.0F, -1.0F});
    CHECK(session.map()->instances.front().chunk_x == 0);
    REQUIRE(session.set_instance_transform(
        {.value = "hero"}, {.position = {-3.1F, -6.1F}, .rotation_degrees = 17.0F},
        {.enabled = false, .grid_size = 4.0F}));
    CHECK(session.map()->instances.front().transform.position == fabric::core::Vec2{-3.1F, -6.1F});
    CHECK(session.map()->instances.front().transform.rotation_degrees == 17.0F);
    REQUIRE(session.set_instance_property(
        {.value = "hero"}, {"health", std::int64_t{3}}));
    REQUIRE(session.map()->instances.front().properties.size() == 1);
    CHECK(std::get<std::int64_t>(session.map()->instances.front().properties.front().value) == 3);
    REQUIRE(session.set_instance_property(
        {.value = "hero"}, {"health", std::int64_t{4}}));
    CHECK(session.map()->instances.front().properties.size() == 1);
    CHECK(std::get<std::int64_t>(session.map()->instances.front().properties.front().value) == 4);
    REQUIRE(session.undo());
    CHECK(std::get<std::int64_t>(session.map()->instances.front().properties.front().value) == 3);
    CHECK_FALSE(session.set_instance_property({.value = "hero"}, {"", false}));
    CHECK(fabric::editor::MapSession::snap_position({5.0F, 5.0F}, {.grid_size = 0.0F}) ==
          fabric::core::Vec2{5.0F, 5.0F});
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map session edits layer state and prefab overrides undoably") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-layer-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, map_with_prefab()));
    REQUIRE(session.set_layer_visibility({.value = "gameplay"}, false));
    REQUIRE(session.set_layer_locked({.value = "gameplay"}, true));
    REQUIRE(session.set_layer_depth({.value = "gameplay"}, 8.0F));
    CHECK_FALSE(session.map()->layers.back().visible);
    CHECK(session.map()->layers.back().locked);
    CHECK(session.map()->layers.back().depth == 8.0F);
    REQUIRE(session.set_prefab_override({.value = "hero-prefab"}, {"speed", 2.5F}));
    REQUIRE(session.map()->prefabs.front().overrides.size() == 1);
    CHECK(std::get<float>(session.map()->prefabs.front().overrides.front().value) == 2.5F);
    REQUIRE(session.undo());
    CHECK(session.map()->prefabs.front().overrides.empty());
    REQUIRE(session.undo());
    CHECK(session.map()->layers.back().depth == 2.0F);
    CHECK_FALSE(session.set_layer_depth({.value = "gameplay"}, std::nanf("")));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
