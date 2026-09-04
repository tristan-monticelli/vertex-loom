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
                       {"instances-secondary", "Instances Secondary",
                        fabric::project::MapLayerKind::instances, true, false, 1.0F},
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
    result.prefabs.push_back({
        .id = "hero-prefab",
        .entity = {{.value = "entity"}, "entity"}});
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
    REQUIRE(session.set_instance_layer({.value = "hero"}, {.value = "instances-secondary"}));
    CHECK(session.map()->instances.front().layer_id == "instances-secondary");
    REQUIRE(session.undo());
    REQUIRE(session.set_instances_layer({{.value = "hero"}}, {.value = "instances-secondary"}));
    CHECK(session.map()->instances.front().layer_id == "instances-secondary");
    REQUIRE(session.undo());
    REQUIRE(session.place_instance(instance("hero-two", 2.0F)));
    REQUIRE(session.remove_instances({{.value = "hero"}, {.value = "hero-two"}}));
    CHECK(session.map()->instances.empty());
    REQUIRE(session.undo());
    REQUIRE(session.remove_instance({.value = "hero-two"}));
    REQUIRE(session.declare_event({{.value = "on-enter"}, {}}));
    REQUIRE(session.add_trigger({"enter", "triggers", 0, {.value = "on-enter"}, {}}));
    REQUIRE_FALSE(session.add_trigger({"invalid-collision", "triggers", 4,
                                       {.value = "on-enter"}, {}}));
    REQUIRE_FALSE(session.remove_event({.value = "on-enter"}));
    REQUIRE(session.declare_event({{.value = "on-exit"}, {}}));
    auto trigger = session.map()->triggers.front();
    trigger.event_id = {.value = "on-exit"};
    REQUIRE(session.set_trigger(0, trigger));
    CHECK(session.map()->triggers.front().event_id.value == "on-exit");
    REQUIRE(session.undo());
    REQUIRE(session.set_event_payload({.value = "on-exit"},
                                      {{"reason", std::string{"leave"}}}));
    REQUIRE(session.map()->events.back().payload.size() == 1);
    CHECK(std::get<std::string>(session.map()->events.back().payload.front().value) == "leave");
    REQUIRE(session.undo());
    REQUIRE_FALSE(session.set_event_payload({.value = "on-exit"},
                                            {{"reason", false}, {"reason", true}}));
    REQUIRE(session.remove_trigger({.value = "enter"}));
    REQUIRE(session.remove_event({.value = "on-enter"}));
    REQUIRE(session.remove_event({.value = "on-exit"}));
    auto collision = session.map()->collisions.front();
    collision.center = {3.0F, 4.0F};
    collision.radius = 2.0F;
    collision.sensor = false;
    REQUIRE(session.set_collision_shape(0, collision));
    CHECK(session.map()->collisions.front().center == fabric::core::Vec2{3.0F, 4.0F});
    REQUIRE(session.undo());
    CHECK(session.map()->collisions.front().center == fabric::core::Vec2{});
    collision.kind = fabric::project::CollisionShapeKind::polygon;
    collision.points = {{-1.0F, -1.0F}, {1.0F, -1.0F}, {0.0F, 1.0F}};
    REQUIRE(session.set_collision_shape(0, collision));
    CHECK(session.map()->collisions.front().points.size() == 3);
    REQUIRE(session.undo());
    REQUIRE(session.set_layer_locked({.value = "collision"}, true));
    REQUIRE_FALSE(session.set_collision_shape(0, collision));
    REQUIRE(session.set_layer_locked({.value = "collision"}, false));
    REQUIRE(session.remove_collision_shape(0));
    CHECK(session.map()->collisions.empty());
    REQUIRE(session.undo());
    CHECK(session.map()->collisions.size() == 1);
    REQUIRE(session.save());

    fabric::editor::MapSession reopened;
    REQUIRE(reopened.open(root, {.value = "session"}));
    REQUIRE(reopened.map()->instances.front().chunk_y == -2);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map creation and selection save the previous dirty map") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-transition-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, map()));
    REQUIRE(session.place_instance(instance("first-instance", 2.0F)));

    auto second = map();
    second.document.id = {.value = "second"};
    second.document.name = "Second";
    REQUIRE(session.create(root, second));
    CHECK_FALSE(session.dirty());

    auto persisted_first = fabric::project::load_map(
        root, manifest(), "maps/session.map.json");
    REQUIRE(persisted_first.ok());
    REQUIRE(persisted_first.asset->instances.size() == 1U);
    CHECK(persisted_first.asset->instances.front().id == "first-instance");

    REQUIRE(session.place_instance(instance("second-instance", 3.0F)));
    REQUIRE(session.open(root, {.value = "session"}));
    CHECK_FALSE(session.dirty());
    CHECK(session.map()->document.id.value == "session");

    auto persisted_second = fabric::project::load_map(
        root, manifest(), "maps/second.map.json");
    REQUIRE(persisted_second.ok());
    REQUIRE(persisted_second.asset->instances.size() == 1U);
    CHECK(persisted_second.asset->instances.front().id == "second-instance");

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
    REQUIRE(session.set_instance_animation(
        {.value = "hero"},
        fabric::project::ResourceReference{{.value = "walk"}, "animation"}));
    REQUIRE(session.map()->instances.front().properties.size() == 2);
    CHECK(std::get<fabric::project::ResourceReference>(
              session.map()->instances.front().properties.back().value).id ==
          fabric::core::ResourceId{.value = "walk"});
    CHECK_FALSE(session.set_instance_animation(
        {.value = "hero"},
        fabric::project::ResourceReference{{.value = "walk"}, "entity"}));
    REQUIRE(session.set_instance_animation({.value = "hero"}, std::nullopt));
    CHECK(session.map()->instances.front().properties.size() == 1);
    REQUIRE(session.undo());
    CHECK(session.map()->instances.front().properties.size() == 2);
    CHECK_FALSE(session.set_instance_property({.value = "hero"}, {"", false}));
    CHECK(fabric::editor::MapSession::snap_position({5.0F, 5.0F}, {.grid_size = 0.0F}) ==
          fabric::core::Vec2{5.0F, 5.0F});
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map session autosaves and offers newer valid recovery") {
    using namespace std::chrono_literals;
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-recovery-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, map()));
    REQUIRE(session.place_instance(instance("hero", 3.0F)));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    CHECK(session.update_autosave(start) == fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + 2s) == fabric::editor::AutosaveStatus::saved);

    fabric::editor::MapSession reopened;
    REQUIRE(reopened.open(root, {.value = "session"}));
    REQUIRE(reopened.has_recovery());
    CHECK(reopened.map()->instances.empty());
    REQUIRE(reopened.accept_recovery(start + 3s));
    CHECK(reopened.map()->instances.size() == 1U);
    CHECK(reopened.map()->instances.front().id == "hero");
    CHECK(reopened.dirty());
    REQUIRE(reopened.save());
    CHECK_FALSE(reopened.has_recovery());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map session edits layer state and prefab overrides undoably") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-layer-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source = map_with_prefab();
    source.instances.push_back({"hero", std::nullopt,
                                fabric::project::ResourceReference{{.value = "hero-prefab"}, "prefab"},
                                "instances", {}, 0, 0, {}});
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, source));
    REQUIRE(session.set_layer_visibility({.value = "gameplay"}, false));
    REQUIRE(session.set_layer_locked({.value = "gameplay"}, true));
    REQUIRE(session.set_layer_depth({.value = "gameplay"}, 8.0F));
    CHECK_FALSE(session.map()->layers.back().visible);
    CHECK(session.map()->layers.back().locked);
    CHECK(session.map()->layers.back().depth == 8.0F);
    REQUIRE(session.set_prefab_override({.value = "hero-prefab"}, {"speed", 2.5F}));
    REQUIRE(session.map()->prefabs.front().overrides.size() == 1);
    CHECK(std::get<float>(session.map()->prefabs.front().overrides.front().value) == 2.5F);
    auto effective = session.effective_instance_properties({.value = "hero"});
    REQUIRE(effective.size() == 1);
    CHECK(effective.front().id == "speed");
    CHECK(std::get<float>(effective.front().value) == 2.5F);
    REQUIRE(session.set_instance_property({.value = "hero"}, {"speed", 3.0F}));
    effective = session.effective_instance_properties({.value = "hero"});
    REQUIRE(effective.size() == 1);
    CHECK(std::get<float>(effective.front().value) == 3.0F);
    REQUIRE(session.undo());
    CHECK(std::get<float>(session.effective_instance_properties({.value = "hero"}).front().value) == 2.5F);
    REQUIRE(session.undo());
    CHECK(session.map()->prefabs.front().overrides.empty());
    REQUIRE(session.undo());
    CHECK(session.map()->layers.back().depth == 2.0F);
    CHECK_FALSE(session.set_layer_depth({.value = "gameplay"}, std::nanf("")));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map session configures and removes a path follower undoably") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-path-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MapSession session;
    auto source = map();
    source.instances = {instance("hero", 0.0F)};
    REQUIRE(session.create(root, source));
    fabric::project::PathFollowerState follower{
        .path = {{.value = "path"}, "texturedPath"},
        .progress = 0.25F,
        .speed = 3.0F,
        .loop = false,
        .orient_to_path = true,
        .rotation_offset_degrees = 10.0F};
    REQUIRE(session.set_instance_path_follower({.value = "hero"}, follower));
    REQUIRE(session.map()->instances.front().path_follower == follower);
    REQUIRE(session.undo());
    CHECK_FALSE(session.map()->instances.front().path_follower.has_value());
    REQUIRE(session.redo());
    CHECK(session.map()->instances.front().path_follower == follower);
    REQUIRE(session.set_instance_path_follower({.value = "hero"}, std::nullopt));
    CHECK_FALSE(session.map()->instances.front().path_follower.has_value());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("map session translates a selection atomically and respects layer locks") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-map-selection-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source = map();
    source.instances = {instance("hero", 1.0F), instance("crate", 3.0F)};
    fabric::editor::MapSession session;
    REQUIRE(session.create(root, source));
    REQUIRE(session.translate_instances({{.value = "hero"}, {.value = "crate"}},
                                         {2.0F, 0.0F}, {.enabled = false}));
    CHECK(session.map()->instances[0].transform.position.x == 3.0F);
    CHECK(session.map()->instances[1].transform.position.x == 5.0F);
    REQUIRE(session.duplicate_instance({.value = "hero"}, {2.0F, 0.0F}, {.enabled = false}));
    REQUIRE(session.map()->instances.size() == 3);
    CHECK(session.map()->instances.back().id == "hero-copy");
    REQUIRE(session.reorder_instance({.value = "hero-copy"}, 0U));
    CHECK(session.map()->instances.front().id == "hero-copy");
    REQUIRE(session.undo());
    CHECK(session.map()->instances.back().id == "hero-copy");
    REQUIRE(session.undo());
    REQUIRE(session.map()->instances.size() == 2);
    REQUIRE(session.undo());
    CHECK(session.map()->instances[0].transform.position.x == 1.0F);
    REQUIRE(session.set_layer_locked({.value = "instances"}, true));
    CHECK_FALSE(session.translate_instances({{.value = "hero"}}, {1.0F, 0.0F},
                                             {.enabled = false}));
    CHECK_FALSE(session.duplicate_instance({.value = "hero"}));
    CHECK_FALSE(session.reorder_instance({.value = "hero"}, 0U));
    CHECK_FALSE(session.set_instance_property({.value = "hero"}, {"blocked", true}));
    CHECK_FALSE(session.remove_instance({.value = "hero"}));
    CHECK_FALSE(session.place_instance(instance("new", 0.0F)));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
