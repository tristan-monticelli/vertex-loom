#include "fabric/project/texture_asset.hpp"
#include "fabric/project/visual_composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "composition-tests"},
            .name = "Composition Tests"};
}

fabric::project::VisualComposition composition() {
    return {
        .document = {.schema_version = 1,
                     .type = "visualComposition",
                     .id = {.value = "textile-head"},
                     .name = "Textile Head"},
        .size = {4.0F, 3.0F},
        .layers = {
            {.id = "face",
             .name = "Face",
             .kind = fabric::project::VisualLayerKind::raster,
             .resource = {{.value = "face-source"}, "texture"},
             .anchor = {0.5F, 0.5F},
             .transform = {.position = {0.25F, -0.5F}},
             .opacity = 0.9F,
             .z_order = -1.0F,
             .raster_view = fabric::project::RasterView{
                 .crop = {{2.0F, 1.0F}, {12.0F, 10.0F}},
                 .pivot = {0.5F, 0.4F},
                 .filter = fabric::project::RasterFilter::nearest}},
            {.id = "outline",
             .name = "Outline",
             .kind = fabric::project::VisualLayerKind::vector,
             .resource = {{.value = "face-outline"}, "vector"},
             .z_order = 1.0F},
            {.id = "eyes",
             .name = "Eyes",
             .kind = fabric::project::VisualLayerKind::component,
             .resource = {{.value = "button-eyes"}, "visualComponent"},
             .z_order = 2.0F},
            {.id = "seam",
             .name = "Seam",
             .kind = fabric::project::VisualLayerKind::textured_path,
             .resource = {{.value = "face-seam"}, "texturedPath"},
             .z_order = 3.0F},
        },
    };
}

std::filesystem::path temporary_root(const std::string& prefix) {
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("visual composition v1 round trips every layer kind") {
    const auto source = composition();
    const auto parsed = fabric::project::parse_visual_composition(
        manifest(), fabric::project::serialize_visual_composition(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);
    const auto references =
        fabric::project::visual_composition_resource_references(source);
    REQUIRE(references.size() == 4U);
    CHECK(references[0].expected_type == "texture");
    CHECK(references[1].expected_type == "vector");
    CHECK(references[2].expected_type == "visualComponent");
    CHECK(references[3].expected_type == "texturedPath");
}

TEST_CASE("visual composition parser and validator reject malformed layers") {
    const auto unknown = fabric::project::parse_visual_composition(
        manifest(),
        R"({"schemaVersion":1,"type":"visualComposition","id":"bad","name":"Bad","size":{"x":1,"y":1},"layers":[],"surprise":true})");
    CHECK_FALSE(unknown.ok());

    auto invalid = composition();
    invalid.layers[1].id = invalid.layers[0].id;
    invalid.layers[1].resource.expected_type = "texture";
    invalid.layers[1].raster_view = fabric::project::RasterView{
        .crop = {{0.0F, 0.0F}, {1.0F, 1.0F}}};
    invalid.layers[2].anchor.x = 1.5F;
    invalid.layers[3].opacity = std::numeric_limits<float>::infinity();
    const auto validation = fabric::project::validate_visual_composition(
        manifest(), invalid);
    CHECK_FALSE(validation.ok());
    CHECK(validation.errors.size() >= 5U);
}

TEST_CASE("visual composition publication is atomic") {
    const auto root = temporary_root("fabric-composition-publish");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source = composition();
    source.layers.resize(1U);
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), source).ok());
    const auto path = root / fabric::project::visual_composition_document_path(
        manifest(), source.document.id);
    const auto published = read_file(path);

    source.layers.front().opacity = 2.0F;
    CHECK_FALSE(fabric::project::publish_visual_composition(
                    root, manifest(), source).ok());
    CHECK(read_file(path) == published);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("headless validation resolves composition textures and crop bounds") {
    const auto root = temporary_root("fabric-composition-graph");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    const auto input = root / "input.png";
    std::ofstream{input, std::ios::binary} << "source-bytes";
    const fabric::project::TextureAsset texture{
        .document = {.schema_version = 1,
                     .type = "texture",
                     .id = {.value = "face-source"},
                     .name = "Face Source"},
        .source = "assets/textures/face-source.png",
        .width = 16U,
        .height = 12U,
    };
    REQUIRE(fabric::project::publish_texture_asset(
                root, manifest(), texture, input).ok());
    auto source = composition();
    source.layers.resize(1U);
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), source).ok());
    CHECK(fabric::project::validate_project(root).ok());

    source.layers.front().raster_view->crop.size.x = 15.0F;
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), source).ok());
    CHECK_FALSE(fabric::project::validate_project(root).ok());

    source.layers.front().raster_view->crop.size.x = 12.0F;
    source.layers.front().resource.id.value = "missing-face-source";
    REQUIRE(fabric::project::publish_visual_composition(
                root, manifest(), source).ok());
    const auto missing = fabric::project::validate_project(root);
    CHECK_FALSE(missing.ok());
    CHECK(std::ranges::any_of(missing.errors, [](const auto& error) {
        return error.code == fabric::project::ErrorCode::missing_resource;
    }));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
