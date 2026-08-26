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
    CHECK(entered.front().actor_id == "character");
    CHECK(entered.front().kind == runtime::GameplayEventKind::entered);
    REQUIRE(entered.front().payload.size() == 1);
    CHECK(std::get<std::string>(entered.front().payload.front().value) == "blue");
    CHECK(triggers.update({2.0F, 0.0F}).empty());
    CHECK(triggers.active_count() == 1);
    const auto exited = triggers.update({0.0F, 0.0F});
    REQUIRE(exited.size() == 1);
    CHECK(exited.front().kind == runtime::GameplayEventKind::exited);
    CHECK(exited.front().payload == entered.front().payload);
    CHECK(triggers.active_count() == 0);
    REQUIRE(triggers.update({2.0F, 0.0F}).size() == 1);
}

TEST_CASE("trigger runtime tracks actor bounds independently and merges payload") {
    project::MapDocument map;
    map.collisions = {{project::CollisionShapeKind::circle, "triggers", true,
                       {2.0F, 0.0F}, 1.0F, 0.0F, {}}};
    map.events = {{{.value = "door-open"},
                   {{"key", std::string{"global"}},
                    {"difficulty", std::int64_t{2}}}}};
    map.triggers = {{"door", "triggers", 0, {.value = "door-open"},
                     {{"key", std::string{"blue"}},
                      {"one-shot", true}}}};
    runtime::TriggerRuntime triggers(map);

    const std::vector<runtime::TriggerActor> actors{
        {"monster", {{0.9F, -0.25F}, {0.5F, 0.5F}}},
        {"player", {{1.5F, -0.25F}, {0.5F, 0.5F}}}};
    const auto entered = triggers.update(actors);
    REQUIRE(entered.size() == 2U);
    CHECK(entered[0].actor_id == "monster");
    CHECK(entered[1].actor_id == "player");
    CHECK(triggers.active_count() == 2U);
    REQUIRE(entered[0].payload.size() == 3U);
    CHECK(std::get<std::string>(entered[0].payload[0].value) == "blue");
    CHECK(std::get<std::int64_t>(entered[0].payload[1].value) == 2);
    CHECK(std::get<bool>(entered[0].payload[2].value));

    const auto one_left = triggers.update({actors.front()});
    REQUIRE(one_left.size() == 1U);
    CHECK(one_left.front().actor_id == "player");
    CHECK(one_left.front().kind == runtime::GameplayEventKind::exited);
    CHECK(triggers.active_count() == 1U);
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
    const auto moved = triggers.update({10.0F, 0.5F});
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].kind == runtime::GameplayEventKind::exited);
    CHECK(moved[1].kind == runtime::GameplayEventKind::entered);
    const auto exited = triggers.update({20.0F, 20.0F});
    REQUIRE(exited.size() == 1);
    CHECK(exited.front().kind == runtime::GameplayEventKind::exited);
}
