#include "fabric/runtime/replay_player.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ReplayDocument replay() {
    return {.document = {.schema_version = 1, .type = "replay",
                         .id = {.value = "run"}, .name = "Run"},
            .build = "test-build", .seed = 7,
            .inputs = {{2, "move_right", true, false},
                       {4, "move_right", false, true}},
            .events = {{3, "door-open", ""}},
            .checkpoints = {{3, {{"player", 4096, 0, 0}}}}};
}

TEST_CASE("replay player applies actions, events and checkpoints by frame") {
    const auto source = replay();
    fabric::runtime::ReplayPlayer player(source);
    fabric::runtime::InputActionMap input;

    REQUIRE(player.advance(1, input));
    CHECK_FALSE(input.held("move_right"));
    REQUIRE(player.advance(2, input));
    CHECK(input.held("move_right"));
    CHECK(input.pressed("move_right"));
    REQUIRE(player.advance(3, input));
    CHECK(input.held("move_right"));
    REQUIRE(player.events().size() == 1);
    CHECK(player.events().front().name == "door-open");
    REQUIRE(player.checkpoint().has_value());
    CHECK(player.checkpoint()->frame == 3);
    REQUIRE(player.advance(4, input));
    CHECK_FALSE(input.held("move_right"));
    CHECK(input.released("move_right"));
    CHECK_FALSE(player.advance(3, input));
}

} // namespace
