#pragma once

#include "fabric/render/aseprite.hpp"
#include "fabric/render/raster_image.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::render {

struct SpriteRect {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const SpriteRect&, const SpriteRect&) = default;
};

struct SpriteSourceFrame {
    std::string name;
    RasterImage image;
    std::uint32_t duration_ms{100};
    std::optional<AsepritePoint> pivot;
};

struct SpriteAtlasFrame {
    std::string name;
    SpriteRect atlas_bounds;
    SpriteRect source_bounds;
    std::uint32_t source_width{};
    std::uint32_t source_height{};
    std::uint32_t duration_ms{};
    std::optional<AsepritePoint> pivot;

    friend bool operator==(const SpriteAtlasFrame&, const SpriteAtlasFrame&) =
        default;
};

enum class SpriteAtlasErrorCode {
    no_frames,
    invalid_image,
    invalid_region,
    atlas_too_large,
    encode_failed,
};

struct SpriteAtlasError {
    SpriteAtlasErrorCode code{};
    std::string message;
};

struct SpriteAtlas {
    RasterImage image;
    std::vector<std::uint8_t> png;
    std::vector<SpriteAtlasFrame> frames;
};

struct SpriteAtlasResult {
    std::optional<SpriteAtlas> atlas;
    std::optional<SpriteAtlasError> error;

    [[nodiscard]] bool ok() const noexcept {
        return atlas.has_value() && !error.has_value();
    }
};

struct SpriteRegion {
    std::string name;
    SpriteRect bounds;
    std::uint32_t duration_ms{100};
    std::optional<AsepritePoint> pivot;
};

struct SpriteGrid {
    std::uint32_t frame_width{};
    std::uint32_t frame_height{};
    std::uint32_t offset_x{};
    std::uint32_t offset_y{};
    std::uint32_t spacing_x{};
    std::uint32_t spacing_y{};
    std::uint32_t duration_ms{100};
};

struct SpriteFramesResult {
    std::optional<std::vector<SpriteSourceFrame>> frames;
    std::optional<SpriteAtlasError> error;

    [[nodiscard]] bool ok() const noexcept {
        return frames.has_value() && !error.has_value();
    }
};

[[nodiscard]] std::string_view to_string(SpriteAtlasErrorCode code) noexcept;
[[nodiscard]] SpriteFramesResult slice_sprite_grid(
    const RasterImage& source, const SpriteGrid& grid);
[[nodiscard]] SpriteFramesResult slice_sprite_regions(
    const RasterImage& source, std::span<const SpriteRegion> regions);
[[nodiscard]] SpriteAtlasResult build_sprite_atlas(
    std::span<const SpriteSourceFrame> frames);

} // namespace fabric::render
