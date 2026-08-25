#include "fabric/runtime/trigger_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fabric;

TEST_CASE("trigger runtime emits an enter event once and resolves payload") {
    project::MapDocument map;
    map.collisions = {{project::CollisionShapeKind::circle, "triggers", true,
                       {2.0F, 0.0F}, 1.0F, 0.0F, {}}};
    map.events = {{{.value = "door-open"}, {{"key", std::string{"blue"}}}}};
    map.triggers = {{"door", "triggers", 0, {.value = "door-open"}, {}}};
    runtime::TriggerRuntime triggers(map);

    CHECK(triggers.update({0.0F, 0.0F}).empty());
    const auto entered = triggers.update({2.0F, 0.0F});
    REQUIRE(entered.size() == 1);
    CHECK(entered.front().id.value == "door-open");
    CHECK(entered.front().trigger_id == "door");
    REQUIRE(entered.front().payload.size() == 1);
    CHECK(std::get<std::string>(entered.front().payload.front().value) == "blue");
    CHECK(triggers.update({2.0F, 0.0F}).empty());
    CHECK(triggers.active_count() == 1);
    CHECK(triggers.update({0.0F, 0.0F}).empty());
    CHECK(triggers.active_count() == 0);
    REQUIRE(triggers.update({2.0F, 0.0F}).size() == 1);
}

TEST_CASE("trigger runtime supports polygon and capsule zones") {
    project::MapDocument map;
    map.collisions = {
        {project::CollisionShapeKind::polygon, "triggers", true, {}, 0.0F, 0.0F,
         {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {0.0F, 4.0F}}},
        {project::CollisionShapeKind::capsule, "triggers", true, {10.0F, 0.0F}, 1.0F, 4.0F, {}}};
    map.events = {{{.value = "polygon"}, {}}, {{.value = "capsule"}, {}}};
    map.triggers = {{"polygon-trigger", "triggers", 0, {.value = "polygon"}, {}},
                    {"capsule-trigger", "triggers", 1, {.value = "capsule"}, {}}};
    runtime::TriggerRuntime triggers(map);

    REQUIRE(triggers.update({2.0F, 2.0F}).size() == 1);
    CHECK(triggers.update({10.0F, 0.5F}).size() == 1);
    CHECK(triggers.update({20.0F, 20.0F}).empty());
}
