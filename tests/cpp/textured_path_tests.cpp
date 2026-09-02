#include "fabric/project/textured_path.hpp"
#include "fabric/project/texture_asset.hpp"

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
            .id = {.value = "textured-path-tests"},
            .name = "Textured Path Tests"};
}

fabric::project::TexturedPath textured_path() {
    using Kind = fabric::project::TexturedPathCommandKind;
    return {
        .document = {.schema_version = 1,
                     .type = "texturedPath",
                     .id = {.value = "embroidered-seam"},
                     .name = "Embroidered Seam"},
        .commands = {
            {.kind = Kind::move, .point = {-2.0F, 0.0F}},
            {.kind = Kind::line, .point = {0.0F, 1.0F}},
            {.kind = Kind::cubic,
             .point = {3.0F, 0.5F},
             .control1 = {1.0F, 2.0F},
             .control2 = {2.0F, -1.0F}},
        },
        .closed = false,
        .width = 0.25F,
        .width_profile = {{.position = 0.0F, .width = 0.15F},
                          {.position = 0.4F, .width = 0.35F},
                          {.position = 1.0F, .width = 0.2F}},
        .texture = {{.value = "yellow-thread"}, "texture"},
        .uv_mode = fabric::project::TexturedPathUvMode::repeat,
        .uv_scale = {0.5F, 2.0F},
        .uv_offset = {0.25F, -0.5F},
        .color = {0.9F, 0.8F, 0.2F, 0.75F},
        .opacity = 0.8F,
        .join = fabric::project::TexturedPathJoin::round,
        .cap = fabric::project::TexturedPathCap::square,
        .miter_limit = 3.0F,
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

bool has_field(const fabric::project::ValidationReport& report,
               const std::string_view field) {
    return std::ranges::any_of(report.errors, [&](const auto& error) {
        return error.field == field;
    });
}

} // namespace

TEST_CASE("textured path v1 round trips its authoring contract") {
    const auto source = textured_path();
    const auto serialized = fabric::project::serialize_textured_path(source);
    const auto parsed = fabric::project::parse_textured_path(
        manifest(), serialized);
    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == source);
    CHECK(serialized.find("collision") == std::string::npos);
    CHECK(serialized.find("mesh") == std::string::npos);

    const auto references =
        fabric::project::textured_path_resource_references(source);
    REQUIRE(references.size() == 1U);
    CHECK(references.front() == source.texture);
}

TEST_CASE("textured path persists mirror mapping for non-tileable friezes") {
    auto source = textured_path();
    source.uv_mode = fabric::project::TexturedPathUvMode::mirror;
    const auto serialized = fabric::project::serialize_textured_path(source);
    CHECK(serialized.find("\"uvMode\"") != std::string::npos);
    CHECK(serialized.find("mirror") != std::string::npos);
    const auto parsed = fabric::project::parse_textured_path(
        manifest(), serialized);
    REQUIRE(parsed.ok());
    CHECK(parsed.asset->uv_mode ==
          fabric::project::TexturedPathUvMode::mirror);
}

TEST_CASE("textured path persists shader profile and animated surface settings") {
    auto source = textured_path();
    source.shader.profile = fabric::project::SurfaceShaderProfile::thread;
    source.shader.classification = fabric::project::TextureClassification::beam;
    source.shader.primary_color = {0.2F, 0.4F, 0.8F, 1.0F};
    source.shader.effect_color = {1.0F, 0.2F, 0.7F, 1.0F};
    source.shader.shine = 0.75F;
    source.shader.holography = 0.35F;
    source.shader.intensity = 1.4F;
    source.shader.repetition = {3.0F, 2.0F};
    source.shader.deformation = {0.1F, -0.2F};
    source.shader.effects = {
        {.kind = fabric::project::SurfaceEffectKind::tint,
         .color = {0.2F, 0.4F, 0.8F, 1.0F}, .amount = 0.8F},
        {.kind = fabric::project::SurfaceEffectKind::holography,
         .color = {1.0F, 0.2F, 0.7F, 0.9F}, .amount = 0.35F,
         .scale = 2.0F},
        {.kind = fabric::project::SurfaceEffectKind::shine,
         .enabled = false, .color = {1.0F, 0.8F, 0.2F, 1.0F},
         .amount = 0.4F},
        {.kind = fabric::project::SurfaceEffectKind::shine,
         .color = {0.2F, 0.8F, 1.0F, 1.0F}, .amount = 0.2F},
    };
    const auto parsed = fabric::project::parse_textured_path(
        manifest(), fabric::project::serialize_textured_path(source));
    REQUIRE(parsed.ok());
    CHECK(parsed.asset->shader == source.shader);
    CHECK(parsed.asset->shader.effects.size() == 4U);
}

TEST_CASE("textured path rejects invalid modular surface effects") {
    auto source = textured_path();
    source.shader.effects = {{
        .kind = fabric::project::SurfaceEffectKind::holography,
        .amount = 1.2F, .scale = 0.0F}};
    const auto report = fabric::project::validate_textured_path(
        manifest(), source);
    CHECK_FALSE(report.ok());
    CHECK(has_field(report, "shader.effects[0].amount"));
    CHECK(has_field(report, "shader.effects[0].scale"));
}

TEST_CASE("textured path persists source edge and thickness metrics") {
    auto source = textured_path();
    source.texture_metrics = {
        .origin = {0.05F, 0.2F}, .size = {0.9F, 0.6F}};
    const auto parsed = fabric::project::parse_textured_path(
        manifest(), fabric::project::serialize_textured_path(source));
    REQUIRE(parsed.ok());
    CHECK(parsed.asset->texture_metrics == source.texture_metrics);
}

TEST_CASE("textured path parser is strict") {
    const auto source = textured_path();
    auto serialized = fabric::project::serialize_textured_path(source);
    serialized.insert(serialized.find('{') + 1U, "\n  \"collision\": true,");
    CHECK_FALSE(fabric::project::parse_textured_path(
                    manifest(), serialized).ok());

    serialized = fabric::project::serialize_textured_path(source);
    const auto line = serialized.find("\"kind\": \"line\"");
    REQUIRE(line != std::string::npos);
    const auto point_end = serialized.find(
        '}', serialized.find("\"point\"", line));
    REQUIRE(point_end != std::string::npos);
    serialized.insert(point_end + 1U,
                      ", \"control1\": {\"x\": 0, \"y\": 0}");
    CHECK_FALSE(fabric::project::parse_textured_path(
                    manifest(), serialized).ok());
}

TEST_CASE("textured path validator rejects malformed geometry and style") {
    auto source = textured_path();
    source.commands[1].kind = fabric::project::TexturedPathCommandKind::move;
    source.width = 0.0F;
    source.width_profile[1].position = 0.0F;
    source.texture.expected_type = "vector";
    source.uv_scale.x = -1.0F;
    source.opacity = 2.0F;
    source.miter_limit = 0.5F;
    const auto report = fabric::project::validate_textured_path(
        manifest(), source);
    CHECK_FALSE(report.ok());
    CHECK(has_field(report, "commands[1].kind"));
    CHECK(has_field(report, "width"));
    CHECK(has_field(report, "widthProfile[1].position"));
    CHECK(has_field(report, "texture"));
    CHECK(has_field(report, "uvScale"));
    CHECK(has_field(report, "opacity"));
    CHECK(has_field(report, "miterLimit"));

    source = textured_path();
    source.commands[2].control1.x =
        std::numeric_limits<float>::infinity();
    source.closed = true;
    source.commands.back().point = source.commands.front().point;
    const auto coordinates = fabric::project::validate_textured_path(
        manifest(), source);
    CHECK_FALSE(coordinates.ok());
    CHECK(has_field(coordinates, "commands[2]"));
    CHECK(has_field(coordinates, "commands"));
}

TEST_CASE("textured path publication is atomic") {
    const auto root = temporary_root("fabric-textured-path-publish");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source = textured_path();
    REQUIRE(fabric::project::publish_textured_path(
                root, manifest(), source).ok());
    const auto path = root / fabric::project::textured_path_document_path(
        manifest(), source.document.id);
    const auto published = read_file(path);

    source.width = -1.0F;
    CHECK_FALSE(fabric::project::publish_textured_path(
                    root, manifest(), source).ok());
    CHECK(read_file(path) == published);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("headless validation resolves textured path textures") {
    const auto root = temporary_root("fabric-textured-path-graph");
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    const auto input = root / "thread.png";
    std::ofstream{input, std::ios::binary} << "source-bytes";
    const fabric::project::TextureAsset texture{
        .document = {.schema_version = 1,
                     .type = "texture",
                     .id = {.value = "yellow-thread"},
                     .name = "Yellow Thread"},
        .source = "assets/textures/yellow-thread.png",
        .width = 8U,
        .height = 8U,
    };
    REQUIRE(fabric::project::publish_texture_asset(
                root, manifest(), texture, input).ok());
    auto source = textured_path();
    REQUIRE(fabric::project::publish_textured_path(
                root, manifest(), source).ok());
    CHECK(fabric::project::validate_project(root).ok());

    source.texture.id.value = "missing-thread";
    REQUIRE(fabric::project::publish_textured_path(
                root, manifest(), source).ok());
    const auto missing = fabric::project::validate_project(root);
    CHECK_FALSE(missing.ok());
    CHECK(std::ranges::any_of(missing.errors, [](const auto& error) {
        return error.code == fabric::project::ErrorCode::missing_resource;
    }));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
