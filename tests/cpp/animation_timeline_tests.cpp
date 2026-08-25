#include "fabric/editor/animation_timeline.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::AnimationClip clip() {
    return {.document = {.schema_version = 1,
                         .type = "animation",
                         .id = {.value = "timeline"},
                         .name = "Timeline"},
            .duration = 2.0F,
            .loop = false};
}

TEST_CASE("animation timeline edits are undoable and redoable") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{"root", "transform", "opacity"};

    REQUIRE(timeline.insert_key(binding, 0.0F, 0.0F,
                               fabric::project::AnimationInterpolation::linear));
    REQUIRE(timeline.insert_key(binding, 2.0F, 1.0F,
                               fabric::project::AnimationInterpolation::linear));
    REQUIRE(source.tracks.size() == 1);
    REQUIRE(source.tracks.front().keys.size() == 2);
    REQUIRE(commands.can_undo());

    REQUIRE(timeline.move_key(binding, 1, 1.0F));
    REQUIRE(source.tracks.front().keys.back().time == 1.0F);
    REQUIRE(commands.undo());
    REQUIRE(source.tracks.front().keys.back().time == 2.0F);
    REQUIRE(commands.redo());
    REQUIRE(source.tracks.front().keys.back().time == 1.0F);

    REQUIRE(timeline.remove_key(binding, 1));
    REQUIRE(source.tracks.front().keys.size() == 1);
    REQUIRE(commands.undo());
    REQUIRE(source.tracks.front().keys.size() == 2);
}

TEST_CASE("animation timeline rejects invalid key edits") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{"root", "state", "visible"};

    REQUIRE(timeline.insert_key(binding, 0.0F, false,
                               fabric::project::AnimationInterpolation::step));
    REQUIRE_FALSE(timeline.insert_key(binding, 1.0F, 1.0F,
                                      fabric::project::AnimationInterpolation::step));
    REQUIRE_FALSE(timeline.move_key(binding, 0, 3.0F));
    REQUIRE(source.tracks.front().keys.size() == 1);
}

} // namespace
