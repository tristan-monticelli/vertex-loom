#include "fabric/project/animation.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "animation-test"},
            .name = "Animation Test",
            .directories = {}};
}

fabric::project::AnimationClip clip() {
    using namespace fabric::project;
    return {
        .document = {.schema_version = current_animation_schema_version,
                     .type = "animation",
                     .id = {.value = "idle"},
                     .name = "Idle"},
        .duration = 1.0F,
        .loop = true,
        .markers = {{"start", 0.0F}, {"end", 1.0F}},
        .tracks = {
            {.binding = {"root", "transform", "opacity"},
             .interpolation = AnimationInterpolation::linear,
             .keys = {{0.0F, 0.0F}, {1.0F, 1.0F}}},
            {.binding = {"root", "transform", "position"},
             .interpolation = AnimationInterpolation::cubic,
             .keys = {{0.0F, fabric::core::Vec2{0.0F, 0.0F}},
                      {1.0F, fabric::core::Vec2{10.0F, 20.0F}}}},
            {.binding = {"root", "state", "visible"},
             .interpolation = AnimationInterpolation::step,
             .keys = {{0.0F, false}, {1.0F, true}}},
            {.binding = {"root", "sprite", "frame"},
             .interpolation = AnimationInterpolation::step,
             .keys = {{0.0F, ResourceReference{{.value = "hero"}, "texture"}}}},
        },
    };
}

TEST_CASE("animation v1 migrates to an explicit generic v2 clip") {
    auto source = clip();
    source.document.schema_version = 1;
    auto serialized = fabric::project::serialize_animation(source);
    const auto preview = serialized.find("  \"previewEntity\"");
    REQUIRE(preview != std::string::npos);
    const auto line_end = serialized.find('\n', preview);
    serialized.erase(preview, line_end - preview + 1U);
    const auto parsed = fabric::project::parse_animation(manifest(), serialized);
    REQUIRE(parsed.ok());
    CHECK(parsed.asset->document.schema_version ==
          fabric::project::current_animation_schema_version);
    CHECK_FALSE(parsed.asset->preview_entity.has_value());
}

TEST_CASE("animation clips round-trip and publish atomically") {
    const auto source = clip();
    const auto parsed = fabric::project::parse_animation(
        manifest(), fabric::project::serialize_animation(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.asset == source);

    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-animation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "assets");
    const auto published = fabric::project::publish_animation(root, manifest(), source);
    REQUIRE(published.ok());
    REQUIRE(std::filesystem::is_regular_file(
        root / "assets/animations/idle.animation.json"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("animation dependencies exclude the authoring preview entity") {
    auto source = clip();
    source.preview_entity = fabric::project::ResourceReference{
        {.value = "animated-entity"}, "entity"};

    const auto references =
        fabric::project::animation_resource_references(source);

    REQUIRE(references.size() == 1U);
    CHECK(references.front() == fabric::project::ResourceReference{
        {.value = "hero"}, "texture"});
}

TEST_CASE("animation evaluation interpolates supported values and loops") {
    const auto source = clip();
    const auto evaluated = fabric::project::evaluate_animation(source, 0.5F);
    REQUIRE(evaluated.ok());
    REQUIRE(evaluated.properties.size() == 4);
    REQUIRE(std::get<float>(evaluated.properties[0].value) == 0.5F);
    REQUIRE(std::get<fabric::core::Vec2>(evaluated.properties[1].value).x == 5.0F);
    REQUIRE(std::get<bool>(evaluated.properties[2].value) == false);
    REQUIRE(std::get<fabric::project::ResourceReference>(
                evaluated.properties[3].value).id.value == "hero");

    const auto looped = fabric::project::evaluate_animation(source, 1.25F);
    REQUIRE(std::get<float>(looped.properties[0].value) == 0.25F);
}

TEST_CASE("animation validation rejects incompatible interpolated values") {
    auto invalid = clip();
    invalid.tracks[0].keys[1].value = fabric::core::Vec2{1.0F, 1.0F};
    REQUIRE_FALSE(fabric::project::validate_animation(manifest(), invalid).ok());
}

TEST_CASE("animation composition round-trips and is exposed during evaluation") {
    auto source = clip();
    source.tracks.front().composition =
        fabric::project::AnimationComposition::additive;
    const auto serialized = fabric::project::serialize_animation(source);
    const auto parsed = fabric::project::parse_animation(manifest(), serialized);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.asset->tracks.front().composition ==
            fabric::project::AnimationComposition::additive);

    const auto evaluated = fabric::project::evaluate_animation(*parsed.asset, 0.5F);
    REQUIRE(evaluated.ok());
    REQUIRE(evaluated.properties.front().composition ==
            fabric::project::AnimationComposition::additive);
    CHECK(std::get<float>(evaluated.properties.front().value) == 0.5F);
}

TEST_CASE("animation easing and tangents round-trip and affect cubic evaluation") {
    auto source = clip();
    source.tracks[0].easing = fabric::project::AnimationEasing::ease_in;
    auto& track = source.tracks[1];
    track.easing = fabric::project::AnimationEasing::linear;
    track.keys[0].out_tangent = fabric::core::Vec2{10.0F, 0.0F};
    track.keys[1].in_tangent = fabric::core::Vec2{0.0F, 20.0F};

    const auto serialized = fabric::project::serialize_animation(source);
    REQUIRE(serialized.find("inTangent") != std::string::npos);
    REQUIRE(serialized.find("outTangent") != std::string::npos);
    const auto parsed = fabric::project::parse_animation(manifest(), serialized);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.asset->tracks[1] == track);

    const auto evaluated = fabric::project::evaluate_animation(*parsed.asset, 0.5F);
    REQUIRE(evaluated.ok());
    const auto position = std::get<fabric::core::Vec2>(evaluated.properties[1].value);
    CHECK(std::get<float>(evaluated.properties[0].value) == 0.25F);
    CHECK(position.x == 6.25F);
    CHECK(position.y == 7.5F);
}

TEST_CASE("animation validation rejects tangent type mismatches") {
    auto invalid = clip();
    invalid.tracks[1].keys.front().out_tangent = 1.0F;
    const auto report = fabric::project::validate_animation(manifest(), invalid);
    REQUIRE_FALSE(report.ok());
    CHECK(std::ranges::any_of(report.errors, [](const auto& error) {
        return error.field == "tracks.keys.outTangent";
    }));
}

TEST_CASE("rotation interpolation follows the shortest angular path") {
    auto source = clip();
    source.tracks.push_back({
        {.node_id = "root", .component_id = "transform", .property_id = "rotationDegrees"},
        fabric::project::AnimationInterpolation::linear,
        {{0.0F, 350.0F}, {1.0F, 10.0F}}});
    const auto evaluated = fabric::project::evaluate_animation(source, 0.5F);
    REQUIRE(evaluated.ok());
    const auto& angle = evaluated.properties.back().value;
    REQUIRE(std::holds_alternative<float>(angle));
    CHECK(std::get<float>(angle) == 360.0F);

    source.tracks.back().keys = {{0.0F, 10.0F}, {1.0F, 350.0F}};
    const auto reverse = fabric::project::evaluate_animation(source, 0.5F);
    REQUIRE(reverse.ok());
    CHECK(std::get<float>(reverse.properties.back().value) == 0.0F);
}

TEST_CASE("animation marker hits are ordered across loop boundaries") {
    auto source = clip();
    source.markers = {{"foot", 0.25F}, {"turn", 0.75F}};
    const auto one_pass = fabric::project::animation_markers_between(source, 0.2F, 0.8F);
    REQUIRE(one_pass.size() == 2U);
    CHECK(one_pass[0] == fabric::project::AnimationMarkerHit{"foot", 0.25F, 0.25F, 0});
    CHECK(one_pass[1] == fabric::project::AnimationMarkerHit{"turn", 0.75F, 0.75F, 0});

    const auto looped = fabric::project::animation_markers_between(source, 0.6F, 1.4F);
    REQUIRE(looped.size() == 2U);
    CHECK(looped[0] == fabric::project::AnimationMarkerHit{"turn", 0.75F, 0.75F, 0});
    CHECK(looped[1] == fabric::project::AnimationMarkerHit{"foot", 1.25F, 0.25F, 1});

    source.loop = false;
    const auto clamped = fabric::project::animation_markers_between(source, -1.0F, 2.0F);
    REQUIRE(clamped.size() == 2U);
    CHECK(clamped.front().id == "foot");
    CHECK(clamped.back().id == "turn");
}

} // namespace
