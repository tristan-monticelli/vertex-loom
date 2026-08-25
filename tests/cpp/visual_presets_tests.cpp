#include "fabric/editor/visual_presets.hpp"
#include "fabric/project/texture_asset.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "visual-preset-tests"},
            .name = "Visual Preset Tests"};
}

fabric::editor::VisualPresetRequest request(
    const fabric::editor::VisualPresetKind kind,
    const std::string& id) {
    return {.kind = kind,
            .id = {.value = id},
            .name = std::string{fabric::editor::label(kind)},
            .thread_texture = fabric::project::ResourceReference{
                {.value = "cotton-thread"}, "texture"}};
}

std::filesystem::path temporary_root(const std::string& prefix) {
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

void publish_thread_texture(const std::filesystem::path& root) {
    const auto input = root / "thread.png";
    std::ofstream{input, std::ios::binary} << "thread-source";
    REQUIRE(fabric::project::publish_texture_asset(
        root, manifest(),
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "cotton-thread"},
                      .name = "Cotton Thread"},
         .source = "assets/textures/cotton-thread.png",
         .width = 8U,
         .height = 8U},
        input).ok());
}

} // namespace

TEST_CASE("eye and button presets are native parametric components") {
    const auto eye = fabric::editor::build_visual_preset(
        manifest(), request(fabric::editor::VisualPresetKind::eye,
                            "round-eye"));
    REQUIRE(eye.ok());
    REQUIRE(eye.bundle->vectors.size() == 1U);
    REQUIRE(eye.bundle->vectors.front().native.has_value());
    CHECK(eye.bundle->vectors.front().native->nodes.size() == 4U);
    CHECK(eye.bundle->textured_paths.empty());
    CHECK(eye.bundle->component.variants.size() == 2U);
    CHECK(eye.bundle->composition.layers.size() == 1U);

    const auto button = fabric::editor::build_visual_preset(
        manifest(), request(fabric::editor::VisualPresetKind::button,
                            "four-hole-button"));
    REQUIRE(button.ok());
    REQUIRE(button.bundle->vectors.front().native.has_value());
    CHECK(button.bundle->vectors.front().native->nodes.size() == 5U);
    CHECK(button.bundle->component.parameters.size() == 3U);
}

TEST_CASE("seam preset exposes a textured path without renderer specialization") {
    const auto seam_request = request(
        fabric::editor::VisualPresetKind::seam, "curved-seam");
    const auto first = fabric::editor::build_visual_preset(
        manifest(), seam_request);
    const auto second = fabric::editor::build_visual_preset(
        manifest(), seam_request);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(*first.bundle == *second.bundle);
    REQUIRE(first.bundle->textured_paths.size() == 1U);
    CHECK(first.bundle->composition.layers.front().kind ==
          fabric::project::VisualLayerKind::textured_path);
    CHECK(first.bundle->component.parameters.size() == 5U);
}

TEST_CASE("zipper composes two rails repeated teeth and one slider") {
    auto zipper_request = request(
        fabric::editor::VisualPresetKind::zipper, "coat-zipper");
    zipper_request.zipper_tooth_count = 10U;
    const auto zipper = fabric::editor::build_visual_preset(
        manifest(), zipper_request);
    REQUIRE(zipper.ok());
    CHECK(zipper.bundle->textured_paths.size() == 2U);
    CHECK(zipper.bundle->vectors.size() == 2U);
    REQUIRE(zipper.bundle->composition.layers.size() == 13U);
    CHECK(zipper.bundle->composition.layers[0].id == "left-rail");
    CHECK(zipper.bundle->composition.layers[1].id == "right-rail");
    CHECK(zipper.bundle->composition.layers.back().id == "slider");
    CHECK(std::ranges::count_if(
              zipper.bundle->composition.layers, [](const auto& layer) {
                  return layer.resource.id.value == "coat-zipper-tooth";
              }) == 10);
    CHECK(std::ranges::any_of(
        zipper.bundle->component.parameters, [](const auto& parameter) {
            return parameter.id == "slider-position" && parameter.animatable;
        }));
}

TEST_CASE("thread presets reject missing inputs and excessive teeth") {
    auto missing = request(fabric::editor::VisualPresetKind::seam, "seam");
    missing.thread_texture.reset();
    CHECK_FALSE(fabric::editor::build_visual_preset(
                    manifest(), missing).ok());

    auto excessive = request(
        fabric::editor::VisualPresetKind::zipper, "zipper");
    excessive.zipper_tooth_count = 129U;
    CHECK_FALSE(fabric::editor::build_visual_preset(
                    manifest(), excessive).ok());
}

TEST_CASE("preset publication creates a headless-valid resource graph") {
    const auto root = temporary_root("fabric-visual-presets");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    publish_thread_texture(root);
    for (const auto [kind, id] : {
             std::pair{fabric::editor::VisualPresetKind::eye, "preset-eye"},
             std::pair{fabric::editor::VisualPresetKind::button, "preset-button"},
             std::pair{fabric::editor::VisualPresetKind::seam, "preset-seam"},
             std::pair{fabric::editor::VisualPresetKind::zipper, "preset-zipper"}}) {
        const auto published = fabric::editor::publish_visual_preset(
            root, manifest(), request(kind, id));
        REQUIRE(published.ok());
    }
    CHECK(fabric::project::validate_project(root).ok());

    const auto duplicate = fabric::editor::publish_visual_preset(
        root, manifest(),
        request(fabric::editor::VisualPresetKind::eye, "preset-eye"));
    CHECK_FALSE(duplicate.ok());
    CHECK(std::ranges::any_of(duplicate.errors, [](const auto& error) {
        return error.code ==
            fabric::project::ErrorCode::asset_already_exists;
    }));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
