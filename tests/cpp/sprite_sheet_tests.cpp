#include "fabric/project/sprite_sheet.hpp"
#include "fabric/render/sprite_atlas.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TemporaryProject {
public:
    TemporaryProject() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root_ = std::filesystem::temp_directory_path() /
            ("vertex-loom-sprite-sheet-tests-" + std::to_string(stamp));
        const auto created = fabric::project::create_project(root_, manifest_);
        REQUIRE(created.ok());
    }

    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

    [[nodiscard]] const fabric::project::ProjectManifest& manifest() const
        noexcept {
        return manifest_;
    }

private:
    std::filesystem::path root_;
    fabric::project::ProjectManifest manifest_{
        .id = {.value = "sprite-tests"},
        .name = "Sprite tests",
    };
};

struct Fixture {
    fabric::project::SpriteSheetDefinition definition;
    std::vector<std::uint8_t> png;
};

Fixture fixture(const fabric::project::ProjectManifest& manifest,
                const fabric::project::SpriteSourceKind kind =
                    fabric::project::SpriteSourceKind::aseprite) {
    const std::vector<fabric::render::SpriteSourceFrame> frames{
        {.name = "idle-0",
         .image = {.width = 2,
                   .height = 1,
                   .rgba8 = {255, 0, 0, 255, 0, 255, 0, 255}},
         .duration_ms = 80,
         .pivot = fabric::render::AsepritePoint{1, 0}},
    };
    auto atlas = fabric::render::build_sprite_atlas(frames);
    REQUIRE(atlas.ok());
    const fabric::core::ResourceId id{.value = "hero-idle"};
    const auto& packed = atlas.atlas->frames[0];
    fabric::project::SpriteSheetDefinition definition{
        .document = {
            .schema_version =
                fabric::project::current_sprite_sheet_schema_version,
            .type = "spriteSheet",
            .id = id,
            .name = "Hero idle",
        },
        .source_kind = kind,
        .source = fabric::project::sprite_sheet_source_path(manifest, id, kind),
        .atlas = fabric::project::sprite_sheet_atlas_path(manifest, id),
        .atlas_size = {atlas.atlas->image.width, atlas.atlas->image.height},
        .frames = {{
            .name = packed.name,
            .atlas_bounds = {packed.atlas_bounds.x, packed.atlas_bounds.y,
                             packed.atlas_bounds.width,
                             packed.atlas_bounds.height},
            .source_bounds = {packed.source_bounds.x, packed.source_bounds.y,
                              packed.source_bounds.width,
                              packed.source_bounds.height},
            .source_size = {packed.source_width, packed.source_height},
            .duration_ms = packed.duration_ms,
            .pivot = fabric::project::SpritePoint{1, 0},
        }},
        .tags = {{.name = "idle",
                  .from_frame = 0,
                  .to_frame = 0,
                  .direction = "forward",
                  .repeat = 0}},
        .slices = {{
            .name = "hitbox",
            .keys = {{.frame = 0,
                      .bounds = {-1, 0, 2, 1},
                      .pivot = fabric::project::SpritePoint{1, 0}}},
        }},
    };
    return {std::move(definition), std::move(atlas.atlas->png)};
}

std::filesystem::path write_source(const TemporaryProject& project,
                                   const std::string& extension) {
    const auto path = project.root() / ("source" + extension);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "validated source fixture";
    REQUIRE(output.good());
    return path;
}

} // namespace

TEST_CASE("SpriteSheetDefinition v1 round-trips all metadata strictly") {
    TemporaryProject project;
    const auto expected = fixture(project.manifest()).definition;

    const auto serialized = fabric::project::serialize_sprite_sheet(expected);
    const auto parsed =
        fabric::project::parse_sprite_sheet(project.manifest(), serialized);

    REQUIRE(parsed.ok());
    CHECK(*parsed.asset == expected);

    std::string with_unknown = serialized;
    with_unknown.insert(with_unknown.find('{') + 1U, "\n  \"unknown\": true,");
    const auto rejected =
        fabric::project::parse_sprite_sheet(project.manifest(), with_unknown);
    REQUIRE_FALSE(rejected.ok());
    CHECK(rejected.errors[0].code == fabric::project::ErrorCode::invalid_asset);
}

TEST_CASE("sprite sheet validation rejects paths and frame ranges") {
    TemporaryProject project;
    auto invalid = fixture(project.manifest()).definition;
    invalid.source = "../outside.aseprite";
    invalid.tags[0].to_frame = 4;

    const auto report =
        fabric::project::validate_sprite_sheet(project.manifest(), invalid);

    REQUIRE_FALSE(report.ok());
    CHECK(report.errors.size() == 2);
}

TEST_CASE("sprite sheet publication preserves source and publishes JSON last") {
    TemporaryProject project;
    auto value = fixture(project.manifest());
    const auto source = write_source(project, ".aseprite");

    const auto published = fabric::project::publish_sprite_sheet(
        project.root(), project.manifest(), value.definition, source, value.png);

    REQUIRE(published.ok());
    CHECK(std::filesystem::is_regular_file(
        project.root() / value.definition.source));
    CHECK(std::filesystem::is_regular_file(
        project.root() / value.definition.atlas));
    CHECK(std::filesystem::is_regular_file(
        project.root() / fabric::project::sprite_sheet_document_path(
                             project.manifest(),
                             value.definition.document.id)));
    CHECK(fabric::project::validate_project(project.root()).ok());

    const auto duplicate = fabric::project::publish_sprite_sheet(
        project.root(), project.manifest(), value.definition, source, value.png);
    REQUIRE_FALSE(duplicate.ok());
    CHECK(duplicate.errors[0].code ==
          fabric::project::ErrorCode::asset_already_exists);
}

TEST_CASE("headless validation rejects an atlas with inconsistent dimensions") {
    TemporaryProject project;
    auto value = fixture(project.manifest());
    const auto source = write_source(project, ".aseprite");
    REQUIRE(fabric::project::publish_sprite_sheet(
                project.root(), project.manifest(), value.definition, source,
                value.png)
                .ok());

    const auto atlas_path = project.root() / value.definition.atlas;
    std::fstream atlas(atlas_path,
                       std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(atlas.good());
    atlas.seekp(19);
    atlas.put(static_cast<char>(value.definition.atlas_size.width + 1U));
    atlas.flush();
    REQUIRE(atlas.good());

    const auto report = fabric::project::validate_project(project.root());
    REQUIRE_FALSE(report.ok());
    CHECK(report.errors[0].field == "atlasSize");
}
