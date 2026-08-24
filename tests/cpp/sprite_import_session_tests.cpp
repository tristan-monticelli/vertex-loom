#include "fabric/editor/project_session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void u8(Bytes& bytes, const std::uint8_t value) { bytes.push_back(value); }
void u16(Bytes& bytes, const std::uint16_t value) {
    u8(bytes, static_cast<std::uint8_t>(value & 0xffU));
    u8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}
void u32(Bytes& bytes, const std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        u8(bytes, static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}
void zeros(Bytes& bytes, const std::size_t count) {
    bytes.insert(bytes.end(), count, 0);
}
void string(Bytes& bytes, const std::string& value) {
    u16(bytes, static_cast<std::uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
void patch_u32(Bytes& bytes, const std::size_t offset,
               const std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] =
            static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}
Bytes chunk(const std::uint16_t type, const Bytes& payload) {
    Bytes result;
    u32(result, static_cast<std::uint32_t>(payload.size() + 6U));
    u16(result, type);
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

Bytes aseprite_fixture() {
    Bytes layer;
    u16(layer, 1);
    u16(layer, 0);
    u16(layer, 0);
    zeros(layer, 4);
    u16(layer, 0);
    u8(layer, 255);
    zeros(layer, 3);
    string(layer, "paint");
    Bytes cel;
    u16(cel, 0);
    u16(cel, 0);
    u16(cel, 0);
    u8(cel, 255);
    u16(cel, 0);
    u16(cel, 0);
    zeros(cel, 5);
    u16(cel, 1);
    u16(cel, 1);
    cel.insert(cel.end(), {12, 34, 56, 255});

    Bytes frame;
    u32(frame, 0);
    u16(frame, 0xf1fa);
    u16(frame, 2);
    u16(frame, 90);
    zeros(frame, 2);
    u32(frame, 2);
    const auto layer_chunk = chunk(0x2004, layer);
    const auto cel_chunk = chunk(0x2005, cel);
    frame.insert(frame.end(), layer_chunk.begin(), layer_chunk.end());
    frame.insert(frame.end(), cel_chunk.begin(), cel_chunk.end());
    patch_u32(frame, 0, static_cast<std::uint32_t>(frame.size()));

    Bytes file;
    u32(file, 0);
    u16(file, 0xa5e0);
    u16(file, 1);
    u16(file, 1);
    u16(file, 1);
    u16(file, 32);
    u32(file, 3);
    u16(file, 100);
    zeros(file, 8);
    u8(file, 0);
    zeros(file, 3);
    u16(file, 0);
    u8(file, 1);
    u8(file, 1);
    zeros(file, 8);
    zeros(file, 84);
    file.insert(file.end(), frame.begin(), frame.end());
    patch_u32(file, 0, static_cast<std::uint32_t>(file.size()));
    return file;
}

constexpr std::array<std::uint8_t, 68> valid_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00,
    0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00,
    0x02, 0xeb, 0x01, 0xf5, 0x69, 0x76, 0x9d, 0x7b, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

class TemporaryProject {
public:
    TemporaryProject() {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root_ = std::filesystem::temp_directory_path() /
            ("vertex-loom-sprite-session-tests-" + std::to_string(stamp));
        const fabric::project::ProjectManifest manifest{
            .id = {.value = "sprite-session"},
            .name = "Sprite session",
        };
        REQUIRE(fabric::project::create_project(root_, manifest).ok());
    }

    ~TemporaryProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    std::filesystem::path write(const std::string& name,
                                const std::span<const std::uint8_t> contents) {
        const auto path = root_ / name;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
        REQUIRE(output.good());
        return path;
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

private:
    std::filesystem::path root_;
};

Bytes read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("ProjectSession imports Aseprite source atlas and metadata") {
    TemporaryProject project;
    const auto source_bytes = aseprite_fixture();
    const auto source = project.write("hero.aseprite", source_bytes);
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.root()));

    REQUIRE(session.import_aseprite(source, {.value = "hero"}, "Hero"));

    REQUIRE(session.imported_sprite_sheet().has_value());
    const auto& imported = *session.imported_sprite_sheet();
    CHECK(imported.asset.document.id.value == "hero");
    CHECK(imported.asset.source_kind ==
          fabric::project::SpriteSourceKind::aseprite);
    REQUIRE(imported.asset.frames.size() == 1);
    CHECK(imported.asset.frames[0].duration_ms == 90);
    CHECK(imported.atlas.rgba8.size() ==
          static_cast<std::size_t>(imported.atlas.width) *
              imported.atlas.height * 4U);
    CHECK(read_bytes(project.root() / imported.asset.source) == source_bytes);
    CHECK(fabric::project::validate_project(project.root()).ok());
}

TEST_CASE("failed Aseprite import preserves the last successful sprite") {
    TemporaryProject project;
    const auto source_bytes = aseprite_fixture();
    const auto source = project.write("hero.aseprite", source_bytes);
    const Bytes corrupt{0, 1, 2, 3};
    const auto corrupt_source = project.write("broken.aseprite", corrupt);
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.root()));
    REQUIRE(session.import_aseprite(source, {.value = "hero"}, "Hero"));

    REQUIRE_FALSE(session.import_aseprite(
        corrupt_source, {.value = "broken"}, "Broken"));

    REQUIRE(session.imported_sprite_sheet().has_value());
    CHECK(session.imported_sprite_sheet()->asset.document.id.value == "hero");
    CHECK_FALSE(std::filesystem::exists(
        project.root() / "assets/textures/broken.sprite.json"));
}

TEST_CASE("PNG grid and free frame imports preserve input regions") {
    SECTION("grid") {
        TemporaryProject project;
        const auto source = project.write("sheet.png", valid_png);
        fabric::editor::ProjectSession session;
        REQUIRE(session.open(project.root()));
        REQUIRE(session.import_png_sprite_grid(
            source, {.value = "grid"}, "Grid",
            {.frame_width = 1, .frame_height = 1, .duration_ms = 70}));
        REQUIRE(session.imported_sprite_sheet().has_value());
        CHECK(session.imported_sprite_sheet()->asset.frames[0].input_bounds ==
              fabric::project::SpriteRect{0, 0, 1, 1});
    }

    SECTION("free frame") {
        TemporaryProject project;
        const auto source = project.write("sheet.png", valid_png);
        fabric::editor::ProjectSession session;
        REQUIRE(session.open(project.root()));
        const std::vector<fabric::render::SpriteRegion> regions{{
            .name = "free",
            .bounds = {0, 0, 1, 1},
            .duration_ms = 110,
            .pivot = fabric::render::AsepritePoint{0, 0},
        }};
        REQUIRE(session.import_png_sprite_regions(
            source, {.value = "free"}, "Free", regions));
        REQUIRE(session.imported_sprite_sheet().has_value());
        CHECK(session.imported_sprite_sheet()->asset.frames[0].name == "free");
        CHECK(session.imported_sprite_sheet()->asset.frames[0].duration_ms ==
              110);
    }
}

TEST_CASE("sprite regeneration is deterministic and preserves its source") {
    TemporaryProject project;
    const auto source = project.write("sheet.png", valid_png);
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.root()));
    REQUIRE(session.import_png_sprite_grid(
        source, {.value = "regenerate"}, "Regenerate",
        {.frame_width = 1, .frame_height = 1}));
    const auto source_path =
        project.root() / session.imported_sprite_sheet()->asset.source;
    const auto atlas_path =
        project.root() / session.imported_sprite_sheet()->asset.atlas;
    const Bytes source_before = read_bytes(source_path);
    const Bytes atlas_before = read_bytes(atlas_path);

    REQUIRE(session.regenerate_sprite_sheet({.value = "regenerate"}));

    CHECK(read_bytes(source_path) == source_before);
    CHECK(read_bytes(atlas_path) == atlas_before);
    CHECK(fabric::project::validate_project(project.root()).ok());
}
