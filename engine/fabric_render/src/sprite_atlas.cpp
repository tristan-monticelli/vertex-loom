#include "fabric/render/sprite_atlas.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace fabric::render {
namespace {

constexpr std::uint32_t extrusion = 1;
constexpr std::uint32_t padding = 1;
constexpr std::uint32_t border = extrusion + padding;

SpriteAtlasResult atlas_failure(const SpriteAtlasErrorCode code,
                                std::string message) {
    return {.error = SpriteAtlasError{code, std::move(message)}};
}

SpriteFramesResult frames_failure(const SpriteAtlasErrorCode code,
                                  std::string message) {
    return {.error = SpriteAtlasError{code, std::move(message)}};
}

bool valid_image(const RasterImage& image) {
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(image.width) * image.height;
    return image.width > 0 && image.height > 0 &&
        image.width <= maximum_raster_dimension &&
        image.height <= maximum_raster_dimension &&
        pixels <= maximum_raster_pixels &&
        image.rgba8.size() == pixels * 4U;
}

bool region_fits(const RasterImage& image, const SpriteRect& region) {
    return region.width > 0 && region.height > 0 &&
        region.x <= image.width && region.y <= image.height &&
        region.width <= image.width - region.x &&
        region.height <= image.height - region.y;
}

RasterImage extract(const RasterImage& source, const SpriteRect& region) {
    RasterImage result{
        .width = region.width,
        .height = region.height,
        .rgba8 = std::vector<std::uint8_t>(
            static_cast<std::size_t>(region.width) * region.height * 4U),
    };
    const std::size_t row_bytes = static_cast<std::size_t>(region.width) * 4U;
    for (std::uint32_t row = 0; row < region.height; ++row) {
        const std::size_t source_offset =
            (static_cast<std::size_t>(region.y + row) * source.width +
             region.x) *
            4U;
        std::copy_n(source.rgba8.begin() +
                        static_cast<std::ptrdiff_t>(source_offset),
                    row_bytes,
                    result.rgba8.begin() +
                        static_cast<std::ptrdiff_t>(row * row_bytes));
    }
    return result;
}

SpriteRect trimmed_bounds(const RasterImage& image) {
    std::uint32_t minimum_x = image.width;
    std::uint32_t minimum_y = image.height;
    std::uint32_t maximum_x = 0;
    std::uint32_t maximum_y = 0;
    bool found = false;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::size_t alpha =
                (static_cast<std::size_t>(y) * image.width + x) * 4U + 3U;
            if (image.rgba8[alpha] == 0) {
                continue;
            }
            found = true;
            minimum_x = std::min(minimum_x, x);
            minimum_y = std::min(minimum_y, y);
            maximum_x = std::max(maximum_x, x);
            maximum_y = std::max(maximum_y, y);
        }
    }
    if (!found) {
        return {0, 0, 1, 1};
    }
    return {minimum_x, minimum_y, maximum_x - minimum_x + 1U,
            maximum_y - minimum_y + 1U};
}

struct PackedInput {
    std::size_t index{};
    SpriteRect source;
    std::uint32_t width{};
    std::uint32_t height{};
};

struct PackingRect {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};

    friend bool operator==(const PackingRect&, const PackingRect&) = default;
};

bool intersects(const PackingRect& left, const PackingRect& right) {
    return left.x < right.x + right.width &&
        left.x + left.width > right.x && left.y < right.y + right.height &&
        left.y + left.height > right.y;
}

bool contains(const PackingRect& outer, const PackingRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
}

void split_free_rectangles(std::vector<PackingRect>& free_rectangles,
                           const PackingRect& used) {
    std::vector<PackingRect> next;
    next.reserve(free_rectangles.size() * 2U);
    for (const auto& free : free_rectangles) {
        if (!intersects(free, used)) {
            next.push_back(free);
            continue;
        }
        const std::uint32_t free_right = free.x + free.width;
        const std::uint32_t free_bottom = free.y + free.height;
        const std::uint32_t used_right = used.x + used.width;
        const std::uint32_t used_bottom = used.y + used.height;
        if (used.y > free.y) {
            next.push_back({free.x, free.y, free.width, used.y - free.y});
        }
        if (used_bottom < free_bottom) {
            next.push_back(
                {free.x, used_bottom, free.width, free_bottom - used_bottom});
        }
        if (used.x > free.x) {
            next.push_back({free.x, free.y, used.x - free.x, free.height});
        }
        if (used_right < free_right) {
            next.push_back(
                {used_right, free.y, free_right - used_right, free.height});
        }
    }
    std::ranges::sort(next, [](const PackingRect& left,
                               const PackingRect& right) {
        return std::tie(left.y, left.x, left.height, left.width) <
            std::tie(right.y, right.x, right.height, right.width);
    });
    next.erase(std::unique(next.begin(), next.end()), next.end());
    std::vector<bool> remove(next.size(), false);
    for (std::size_t inner = 0; inner < next.size(); ++inner) {
        for (std::size_t outer = 0; outer < next.size(); ++outer) {
            if (inner != outer && contains(next[outer], next[inner])) {
                remove[inner] = true;
                break;
            }
        }
    }
    free_rectangles.clear();
    for (std::size_t index = 0; index < next.size(); ++index) {
        if (!remove[index] && next[index].width > 0 && next[index].height > 0) {
            free_rectangles.push_back(next[index]);
        }
    }
}

std::optional<std::vector<PackingRect>> pack(
    const std::vector<PackedInput>& inputs, const std::uint32_t width,
    const std::uint32_t height) {
    std::vector<PackingRect> placements(inputs.size());
    std::vector<PackingRect> free_rectangles{{0, 0, width, height}};
    for (const auto& input : inputs) {
        std::optional<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t,
                                 std::uint32_t, std::size_t>>
            best;
        for (std::size_t index = 0; index < free_rectangles.size(); ++index) {
            const auto& free = free_rectangles[index];
            if (input.width > free.width || input.height > free.height) {
                continue;
            }
            const std::uint32_t remaining_width = free.width - input.width;
            const std::uint32_t remaining_height = free.height - input.height;
            const auto score = std::tuple{
                std::min(remaining_width, remaining_height),
                std::max(remaining_width, remaining_height), free.y, free.x,
                index};
            if (!best.has_value() || score < *best) {
                best = score;
            }
        }
        if (!best.has_value()) {
            return std::nullopt;
        }
        const auto& free = free_rectangles[std::get<4>(*best)];
        const PackingRect used{free.x, free.y, input.width, input.height};
        placements[input.index] = used;
        split_free_rectangles(free_rectangles, used);
    }
    return placements;
}

std::uint32_t next_power_of_two(std::uint32_t value) {
    std::uint32_t result = 1;
    while (result < value && result < maximum_raster_dimension) {
        result *= 2U;
    }
    return result;
}

void copy_pixel(const RasterImage& source, const std::uint32_t source_x,
                const std::uint32_t source_y, RasterImage& destination,
                const std::uint32_t destination_x,
                const std::uint32_t destination_y) {
    const std::size_t source_offset =
        (static_cast<std::size_t>(source_y) * source.width + source_x) * 4U;
    const std::size_t destination_offset =
        (static_cast<std::size_t>(destination_y) * destination.width +
         destination_x) *
        4U;
    std::copy_n(source.rgba8.begin() +
                    static_cast<std::ptrdiff_t>(source_offset),
                4, destination.rgba8.begin() +
                       static_cast<std::ptrdiff_t>(destination_offset));
}

using Bytes = std::vector<std::uint8_t>;

void write_big_endian(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_png_chunk(Bytes& png, const std::array<char, 4>& type,
                      const std::span<const std::uint8_t> payload) {
    write_big_endian(png, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crc_start = png.size();
    for (const char value : type) {
        png.push_back(static_cast<std::uint8_t>(value));
    }
    png.insert(png.end(), payload.begin(), payload.end());
    const uLong checksum = crc32(
        0, png.data() + static_cast<std::ptrdiff_t>(crc_start),
        static_cast<uInt>(4U + payload.size()));
    write_big_endian(png, static_cast<std::uint32_t>(checksum));
}

std::optional<Bytes> encode_png(const RasterImage& image) {
    const std::size_t row_bytes = static_cast<std::size_t>(image.width) * 4U;
    const std::size_t filtered_row = row_bytes + 1U;
    if (filtered_row > std::numeric_limits<uLong>::max() / image.height) {
        return std::nullopt;
    }
    Bytes filtered(filtered_row * image.height);
    for (std::uint32_t row = 0; row < image.height; ++row) {
        filtered[static_cast<std::size_t>(row) * filtered_row] = 0;
        std::copy_n(
            image.rgba8.begin() +
                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(row) *
                                            row_bytes),
            row_bytes,
            filtered.begin() + static_cast<std::ptrdiff_t>(
                                   static_cast<std::size_t>(row) * filtered_row +
                                   1U));
    }
    uLongf compressed_size = compressBound(static_cast<uLong>(filtered.size()));
    Bytes compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size, filtered.data(),
                  static_cast<uLong>(filtered.size()), Z_BEST_COMPRESSION) !=
        Z_OK) {
        return std::nullopt;
    }
    compressed.resize(compressed_size);

    Bytes png{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    Bytes header;
    write_big_endian(header, image.width);
    write_big_endian(header, image.height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_png_chunk(png, {'I', 'H', 'D', 'R'}, header);
    append_png_chunk(png, {'I', 'D', 'A', 'T'}, compressed);
    append_png_chunk(png, {'I', 'E', 'N', 'D'}, {});
    return png;
}

} // namespace

std::string_view to_string(const SpriteAtlasErrorCode code) noexcept {
    switch (code) {
    case SpriteAtlasErrorCode::no_frames: return "no_frames";
    case SpriteAtlasErrorCode::invalid_image: return "invalid_image";
    case SpriteAtlasErrorCode::invalid_region: return "invalid_region";
    case SpriteAtlasErrorCode::atlas_too_large: return "atlas_too_large";
    case SpriteAtlasErrorCode::encode_failed: return "encode_failed";
    }
    return "unknown_error";
}

SpriteFramesResult slice_sprite_grid(const RasterImage& source,
                                     const SpriteGrid& grid) {
    if (!valid_image(source)) {
        return frames_failure(SpriteAtlasErrorCode::invalid_image,
                              "source image is not valid RGBA8");
    }
    if (grid.frame_width == 0 || grid.frame_height == 0 ||
        grid.duration_ms == 0 || grid.offset_x >= source.width ||
        grid.offset_y >= source.height) {
        return frames_failure(SpriteAtlasErrorCode::invalid_region,
                              "grid dimensions, offset or duration are invalid");
    }
    std::vector<SpriteSourceFrame> frames;
    for (std::uint32_t y = grid.offset_y;
         grid.frame_height <= source.height - y;) {
        for (std::uint32_t x = grid.offset_x;
             grid.frame_width <= source.width - x;) {
            const SpriteRect bounds{x, y, grid.frame_width, grid.frame_height};
            frames.push_back(SpriteSourceFrame{
                .name = "frame-" + std::to_string(frames.size()),
                .image = extract(source, bounds),
                .duration_ms = grid.duration_ms,
                .input_bounds = bounds,
            });
            const std::uint64_t next = static_cast<std::uint64_t>(x) +
                grid.frame_width + grid.spacing_x;
            if (next > std::numeric_limits<std::uint32_t>::max()) {
                break;
            }
            x = static_cast<std::uint32_t>(next);
            if (x >= source.width) {
                break;
            }
        }
        const std::uint64_t next = static_cast<std::uint64_t>(y) +
            grid.frame_height + grid.spacing_y;
        if (next > std::numeric_limits<std::uint32_t>::max()) {
            break;
        }
        y = static_cast<std::uint32_t>(next);
        if (y >= source.height) {
            break;
        }
    }
    if (frames.empty()) {
        return frames_failure(SpriteAtlasErrorCode::no_frames,
                              "grid produces no complete frames");
    }
    return {.frames = std::move(frames)};
}

SpriteFramesResult slice_sprite_regions(
    const RasterImage& source, const std::span<const SpriteRegion> regions) {
    if (!valid_image(source)) {
        return frames_failure(SpriteAtlasErrorCode::invalid_image,
                              "source image is not valid RGBA8");
    }
    if (regions.empty()) {
        return frames_failure(SpriteAtlasErrorCode::no_frames,
                              "at least one free frame is required");
    }
    std::vector<SpriteSourceFrame> frames;
    frames.reserve(regions.size());
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const auto& region = regions[index];
        if (!region_fits(source, region.bounds) || region.duration_ms == 0) {
            return frames_failure(SpriteAtlasErrorCode::invalid_region,
                                  "free frame is outside the source image");
        }
        frames.push_back(SpriteSourceFrame{
            .name = region.name.empty()
                ? "frame-" + std::to_string(index)
                : region.name,
            .image = extract(source, region.bounds),
            .duration_ms = region.duration_ms,
            .pivot = region.pivot,
            .input_bounds = region.bounds,
        });
    }
    return {.frames = std::move(frames)};
}

SpriteAtlasResult build_sprite_atlas(
    const std::span<const SpriteSourceFrame> frames) {
    if (frames.empty()) {
        return atlas_failure(SpriteAtlasErrorCode::no_frames,
                             "at least one sprite frame is required");
    }
    std::vector<PackedInput> inputs;
    inputs.reserve(frames.size());
    std::uint64_t total_area = 0;
    std::uint32_t maximum_width = 0;
    std::uint32_t maximum_height = 0;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (!valid_image(frames[index].image) ||
            frames[index].duration_ms == 0) {
            return atlas_failure(SpriteAtlasErrorCode::invalid_image,
                                 "sprite frame is not valid RGBA8");
        }
        const SpriteRect source = trimmed_bounds(frames[index].image);
        const std::uint32_t width = source.width + border * 2U;
        const std::uint32_t height = source.height + border * 2U;
        if (width > maximum_raster_dimension ||
            height > maximum_raster_dimension) {
            return atlas_failure(SpriteAtlasErrorCode::atlas_too_large,
                                 "sprite frame cannot fit atlas borders");
        }
        inputs.push_back({index, source, width, height});
        total_area += static_cast<std::uint64_t>(width) * height;
        maximum_width = std::max(maximum_width, width);
        maximum_height = std::max(maximum_height, height);
    }
    std::ranges::sort(inputs, [](const PackedInput& left,
                                 const PackedInput& right) {
        return std::tuple{std::max(left.width, left.height),
                          static_cast<std::uint64_t>(left.width) * left.height,
                          left.height, left.width,
                          std::numeric_limits<std::size_t>::max() - left.index} >
            std::tuple{std::max(right.width, right.height),
                       static_cast<std::uint64_t>(right.width) * right.height,
                       right.height, right.width,
                       std::numeric_limits<std::size_t>::max() - right.index};
    });

    std::uint32_t atlas_width = next_power_of_two(maximum_width);
    std::uint32_t atlas_height = next_power_of_two(maximum_height);
    while (static_cast<std::uint64_t>(atlas_width) * atlas_height < total_area) {
        if (atlas_width <= atlas_height &&
            atlas_width < maximum_raster_dimension) {
            atlas_width *= 2U;
        } else if (atlas_height < maximum_raster_dimension) {
            atlas_height *= 2U;
        } else {
            return atlas_failure(SpriteAtlasErrorCode::atlas_too_large,
                                 "sprite frames exceed the maximum atlas area");
        }
    }

    std::optional<std::vector<PackingRect>> placements;
    while (!(placements = pack(inputs, atlas_width, atlas_height)).has_value()) {
        if (atlas_width <= atlas_height &&
            atlas_width < maximum_raster_dimension) {
            atlas_width *= 2U;
        } else if (atlas_height < maximum_raster_dimension) {
            atlas_height *= 2U;
        } else if (atlas_width < maximum_raster_dimension) {
            atlas_width *= 2U;
        } else {
            return atlas_failure(SpriteAtlasErrorCode::atlas_too_large,
                                 "MaxRects cannot place all sprite frames");
        }
    }
    if (static_cast<std::uint64_t>(atlas_width) * atlas_height >
        maximum_raster_pixels) {
        return atlas_failure(SpriteAtlasErrorCode::atlas_too_large,
                             "atlas exceeds the raster pixel limit");
    }

    SpriteAtlas atlas{
        .image = {
            .width = atlas_width,
            .height = atlas_height,
            .rgba8 = std::vector<std::uint8_t>(
                static_cast<std::size_t>(atlas_width) * atlas_height * 4U),
        },
        .frames = std::vector<SpriteAtlasFrame>(frames.size()),
    };
    for (const auto& input : inputs) {
        const auto& placement = (*placements)[input.index];
        const std::uint32_t destination_x = placement.x + border;
        const std::uint32_t destination_y = placement.y + border;
        const auto& source = frames[input.index].image;
        for (std::uint32_t y = 0; y < input.source.height; ++y) {
            for (std::uint32_t x = 0; x < input.source.width; ++x) {
                copy_pixel(source, input.source.x + x, input.source.y + y,
                           atlas.image, destination_x + x, destination_y + y);
            }
        }
        for (std::uint32_t y = 0; y < input.source.height; ++y) {
            copy_pixel(source, input.source.x, input.source.y + y, atlas.image,
                       destination_x - 1U, destination_y + y);
            copy_pixel(source, input.source.x + input.source.width - 1U,
                       input.source.y + y, atlas.image,
                       destination_x + input.source.width, destination_y + y);
        }
        for (std::uint32_t x = 0; x < input.source.width; ++x) {
            copy_pixel(source, input.source.x + x, input.source.y, atlas.image,
                       destination_x + x, destination_y - 1U);
            copy_pixel(source, input.source.x + x,
                       input.source.y + input.source.height - 1U, atlas.image,
                       destination_x + x, destination_y + input.source.height);
        }
        copy_pixel(source, input.source.x, input.source.y, atlas.image,
                   destination_x - 1U, destination_y - 1U);
        copy_pixel(source, input.source.x + input.source.width - 1U,
                   input.source.y, atlas.image,
                   destination_x + input.source.width, destination_y - 1U);
        copy_pixel(source, input.source.x,
                   input.source.y + input.source.height - 1U, atlas.image,
                   destination_x - 1U, destination_y + input.source.height);
        copy_pixel(source, input.source.x + input.source.width - 1U,
                   input.source.y + input.source.height - 1U, atlas.image,
                   destination_x + input.source.width,
                   destination_y + input.source.height);

        atlas.frames[input.index] = SpriteAtlasFrame{
            .name = frames[input.index].name.empty()
                ? "frame-" + std::to_string(input.index)
                : frames[input.index].name,
            .atlas_bounds = {destination_x, destination_y, input.source.width,
                             input.source.height},
            .source_bounds = input.source,
            .source_width = source.width,
            .source_height = source.height,
            .duration_ms = frames[input.index].duration_ms,
            .pivot = frames[input.index].pivot,
            .input_bounds = frames[input.index].input_bounds,
        };
    }
    auto png = encode_png(atlas.image);
    if (!png.has_value()) {
        return atlas_failure(SpriteAtlasErrorCode::encode_failed,
                             "cannot encode the deterministic atlas PNG");
    }
    atlas.png = std::move(*png);
    return {.atlas = std::move(atlas)};
}

} // namespace fabric::render
