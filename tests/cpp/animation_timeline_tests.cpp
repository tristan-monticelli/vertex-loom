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
    const auto history_after_first_move = commands.size();
    REQUIRE(timeline.move_key(binding, 1, 1.5F));
    REQUIRE(commands.size() == history_after_first_move);
    REQUIRE(source.tracks.front().keys.back().time == 1.5F);
    REQUIRE(commands.undo());
    REQUIRE(source.tracks.front().keys.back().time == 2.0F);
    REQUIRE(commands.redo());
    REQUIRE(source.tracks.front().keys.back().time == 1.5F);

    REQUIRE(timeline.remove_key(binding, 1));
    REQUIRE(source.tracks.front().keys.size() == 1);
    REQUIRE(commands.undo());
    REQUIRE(source.tracks.front().keys.size() == 2);
}

TEST_CASE("animation timeline lists registered animatable properties for a node") {
    fabric::project::PropertyDescriptorRegistry registry;
    REQUIRE(registry.register_descriptor({
        .component_id = "transform", .property_id = "position",
        .display_path = "Transform/Position",
        .value_kind = fabric::project::PropertyValueKind::vec2}).ok());
    REQUIRE(registry.register_descriptor({
        .component_id = "transform", .property_id = "rotation",
        .display_path = "Transform/Rotation",
        .value_kind = fabric::project::PropertyValueKind::angle,
        .readable = true, .writable = true, .animatable = false}).ok());
    REQUIRE(registry.register_descriptor({
        .component_id = "material", .property_id = "opacity",
        .display_path = "Material/Opacity",
        .value_kind = fabric::project::PropertyValueKind::scalar}).ok());

    const auto bindings = fabric::editor::AnimationTimeline::animatable_bindings("root", registry);
    REQUIRE(bindings.size() == 2);
    CHECK(bindings[0] == fabric::project::PropertyBinding{"root", "transform", "position"});
    CHECK(bindings[1] == fabric::project::PropertyBinding{"root", "material", "opacity"});
    CHECK(fabric::editor::AnimationTimeline::animatable_bindings("", registry).empty());
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
