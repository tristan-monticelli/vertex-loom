#include "fabric/render/raster_image.hpp"
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

using fabric::render::RasterImage;
using Bytes = std::vector<std::uint8_t>;

RasterImage image(const std::uint32_t width, const std::uint32_t height,
                  Bytes pixels) {
    return {.width = width, .height = height, .rgba8 = std::move(pixels)};
}

std::size_t pixel_offset(const RasterImage& value, const std::uint32_t x,
                         const std::uint32_t y) {
    return (static_cast<std::size_t>(y) * value.width + x) * 4U;
}

Bytes pixel(const RasterImage& value, const std::uint32_t x,
            const std::uint32_t y) {
    const std::size_t offset = pixel_offset(value, x, y);
    return {value.rgba8[offset], value.rgba8[offset + 1],
            value.rgba8[offset + 2], value.rgba8[offset + 3]};
}

class TemporaryPng {
public:
    explicit TemporaryPng(const Bytes& contents) {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        directory_ = std::filesystem::temp_directory_path() /
            ("vertex-loom-atlas-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(directory_);
        path_ = directory_ / "atlas.png";
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
        REQUIRE(output.good());
    }

    ~TemporaryPng() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("grid slicing uses stable row-major frame order") {
    const RasterImage source = image(
        4, 2,
        Bytes{
            255, 0, 0, 255, 255, 0, 0, 255,
            0, 255, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 0, 0, 255, 255,
            255, 0, 255, 255, 255, 0, 255, 255,
        });

    const auto sliced = fabric::render::slice_sprite_grid(
        source, {.frame_width = 2, .frame_height = 1, .duration_ms = 75});

    REQUIRE(sliced.ok());
    REQUIRE(sliced.frames->size() == 4);
    CHECK((*sliced.frames)[0].name == "frame-0");
    CHECK((*sliced.frames)[0].duration_ms == 75);
    CHECK(pixel((*sliced.frames)[0].image, 0, 0) ==
          Bytes{255, 0, 0, 255});
    CHECK(pixel((*sliced.frames)[1].image, 0, 0) ==
          Bytes{0, 255, 0, 255});
    CHECK(pixel((*sliced.frames)[2].image, 0, 0) ==
          Bytes{0, 0, 255, 255});
    CHECK(pixel((*sliced.frames)[3].image, 0, 0) ==
          Bytes{255, 0, 255, 255});
}

TEST_CASE("free slicing preserves names durations pivots and order") {
    const RasterImage source = image(
        3, 1,
        Bytes{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255});
    const std::vector<fabric::render::SpriteRegion> regions{
        {.name = "right",
         .bounds = {2, 0, 1, 1},
         .duration_ms = 90,
         .pivot = fabric::render::AsepritePoint{0, 0}},
        {.name = "left", .bounds = {0, 0, 1, 1}, .duration_ms = 120},
    };

    const auto sliced = fabric::render::slice_sprite_regions(source, regions);

    REQUIRE(sliced.ok());
    CHECK((*sliced.frames)[0].name == "right");
    CHECK((*sliced.frames)[0].duration_ms == 90);
    CHECK((*sliced.frames)[0].pivot == fabric::render::AsepritePoint{0, 0});
    CHECK(pixel((*sliced.frames)[0].image, 0, 0) ==
          Bytes{0, 0, 255, 255});
    CHECK((*sliced.frames)[1].name == "left");
}

TEST_CASE("MaxRects atlas and PNG output are deterministic and extruded") {
    const std::vector<fabric::render::SpriteSourceFrame> frames{
        {.name = "trimmed",
         .image = image(
             3, 2,
             Bytes{0, 0, 0, 0, 10, 20, 30, 255, 40, 50, 60, 255,
                   0, 0, 0, 0, 70, 80, 90, 255, 100, 110, 120, 255}),
         .duration_ms = 80,
         .pivot = fabric::render::AsepritePoint{1, 1}},
        {.name = "small",
         .image = image(1, 1, Bytes{200, 150, 100, 255}),
         .duration_ms = 100},
    };

    const auto first = fabric::render::build_sprite_atlas(frames);
    const auto second = fabric::render::build_sprite_atlas(frames);

    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.atlas->png == second.atlas->png);
    CHECK(first.atlas->frames == second.atlas->frames);
    const auto& metadata = first.atlas->frames[0];
    CHECK(metadata.source_bounds == fabric::render::SpriteRect{1, 0, 2, 2});
    CHECK(metadata.source_width == 3);
    CHECK(metadata.source_height == 2);
    CHECK(metadata.pivot == fabric::render::AsepritePoint{1, 1});
    const auto& bounds = metadata.atlas_bounds;
    CHECK(pixel(first.atlas->image, bounds.x, bounds.y) ==
          Bytes{10, 20, 30, 255});
    CHECK(pixel(first.atlas->image, bounds.x - 1, bounds.y) ==
          Bytes{10, 20, 30, 255});
    CHECK(pixel(first.atlas->image, bounds.x - 2, bounds.y) ==
          Bytes{0, 0, 0, 0});
    CHECK(pixel(first.atlas->image, bounds.x + bounds.width, bounds.y) ==
          Bytes{40, 50, 60, 255});

    const TemporaryPng png(first.atlas->png);
    const auto decoded = fabric::render::load_png(png.path());
    REQUIRE(decoded.ok());
    CHECK(decoded.image->width == first.atlas->image.width);
    CHECK(decoded.image->height == first.atlas->image.height);
    CHECK(decoded.image->rgba8 == first.atlas->image.rgba8);
}

TEST_CASE("invalid grid and free regions fail before atlas generation") {
    const RasterImage source = image(1, 1, Bytes{255, 255, 255, 255});

    CHECK_FALSE(fabric::render::slice_sprite_grid(
                    source, {.frame_width = 0, .frame_height = 1})
                    .ok());
    const std::vector<fabric::render::SpriteRegion> outside{
        {.name = "outside", .bounds = {1, 0, 1, 1}}};
    const auto result = fabric::render::slice_sprite_regions(source, outside);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error->code ==
          fabric::render::SpriteAtlasErrorCode::invalid_region);
    CHECK_FALSE(fabric::render::build_sprite_atlas({}).ok());
}
