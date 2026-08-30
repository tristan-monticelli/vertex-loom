#include "fabric/render/raster_image.hpp"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <utility>

namespace fabric::render {
namespace {

RasterImageResult failure(const RasterErrorCode code, std::string message) {
    return RasterImageResult{.error = RasterError{code, std::move(message)}};
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

std::uint32_t read_big_endian_u32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

std::optional<RasterError> preflight_png(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, 24> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) {
        return RasterError{RasterErrorCode::decode_failed,
                           "cannot read a complete PNG header"};
    }

    constexpr std::array<std::uint8_t, 8> signature{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    if (!std::equal(signature.begin(), signature.end(), header.begin()) ||
        std::memcmp(header.data() + 12, "IHDR", 4) != 0) {
        return RasterError{RasterErrorCode::decode_failed,
                           "file does not contain a valid PNG signature and IHDR"};
    }

    const std::uint32_t width = read_big_endian_u32(header.data() + 16);
    const std::uint32_t height = read_big_endian_u32(header.data() + 20);
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (width == 0 || height == 0 || width > maximum_raster_dimension ||
        height > maximum_raster_dimension || pixels > maximum_raster_pixels) {
        return RasterError{RasterErrorCode::invalid_dimensions,
                           "image dimensions exceed the raster safety limits"};
    }
    return std::nullopt;
}

RasterImageResult convert_to_rgba8(SDL_Surface* decoded,
                                   const std::uint32_t maximum_dimension,
                                   const std::uint64_t maximum_pixels) {
    const bool dimensions_are_valid =
        decoded->w > 0 && decoded->h > 0 &&
        decoded->w <= static_cast<int>(maximum_dimension) &&
        decoded->h <= static_cast<int>(maximum_dimension) &&
        static_cast<std::uint64_t>(decoded->w) *
                static_cast<std::uint64_t>(decoded->h) <=
            maximum_pixels;
    if (!dimensions_are_valid) {
        SDL_FreeSurface(decoded);
        return failure(RasterErrorCode::invalid_dimensions,
                       "image dimensions exceed the preview safety limits");
    }

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(
        decoded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(decoded);
    if (converted == nullptr) {
        return failure(RasterErrorCode::decode_failed, SDL_GetError());
    }

    const auto width = static_cast<std::uint32_t>(converted->w);
    const auto height = static_cast<std::uint32_t>(converted->h);
    const std::size_t row_size = static_cast<std::size_t>(width) * 4U;
    RasterImage image{
        .width = width,
        .height = height,
        .rgba8 = std::vector<std::uint8_t>(row_size * height),
    };
    const auto* source = static_cast<const std::uint8_t*>(converted->pixels);
    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(image.rgba8.data() + row_size * row,
                    source + static_cast<std::size_t>(converted->pitch) * row,
                    row_size);
    }
    SDL_FreeSurface(converted);
    return RasterImageResult{.image = std::move(image)};
}

} // namespace

std::string_view to_string(const RasterErrorCode code) noexcept {
    switch (code) {
    case RasterErrorCode::invalid_extension: return "invalid_extension";
    case RasterErrorCode::decode_failed: return "decode_failed";
    case RasterErrorCode::invalid_dimensions: return "invalid_dimensions";
    case RasterErrorCode::source_too_large: return "source_too_large";
    }
    return "unknown_error";
}

RasterImageResult load_png(const std::filesystem::path& path) {
    if (lowercase_extension(path) != ".png") {
        return failure(RasterErrorCode::invalid_extension,
                       "source file must use the .png extension");
    }
    if (const auto error = preflight_png(path); error.has_value()) {
        return RasterImageResult{.error = *error};
    }

    SDL_Surface* decoded = IMG_Load(path.string().c_str());
    if (decoded == nullptr) {
        return failure(RasterErrorCode::decode_failed, IMG_GetError());
    }

    return convert_to_rgba8(decoded, maximum_raster_dimension,
                            maximum_raster_pixels);
}

RasterImageResult load_svg_preview(const std::filesystem::path& path) {
    if (lowercase_extension(path) != ".svg") {
        return failure(RasterErrorCode::invalid_extension,
                       "source file must use the .svg extension");
    }
    std::error_code filesystem_error;
    const auto source_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        return failure(RasterErrorCode::decode_failed,
                       "cannot inspect the SVG source");
    }
    if (source_size > maximum_svg_source_bytes) {
        return failure(RasterErrorCode::source_too_large,
                       "SVG source exceeds the 8 MiB safety limit");
    }

    SDL_RWops* input = SDL_RWFromFile(path.string().c_str(), "rb");
    if (input == nullptr) {
        return failure(RasterErrorCode::decode_failed, SDL_GetError());
    }
    SDL_Surface* decoded = IMG_LoadSizedSVG_RW(
        input, static_cast<int>(maximum_svg_preview_dimension),
        static_cast<int>(maximum_svg_preview_dimension));
    SDL_RWclose(input);
    if (decoded == nullptr) {
        return failure(RasterErrorCode::decode_failed, IMG_GetError());
    }
    return convert_to_rgba8(decoded, maximum_svg_preview_dimension,
                            maximum_svg_preview_pixels);
}

} // namespace fabric::render
