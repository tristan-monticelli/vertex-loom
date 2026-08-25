#include "fabric/editor/map_session.hpp"

#include <chrono>
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

} // namespace
