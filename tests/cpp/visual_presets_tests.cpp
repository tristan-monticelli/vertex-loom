#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/map_session.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/visual_presets.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/runtime/preview_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

void write_thread_png(const std::filesystem::path& path) {
    constexpr std::array<unsigned char, 79> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00,
        0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72,
        0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41,
        0x54, 0x78, 0xda, 0x63, 0x64, 0x60, 0xf8, 0xff, 0x9f, 0x81,
        0x81, 0x81, 0x81, 0x89, 0x01, 0x0a, 0x00, 0x1e, 0x04, 0x02,
        0x01, 0x06, 0xca, 0xf1, 0x64, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
}

void create_studio_preset_fixture(const std::filesystem::path& root) {
    fabric::editor::ProjectSession assets;
    REQUIRE(assets.create(root, {
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "studio-preset-gallery"},
        .name = "Studio Preset Gallery"}));

    auto source = temporary_root("fabric-studio-thread");
    source += ".png";
    write_thread_png(source);
    REQUIRE(assets.import_png(source, {.value = "cotton-thread"},
                              "Cotton Thread"));
    std::error_code ignored;
    std::filesystem::remove(source, ignored);

    const std::array presets{
        std::pair{fabric::editor::VisualPresetKind::eye, "preset-eye"},
        std::pair{fabric::editor::VisualPresetKind::button, "preset-button"},
        std::pair{fabric::editor::VisualPresetKind::seam, "preset-seam"},
        std::pair{fabric::editor::VisualPresetKind::zipper, "preset-zipper"}};
    for (const auto [kind, id] : presets) {
        REQUIRE(assets.create_visual_preset({
            .kind = kind,
            .id = {.value = id},
            .name = std::string{fabric::editor::label(kind)},
            .thread_texture = fabric::project::ResourceReference{
                {.value = "cotton-thread"}, "texture"},
            .zipper_tooth_count = 10U}));
        fabric::editor::CreateEntityPrompt entity;
        entity.name = std::string{id} + " entity";
        entity.node_name = "Visual";
        entity.drawable =
            fabric::project::EntityDrawableKind::visual_component;
        entity.resource_id = id;
        REQUIRE(assets.create_entity(entity));
    }

    fabric::editor::MapSession map;
    REQUIRE(map.create(root, {
        .document = {.schema_version =
                         fabric::project::current_map_schema_version,
                     .type = "map",
                     .id = {.value = "preset-gallery"},
                     .name = "Preset Gallery"},
        .layers = {{"instances", "Instances",
                    fabric::project::MapLayerKind::instances,
                    true, false, 0.0F}}}));
    const std::array<float, 4> positions{-6.0F, -2.0F, 2.0F, 6.0F};
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto id = std::string{presets[index].second};
        REQUIRE(map.place_instance({
            .id = id,
            .entity = fabric::project::ResourceReference{
                {.value = id + "-entity"}, "entity"},
            .layer_id = "instances",
            .transform = {.position = {positions[index], 0.0F}}},
            {.enabled = false}));
    }
    REQUIRE(map.save());
}

std::vector<std::filesystem::path> fixture_files(
    const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file())
            result.push_back(entry.path().lexically_relative(root));
    }
    std::ranges::sort(result);
    return result;
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
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

TEST_CASE("versioned preset gallery is generated by Studio and loads in runtime") {
    const auto fixture = std::filesystem::path{FABRIC_SOURCE_DIR} /
        "tests/fixtures/studio-preset-gallery";
    if (std::getenv("FABRIC_UPDATE_STUDIO_PRESET_FIXTURE") != nullptr) {
        REQUIRE_FALSE(std::filesystem::exists(fixture));
        create_studio_preset_fixture(fixture);
    }
    REQUIRE(std::filesystem::is_directory(fixture));

    const auto regenerated = temporary_root("fabric-regenerated-presets");
    create_studio_preset_fixture(regenerated);
    const auto expected_files = fixture_files(fixture);
    const auto regenerated_files = fixture_files(regenerated);
    REQUIRE(regenerated_files == expected_files);
    for (const auto& relative : expected_files) {
        CHECK(read_binary(regenerated / relative) ==
              read_binary(fixture / relative));
    }
    REQUIRE(fabric::project::validate_project(fixture).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = fixture,
                          .map_id = {.value = "preset-gallery"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.map()->instances.size() == 4U);
    REQUIRE(runtime.run());
    CHECK(runtime.last_frame_packets().size() == 24U);

    std::error_code ignored;
    std::filesystem::remove_all(regenerated, ignored);
}
