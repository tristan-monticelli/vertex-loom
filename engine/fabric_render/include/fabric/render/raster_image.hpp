#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::render {

inline constexpr std::uint32_t maximum_raster_dimension = 16'384;
inline constexpr std::uint64_t maximum_raster_pixels = 67'108'864;

enum class RasterErrorCode {
    invalid_extension,
    decode_failed,
    invalid_dimensions,
};

struct RasterError {
    RasterErrorCode code;
    std::string message;
};

struct RasterImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba8;
};

struct RasterImageResult {
    std::optional<RasterImage> image;
    std::optional<RasterError> error;

    [[nodiscard]] bool ok() const noexcept {
        return image.has_value() && !error.has_value();
    }
};

[[nodiscard]] std::string_view to_string(RasterErrorCode code) noexcept;
[[nodiscard]] RasterImageResult load_png(const std::filesystem::path& path);

} // namespace fabric::render
