#include "fabric/editor/animation_timeline.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::AnimationClip clip() {
    return {.document = {
                         .schema_version =
                             fabric::project::current_animation_schema_version,
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

TEST_CASE("animation timeline scales a key selection atomically around a pivot") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{
        "root", "transform", "opacity"};
    REQUIRE(timeline.insert_key(
        binding, 0.5F, 0.0F,
        fabric::project::AnimationInterpolation::linear));
    REQUIRE(timeline.insert_key(
        binding, 1.5F, 1.0F,
        fabric::project::AnimationInterpolation::linear));
    const std::vector selection{
        fabric::editor::AnimationKeySelection{binding, 0U},
        fabric::editor::AnimationKeySelection{binding, 1U}};

    REQUIRE(timeline.scale_keys(selection, 1.0F, 0.5F));
    CHECK(source.tracks.front().keys[0].time == 0.75F);
    CHECK(source.tracks.front().keys[1].time == 1.25F);
    REQUIRE(commands.undo());
    CHECK(source.tracks.front().keys[0].time == 0.5F);
    CHECK(source.tracks.front().keys[1].time == 1.5F);
    REQUIRE(commands.redo());
    CHECK(source.tracks.front().keys[0].time == 0.75F);
    CHECK(source.tracks.front().keys[1].time == 1.25F);

    const auto before = source;
    CHECK_FALSE(timeline.scale_keys(selection, 0.0F, 2.0F));
    CHECK(source == before);
    CHECK_FALSE(timeline.scale_keys(selection, 1.0F, -1.0F));
    CHECK(source == before);
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

TEST_CASE("animation timeline preserves composition and rejects mismatches") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{"root", "transform", "position"};

    REQUIRE(timeline.set_key(
        binding, 0.0F, fabric::core::Vec2{1.0F, 2.0F},
        fabric::project::AnimationInterpolation::linear,
        fabric::project::AnimationComposition::additive));
    CHECK(source.tracks.front().composition ==
          fabric::project::AnimationComposition::additive);
    REQUIRE_FALSE(timeline.set_key(
        binding, 0.5F, fabric::core::Vec2{2.0F, 3.0F},
        fabric::project::AnimationInterpolation::linear,
        fabric::project::AnimationComposition::replace));
    REQUIRE(timeline.set_key(
        binding, 0.0F, fabric::core::Vec2{4.0F, 5.0F},
        fabric::project::AnimationInterpolation::linear,
        fabric::project::AnimationComposition::additive));
    CHECK(source.tracks.front().keys.size() == 1U);
    CHECK(std::get<fabric::core::Vec2>(source.tracks.front().keys.front().value) ==
          fabric::core::Vec2{4.0F, 5.0F});
}

TEST_CASE("animation timeline creates an undoable A to B segment") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{"root", "transform", "opacity"};

    REQUIRE(timeline.set_segment(
        binding, 0.25F, 0.0F, 1.5F, 1.0F,
        fabric::project::AnimationInterpolation::linear));
    REQUIRE(source.tracks.size() == 1U);
    REQUIRE(source.tracks.front().keys.size() == 2U);
    CHECK(source.tracks.front().keys.front().time == 0.25F);
    CHECK(source.tracks.front().keys.back().time == 1.5F);
    CHECK(commands.size() == 1U);
    REQUIRE(commands.undo());
    CHECK(source.tracks.empty());
    REQUIRE(commands.redo());
    CHECK(source.tracks.front().keys.size() == 2U);
    CHECK_FALSE(timeline.set_segment(
        binding, 1.0F, 0.0F, 1.0F, 1.0F,
        fabric::project::AnimationInterpolation::linear));
}

TEST_CASE("animation timeline curve settings are immediate and undoable") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{
        "root", "transform", "position"};

    REQUIRE(timeline.set_key(
        binding, 0.0F, fabric::core::Vec2{},
        fabric::project::AnimationInterpolation::linear));
    REQUIRE(timeline.set_track_curve(
        binding, fabric::project::AnimationInterpolation::cubic,
        fabric::project::AnimationEasing::ease_in_out));
    CHECK(source.tracks.front().interpolation ==
          fabric::project::AnimationInterpolation::cubic);
    CHECK(source.tracks.front().easing ==
          fabric::project::AnimationEasing::ease_in_out);
    REQUIRE(commands.undo());
    CHECK(source.tracks.front().interpolation ==
          fabric::project::AnimationInterpolation::linear);
    CHECK(source.tracks.front().easing ==
          fabric::project::AnimationEasing::linear);
}

TEST_CASE("animation canvas auto-key updates merge into one undo step") {
    auto source = clip();
    fabric::editor::CommandStack commands;
    fabric::editor::AnimationTimeline timeline(source, commands);
    const fabric::project::PropertyBinding binding{
        "root", "transform", "position"};

    REQUIRE(timeline.set_key(
        binding, 0.5F, fabric::core::Vec2{1.0F, 2.0F},
        fabric::project::AnimationInterpolation::linear,
        fabric::project::AnimationComposition::replace,
        fabric::project::AnimationEasing::linear, {}, {}, true));
    const auto history = commands.size();
    REQUIRE(timeline.set_key(
        binding, 0.5F, fabric::core::Vec2{4.0F, 6.0F},
        fabric::project::AnimationInterpolation::linear,
        fabric::project::AnimationComposition::replace,
        fabric::project::AnimationEasing::linear, {}, {}, true));
    CHECK(commands.size() == history);
    CHECK(std::get<fabric::core::Vec2>(
              source.tracks.front().keys.front().value) ==
          fabric::core::Vec2{4.0F, 6.0F});
    REQUIRE(commands.undo());
    CHECK(source.tracks.empty());
}

} // namespace
