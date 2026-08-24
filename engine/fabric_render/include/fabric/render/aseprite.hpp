#pragma once

#include "fabric/render/raster_image.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::render {

inline constexpr std::uintmax_t maximum_aseprite_source_bytes =
    256U * 1024U * 1024U;
inline constexpr std::uint32_t maximum_aseprite_frames = 65'535;

enum class AsepriteErrorCode {
    invalid_extension,
    io_error,
    source_too_large,
    truncated,
    invalid_header,
    invalid_dimensions,
    unsupported_color_depth,
    invalid_frame,
    invalid_chunk,
    unsupported_external_reference,
    unsupported_tilemap,
    unsupported_blend_mode,
    unsupported_z_index,
    inflate_failed,
    invalid_reference,
    invalid_palette,
    invalid_metadata,
};

struct AsepriteError {
    AsepriteErrorCode code{};
    std::uint64_t offset{};
    std::string message;
};

struct AsepriteLayer {
    std::string name;
    std::optional<std::uint32_t> parent;
    bool group{};
    bool visible{};
    std::uint8_t opacity{255};
};

enum class AsepriteLoopDirection {
    forward,
    reverse,
    ping_pong,
    ping_pong_reverse,
};

struct AsepriteTag {
    std::string name;
    std::uint32_t from_frame{};
    std::uint32_t to_frame{};
    AsepriteLoopDirection direction{AsepriteLoopDirection::forward};
    std::uint32_t repeat{};
};

struct AsepritePoint {
    std::int32_t x{};
    std::int32_t y{};

    friend bool operator==(const AsepritePoint&, const AsepritePoint&) = default;
};

struct AsepriteBounds {
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const AsepriteBounds&, const AsepriteBounds&) = default;
};

struct AsepriteSliceKey {
    std::uint32_t frame{};
    AsepriteBounds bounds;
    std::optional<AsepriteBounds> center;
    std::optional<AsepritePoint> pivot;
};

struct AsepriteSlice {
    std::string name;
    std::vector<AsepriteSliceKey> keys;
};

struct AsepriteFrame {
    RasterImage image;
    std::uint32_t duration_ms{};
};

struct AsepriteDocument {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<AsepriteLayer> layers;
    std::vector<AsepriteFrame> frames;
    std::vector<AsepriteTag> tags;
    std::vector<AsepriteSlice> slices;
};

struct AsepriteResult {
    std::optional<AsepriteDocument> document;
    std::optional<AsepriteError> error;

    [[nodiscard]] bool ok() const noexcept {
        return document.has_value() && !error.has_value();
    }
};

[[nodiscard]] std::string_view to_string(AsepriteErrorCode code) noexcept;
[[nodiscard]] AsepriteResult load_aseprite(
    const std::filesystem::path& path);

} // namespace fabric::render
