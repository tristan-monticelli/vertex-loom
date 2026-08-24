#include "fabric/render/aseprite.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace fabric::render {
namespace {

constexpr std::uint16_t aseprite_magic = 0xa5e0;
constexpr std::uint16_t frame_magic = 0xf1fa;

struct ParseFailure final : std::runtime_error {
    ParseFailure(const AsepriteErrorCode value_code, const std::uint64_t value_offset,
                 std::string message)
        : std::runtime_error(std::move(message)), code(value_code),
          offset(value_offset) {}

    AsepriteErrorCode code;
    std::uint64_t offset;
};

[[noreturn]] void fail(const AsepriteErrorCode code, const std::uint64_t offset,
                       std::string message) {
    throw ParseFailure(code, offset, std::move(message));
}

class Reader {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes,
                    const std::uint64_t base = 0)
        : bytes_(bytes), base_(base) {}

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] std::uint64_t offset() const noexcept {
        return base_ + position_;
    }

    std::uint8_t u8() {
        require(1);
        return bytes_[position_++];
    }

    std::uint16_t u16() {
        require(2);
        const auto value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes_[position_]) |
            (static_cast<std::uint16_t>(bytes_[position_ + 1]) << 8U));
        position_ += 2;
        return value;
    }

    std::int16_t s16() {
        const std::uint16_t value = u16();
        if (value <= static_cast<std::uint16_t>(
                         std::numeric_limits<std::int16_t>::max())) {
            return static_cast<std::int16_t>(value);
        }
        return static_cast<std::int16_t>(
            static_cast<std::int32_t>(value) - 65'536);
    }

    std::uint32_t u32() {
        require(4);
        const auto value = static_cast<std::uint32_t>(bytes_[position_]) |
            (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[position_ + 3]) << 24U);
        position_ += 4;
        return value;
    }

    std::int32_t s32() {
        const std::uint32_t value = u32();
        if (value <= static_cast<std::uint32_t>(
                         std::numeric_limits<std::int32_t>::max())) {
            return static_cast<std::int32_t>(value);
        }
        return static_cast<std::int32_t>(
            static_cast<std::int64_t>(value) - 4'294'967'296LL);
    }

    std::span<const std::uint8_t> bytes(const std::size_t count) {
        require(count);
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

    void skip(const std::size_t count) {
        static_cast<void>(bytes(count));
    }

    Reader subreader(const std::size_t count) {
        const std::uint64_t child_base = offset();
        return Reader(bytes(count), child_base);
    }

    std::string string() {
        const std::uint16_t length = u16();
        const auto value = bytes(length);
        if (!valid_utf8(value)) {
            fail(AsepriteErrorCode::invalid_metadata, offset() - length,
                 "string is not valid UTF-8");
        }
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

private:
    static bool valid_utf8(const std::span<const std::uint8_t> value) {
        std::size_t index = 0;
        while (index < value.size()) {
            const std::uint8_t first = value[index++];
            if (first <= 0x7fU) {
                continue;
            }
            std::size_t continuation = 0;
            std::uint32_t codepoint = 0;
            if ((first & 0xe0U) == 0xc0U) {
                continuation = 1;
                codepoint = first & 0x1fU;
            } else if ((first & 0xf0U) == 0xe0U) {
                continuation = 2;
                codepoint = first & 0x0fU;
            } else if ((first & 0xf8U) == 0xf0U) {
                continuation = 3;
                codepoint = first & 0x07U;
            } else {
                return false;
            }
            if (index + continuation > value.size()) {
                return false;
            }
            for (std::size_t item = 0; item < continuation; ++item) {
                const std::uint8_t next = value[index++];
                if ((next & 0xc0U) != 0x80U) {
                    return false;
                }
                codepoint = (codepoint << 6U) | (next & 0x3fU);
            }
            const std::uint32_t minimum = continuation == 1 ? 0x80U
                : continuation == 2 ? 0x800U
                                    : 0x10000U;
            if (codepoint < minimum || codepoint > 0x10ffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
                return false;
            }
        }
        return true;
    }

    void require(const std::size_t count) const {
        if (count > remaining()) {
            fail(AsepriteErrorCode::truncated, offset(),
                 "unexpected end of Aseprite data");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
    std::uint64_t base_{};
};

struct Palette {
    std::array<std::array<std::uint8_t, 4>, 256> colors{};
    std::uint32_t size{256};

    Palette() {
        for (auto& color : colors) {
            color = {0, 0, 0, 255};
        }
    }
};

struct LayerData {
    AsepriteLayer output;
    std::uint16_t child_level{};
    bool background{};
};

struct CelData {
    std::uint32_t layer{};
    std::int32_t x{};
    std::int32_t y{};
    std::uint8_t opacity{255};
    std::uint16_t type{};
    std::uint32_t linked_frame{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

struct FrameData {
    std::uint32_t duration_ms{};
    std::vector<CelData> cels;
    Palette palette;
};

struct PremultipliedPixel {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{};
};

std::uint8_t multiply_byte(const std::uint32_t left,
                           const std::uint32_t right) {
    return static_cast<std::uint8_t>((left * right + 127U) / 255U);
}

void composite_over(PremultipliedPixel& destination,
                    const PremultipliedPixel& source) {
    const std::uint32_t inverse = 255U - source.alpha;
    destination.red = static_cast<std::uint8_t>(std::min(
        255U, static_cast<std::uint32_t>(source.red) +
                  (static_cast<std::uint32_t>(destination.red) * inverse +
                   127U) /
                      255U));
    destination.green = static_cast<std::uint8_t>(std::min(
        255U, static_cast<std::uint32_t>(source.green) +
                  (static_cast<std::uint32_t>(destination.green) * inverse +
                   127U) /
                      255U));
    destination.blue = static_cast<std::uint8_t>(std::min(
        255U, static_cast<std::uint32_t>(source.blue) +
                  (static_cast<std::uint32_t>(destination.blue) * inverse +
                   127U) /
                      255U));
    destination.alpha = static_cast<std::uint8_t>(std::min(
        255U, static_cast<std::uint32_t>(source.alpha) +
                  (static_cast<std::uint32_t>(destination.alpha) * inverse +
                   127U) /
                      255U));
}

class Parser {
public:
    explicit Parser(const std::span<const std::uint8_t> source)
        : source_(source) {}

    AsepriteDocument parse() {
        Reader reader(source_);
        const std::uint32_t declared_size = reader.u32();
        if (declared_size != source_.size()) {
            fail(AsepriteErrorCode::invalid_header, 0,
                 "header file size does not match the source length");
        }
        if (reader.u16() != aseprite_magic) {
            fail(AsepriteErrorCode::invalid_header, 4,
                 "invalid Aseprite magic number");
        }
        frame_count_ = reader.u16();
        width_ = reader.u16();
        height_ = reader.u16();
        color_depth_ = reader.u16();
        header_flags_ = reader.u32();
        speed_ = reader.u16();
        reader.skip(8);
        transparent_index_ = reader.u8();
        reader.skip(3);
        const std::uint16_t color_count = reader.u16();
        palette_.size = color_count == 0 ? 256U : color_count;
        reader.skip(2);
        reader.skip(8);
        reader.skip(84);

        validate_header();
        frames_.reserve(frame_count_);
        for (std::uint32_t frame = 0; frame < frame_count_; ++frame) {
            parse_frame(reader, frame);
        }
        if (reader.remaining() != 0) {
            fail(AsepriteErrorCode::invalid_frame, reader.offset(),
                 "bytes remain after the declared frames");
        }
        validate_references();

        AsepriteDocument result{
            .width = width_,
            .height = height_,
            .layers = {},
            .frames = {},
            .tags = std::move(tags_),
            .slices = std::move(slices_),
        };
        result.layers.reserve(layers_.size());
        for (auto& layer : layers_) {
            result.layers.push_back(std::move(layer.output));
        }
        result.frames.reserve(frames_.size());
        for (std::uint32_t frame = 0; frame < frame_count_; ++frame) {
            result.frames.push_back(AsepriteFrame{
                .image = render_frame(frame),
                .duration_ms = frames_[frame].duration_ms,
            });
        }
        return result;
    }

private:
    void validate_header() const {
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(width_) * height_;
        if (frame_count_ == 0 || frame_count_ > maximum_aseprite_frames) {
            fail(AsepriteErrorCode::invalid_header, 6,
                 "frame count is outside the supported range");
        }
        if (width_ == 0 || height_ == 0 ||
            width_ > maximum_raster_dimension ||
            height_ > maximum_raster_dimension ||
            pixels > maximum_raster_pixels) {
            fail(AsepriteErrorCode::invalid_dimensions, 8,
                 "canvas dimensions exceed the raster safety limits");
        }
        if (color_depth_ != 8 && color_depth_ != 16 && color_depth_ != 32) {
            fail(AsepriteErrorCode::unsupported_color_depth, 12,
                 "color depth must be indexed, grayscale or RGBA");
        }
        if (palette_.size == 0 || palette_.size > 256) {
            fail(AsepriteErrorCode::invalid_palette, 32,
                 "indexed palette contains more than 256 entries");
        }
    }

    void parse_frame(Reader& source, const std::uint32_t frame_index) {
        const std::uint64_t frame_offset = source.offset();
        const std::uint32_t frame_size = source.u32();
        if (frame_size < 16) {
            fail(AsepriteErrorCode::invalid_frame, frame_offset,
                 "frame size must include its 16-byte header");
        }
        Reader frame = source.subreader(frame_size - 4U);
        if (frame.u16() != frame_magic) {
            fail(AsepriteErrorCode::invalid_frame, frame_offset + 4U,
                 "invalid frame magic number");
        }
        const std::uint16_t old_chunk_count = frame.u16();
        const std::uint16_t duration = frame.u16();
        frame.skip(2);
        const std::uint32_t new_chunk_count = frame.u32();
        const std::uint32_t chunk_count = new_chunk_count != 0
            ? new_chunk_count
            : old_chunk_count;
        if (chunk_count > (frame.remaining() / 6U)) {
            fail(AsepriteErrorCode::invalid_frame, frame.offset(),
                 "chunk count exceeds the frame bounds");
        }

        FrameData parsed{
            .duration_ms = duration == 0 ? speed_ : duration,
            .palette = palette_,
        };
        for (std::uint32_t chunk_index = 0; chunk_index < chunk_count;
             ++chunk_index) {
            const std::uint64_t chunk_offset = frame.offset();
            const std::uint32_t chunk_size = frame.u32();
            if (chunk_size < 6) {
                fail(AsepriteErrorCode::invalid_chunk, chunk_offset,
                     "chunk size must be at least 6 bytes");
            }
            Reader chunk = frame.subreader(chunk_size - 4U);
            const std::uint16_t type = chunk.u16();
            parse_chunk(chunk, type, frame_index, parsed, chunk_offset);
            if (chunk.remaining() != 0) {
                fail(AsepriteErrorCode::invalid_chunk, chunk.offset(),
                     "chunk contains unexpected trailing data");
            }
        }
        if (frame.remaining() != 0) {
            fail(AsepriteErrorCode::invalid_frame, frame.offset(),
                 "frame contains bytes outside its declared chunks");
        }
        palette_ = parsed.palette;
        frames_.push_back(std::move(parsed));
    }

    void parse_chunk(Reader& chunk, const std::uint16_t type,
                     const std::uint32_t frame_index, FrameData& frame,
                     const std::uint64_t chunk_offset) {
        switch (type) {
        case 0x0004: parse_old_palette(chunk, frame.palette, false); break;
        case 0x0011: parse_old_palette(chunk, frame.palette, true); break;
        case 0x2004:
            if (frame_index != 0) {
                fail(AsepriteErrorCode::invalid_metadata, chunk_offset,
                     "layer chunks are only supported in the first frame");
            }
            parse_layer(chunk);
            break;
        case 0x2005: parse_cel(chunk, frame_index, frame); break;
        case 0x2006: parse_cel_extra(chunk); break;
        case 0x2007: parse_color_profile(chunk); break;
        case 0x2008:
            fail(AsepriteErrorCode::unsupported_external_reference,
                 chunk_offset, "external file references are not supported");
        case 0x2016:
            chunk.skip(chunk.remaining());
            break;
        case 0x2017:
            fail(AsepriteErrorCode::invalid_chunk, chunk_offset,
                 "path chunks are not supported");
        case 0x2018: parse_tags(chunk); break;
        case 0x2019: parse_palette(chunk, frame.palette); break;
        case 0x2020:
            chunk.skip(chunk.remaining());
            break;
        case 0x2022: parse_slice(chunk); break;
        case 0x2023:
            fail(AsepriteErrorCode::unsupported_tilemap, chunk_offset,
                 "tileset chunks are not supported");
        default:
            fail(AsepriteErrorCode::invalid_chunk, chunk_offset,
                 "unsupported Aseprite chunk type " +
                     std::to_string(type));
        }
    }

    void parse_old_palette(Reader& chunk, Palette& palette,
                           const bool six_bit) {
        const std::uint16_t packet_count = chunk.u16();
        std::uint32_t cursor = 0;
        for (std::uint16_t packet = 0; packet < packet_count; ++packet) {
            cursor += chunk.u8();
            const std::uint8_t encoded_count = chunk.u8();
            const std::uint32_t count = encoded_count == 0 ? 256U
                                                            : encoded_count;
            if (cursor + count > 256U) {
                fail(AsepriteErrorCode::invalid_palette, chunk.offset(),
                     "old palette packet exceeds 256 entries");
            }
            for (std::uint32_t index = 0; index < count; ++index) {
                auto convert = [six_bit, &chunk](const std::uint8_t value) {
                    if (!six_bit) {
                        return value;
                    }
                    if (value > 63) {
                        fail(AsepriteErrorCode::invalid_palette,
                             chunk.offset() - 1U,
                             "six-bit palette value exceeds 63");
                    }
                    return static_cast<std::uint8_t>(
                        (static_cast<std::uint32_t>(value) * 255U + 31U) /
                        63U);
                };
                palette.colors[cursor++] = {
                    convert(chunk.u8()), convert(chunk.u8()),
                    convert(chunk.u8()), 255};
            }
        }
    }

    void parse_palette(Reader& chunk, Palette& palette) {
        const std::uint32_t size = chunk.u32();
        const std::uint32_t first = chunk.u32();
        const std::uint32_t last = chunk.u32();
        chunk.skip(8);
        if (size == 0 || size > 256 || first > last || last >= size) {
            fail(AsepriteErrorCode::invalid_palette, chunk.offset(),
                 "new palette range is invalid");
        }
        palette.size = size;
        for (std::uint32_t index = first; index <= last; ++index) {
            const std::uint16_t flags = chunk.u16();
            palette.colors[index] = {
                chunk.u8(), chunk.u8(), chunk.u8(), chunk.u8()};
            if ((flags & 1U) != 0) {
                static_cast<void>(chunk.string());
            }
        }
    }

    void parse_layer(Reader& chunk) {
        const std::uint16_t flags = chunk.u16();
        const std::uint16_t type = chunk.u16();
        const std::uint16_t child_level = chunk.u16();
        chunk.skip(4);
        const std::uint16_t blend_mode = chunk.u16();
        std::uint8_t opacity = chunk.u8();
        chunk.skip(3);
        std::string name = chunk.string();
        if (type == 2) {
            fail(AsepriteErrorCode::unsupported_tilemap, chunk.offset(),
                 "tilemap layers are not supported");
        }
        if (type > 2) {
            fail(AsepriteErrorCode::invalid_chunk, chunk.offset(),
                 "layer type is invalid");
        }
        if (blend_mode != 0) {
            fail(AsepriteErrorCode::unsupported_blend_mode, chunk.offset(),
                 "only normal layer blend mode is supported");
        }
        const bool group = type == 1;
        const bool opacity_is_valid = group ? (header_flags_ & 2U) != 0
                                            : (header_flags_ & 1U) != 0;
        if (!opacity_is_valid) {
            opacity = 255;
        }
        std::optional<std::uint32_t> parent;
        if (child_level != 0) {
            if (layers_.empty() ||
                child_level > layers_.back().child_level + 1U) {
                fail(AsepriteErrorCode::invalid_metadata, chunk.offset(),
                     "layer hierarchy skips a parent level");
            }
            for (std::size_t index = layers_.size(); index > 0; --index) {
                if (layers_[index - 1].child_level + 1U == child_level) {
                    if (!layers_[index - 1].output.group) {
                        fail(AsepriteErrorCode::invalid_metadata,
                             chunk.offset(),
                             "layer parent must be a group");
                    }
                    parent = static_cast<std::uint32_t>(index - 1);
                    break;
                }
            }
            if (!parent.has_value()) {
                fail(AsepriteErrorCode::invalid_metadata, chunk.offset(),
                     "layer parent is missing");
            }
        }
        if ((header_flags_ & 4U) != 0) {
            chunk.skip(16);
        }
        layers_.push_back(LayerData{
            .output = AsepriteLayer{
                .name = std::move(name),
                .parent = parent,
                .group = group,
                .visible = (flags & 1U) != 0,
                .opacity = opacity,
            },
            .child_level = child_level,
            .background = (flags & 8U) != 0,
        });
    }

    void parse_cel(Reader& chunk, const std::uint32_t frame_index,
                   FrameData& frame) {
        CelData cel;
        cel.layer = chunk.u16();
        cel.x = chunk.s16();
        cel.y = chunk.s16();
        cel.opacity = chunk.u8();
        cel.type = chunk.u16();
        const std::int16_t z_index = chunk.s16();
        chunk.skip(5);
        if (z_index != 0) {
            fail(AsepriteErrorCode::unsupported_z_index, chunk.offset(),
                 "non-zero cel z-index is not supported");
        }
        if (cel.type == 1) {
            cel.linked_frame = chunk.u16();
            if (cel.linked_frame >= frame_index) {
                fail(AsepriteErrorCode::invalid_reference, chunk.offset(),
                     "linked cel must reference an earlier frame");
            }
        } else if (cel.type == 0 || cel.type == 2) {
            cel.width = chunk.u16();
            cel.height = chunk.u16();
            const std::uint64_t pixel_count =
                static_cast<std::uint64_t>(cel.width) * cel.height;
            if (cel.width == 0 || cel.height == 0 ||
                cel.width > maximum_raster_dimension ||
                cel.height > maximum_raster_dimension ||
                pixel_count > maximum_raster_pixels) {
                fail(AsepriteErrorCode::invalid_dimensions, chunk.offset(),
                     "cel dimensions exceed the raster safety limits");
            }
            const std::size_t bytes_per_pixel = color_depth_ / 8U;
            const std::uint64_t expected64 = pixel_count * bytes_per_pixel;
            if (expected64 > std::numeric_limits<std::size_t>::max()) {
                fail(AsepriteErrorCode::invalid_dimensions, chunk.offset(),
                     "cel byte size overflows the platform size type");
            }
            const std::size_t expected = static_cast<std::size_t>(expected64);
            if (cel.type == 0) {
                const auto pixels = chunk.bytes(expected);
                cel.pixels.assign(pixels.begin(), pixels.end());
            } else {
                cel.pixels = inflate_exact(chunk.bytes(chunk.remaining()),
                                           expected, chunk.offset());
            }
        } else if (cel.type == 3) {
            fail(AsepriteErrorCode::unsupported_tilemap, chunk.offset(),
                 "compressed tilemap cels are not supported");
        } else {
            fail(AsepriteErrorCode::invalid_chunk, chunk.offset(),
                 "cel type is invalid");
        }
        if (std::ranges::any_of(frame.cels, [&cel](const CelData& existing) {
                return existing.layer == cel.layer;
            })) {
            fail(AsepriteErrorCode::invalid_frame, chunk.offset(),
                 "frame contains multiple cels for one layer");
        }
        frame.cels.push_back(std::move(cel));
    }

    static std::vector<std::uint8_t> inflate_exact(
        const std::span<const std::uint8_t> compressed,
        const std::size_t expected, const std::uint64_t offset) {
        if (compressed.empty() || compressed.size() >
                                      std::numeric_limits<uInt>::max() ||
            expected > std::numeric_limits<uInt>::max()) {
            fail(AsepriteErrorCode::inflate_failed, offset,
                 "compressed cel size is unsupported");
        }
        std::vector<std::uint8_t> output(expected);
        z_stream stream{};
        stream.next_in = const_cast<Bytef*>(compressed.data());
        stream.avail_in = static_cast<uInt>(compressed.size());
        stream.next_out = output.data();
        stream.avail_out = static_cast<uInt>(output.size());
        if (inflateInit(&stream) != Z_OK) {
            fail(AsepriteErrorCode::inflate_failed, offset,
                 "cannot initialize zlib decompression");
        }
        const int status = inflate(&stream, Z_FINISH);
        const bool valid = status == Z_STREAM_END && stream.avail_in == 0 &&
            stream.total_out == expected;
        inflateEnd(&stream);
        if (!valid) {
            fail(AsepriteErrorCode::inflate_failed, offset,
                 "compressed cel does not decode to its declared size");
        }
        return output;
    }

    static void parse_cel_extra(Reader& chunk) {
        const std::uint32_t flags = chunk.u32();
        chunk.skip(16);
        chunk.skip(16);
        if (flags != 0) {
            fail(AsepriteErrorCode::invalid_chunk, chunk.offset(),
                 "precise cel bounds are not supported");
        }
    }

    static void parse_color_profile(Reader& chunk) {
        const std::uint16_t type = chunk.u16();
        chunk.skip(2);
        chunk.skip(4);
        chunk.skip(8);
        if (type > 2) {
            fail(AsepriteErrorCode::invalid_chunk, chunk.offset(),
                 "color profile type is invalid");
        }
        if (type == 2) {
            const std::uint32_t length = chunk.u32();
            chunk.skip(length);
        }
    }

    void parse_tags(Reader& chunk) {
        const std::uint16_t count = chunk.u16();
        chunk.skip(8);
        for (std::uint16_t index = 0; index < count; ++index) {
            AsepriteTag tag;
            tag.from_frame = chunk.u16();
            tag.to_frame = chunk.u16();
            const std::uint8_t direction = chunk.u8();
            if (direction > 3) {
                fail(AsepriteErrorCode::invalid_metadata, chunk.offset(),
                     "tag loop direction is invalid");
            }
            tag.direction = static_cast<AsepriteLoopDirection>(direction);
            tag.repeat = chunk.u16();
            chunk.skip(6);
            chunk.skip(4);
            tag.name = chunk.string();
            tags_.push_back(std::move(tag));
        }
    }

    void parse_slice(Reader& chunk) {
        const std::uint32_t key_count = chunk.u32();
        const std::uint32_t flags = chunk.u32();
        chunk.skip(4);
        if ((flags & ~3U) != 0 || key_count > maximum_aseprite_frames) {
            fail(AsepriteErrorCode::invalid_metadata, chunk.offset(),
                 "slice flags or key count are invalid");
        }
        AsepriteSlice slice{.name = chunk.string()};
        slice.keys.reserve(key_count);
        for (std::uint32_t index = 0; index < key_count; ++index) {
            AsepriteSliceKey key{
                .frame = chunk.u32(),
                .bounds = {
                    .x = chunk.s32(),
                    .y = chunk.s32(),
                    .width = chunk.u32(),
                    .height = chunk.u32(),
                },
            };
            if ((flags & 1U) != 0) {
                key.center = AsepriteBounds{
                    .x = chunk.s32(),
                    .y = chunk.s32(),
                    .width = chunk.u32(),
                    .height = chunk.u32(),
                };
            }
            if ((flags & 2U) != 0) {
                key.pivot = AsepritePoint{chunk.s32(), chunk.s32()};
            }
            slice.keys.push_back(std::move(key));
        }
        slices_.push_back(std::move(slice));
    }

    void validate_references() const {
        if (layers_.empty()) {
            fail(AsepriteErrorCode::invalid_metadata, 128,
                 "sprite contains no layers");
        }
        for (std::uint32_t frame_index = 0; frame_index < frames_.size();
             ++frame_index) {
            for (const auto& cel : frames_[frame_index].cels) {
                if (cel.layer >= layers_.size() || layers_[cel.layer].output.group) {
                    fail(AsepriteErrorCode::invalid_reference, 128,
                         "cel references a missing or group layer");
                }
                if (cel.type == 1) {
                    static_cast<void>(resolve_cel(frame_index, cel.layer));
                }
            }
        }
        for (const auto& tag : tags_) {
            if (tag.from_frame > tag.to_frame || tag.to_frame >= frame_count_) {
                fail(AsepriteErrorCode::invalid_metadata, 128,
                     "tag frame range is outside the sprite");
            }
        }
        for (const auto& slice : slices_) {
            std::uint32_t previous = 0;
            bool first = true;
            for (const auto& key : slice.keys) {
                if (key.frame >= frame_count_ || (!first && key.frame <= previous)) {
                    fail(AsepriteErrorCode::invalid_metadata, 128,
                         "slice keys must use increasing valid frames");
                }
                previous = key.frame;
                first = false;
            }
        }
    }

    const CelData* find_cel(const std::uint32_t frame,
                            const std::uint32_t layer) const {
        const auto iterator = std::ranges::find_if(
            frames_[frame].cels,
            [layer](const CelData& cel) { return cel.layer == layer; });
        return iterator == frames_[frame].cels.end() ? nullptr : &*iterator;
    }

    const CelData& resolve_cel(const std::uint32_t frame,
                               const std::uint32_t layer) const {
        const CelData* cel = find_cel(frame, layer);
        if (cel == nullptr) {
            fail(AsepriteErrorCode::invalid_reference, 128,
                 "linked cel target is missing");
        }
        std::uint32_t remaining_links = frame_count_;
        while (cel->type == 1) {
            if (remaining_links-- == 0 || cel->linked_frame >= frame) {
                fail(AsepriteErrorCode::invalid_reference, 128,
                     "linked cel chain is cyclic or future-facing");
            }
            cel = find_cel(cel->linked_frame, layer);
            if (cel == nullptr) {
                fail(AsepriteErrorCode::invalid_reference, 128,
                     "linked cel target is missing");
            }
        }
        return *cel;
    }

    std::vector<PremultipliedPixel> render_layer(
        const std::uint32_t frame_index, const std::uint32_t layer_index) const {
        const auto canvas_size = static_cast<std::size_t>(width_) * height_;
        std::vector<PremultipliedPixel> canvas(canvas_size);
        const auto& layer = layers_[layer_index];
        if (!layer.output.visible) {
            return canvas;
        }
        if (layer.output.group) {
            for (std::uint32_t child = layer_index + 1; child < layers_.size();
                 ++child) {
                if (layers_[child].output.parent != layer_index) {
                    continue;
                }
                auto child_pixels = render_layer(frame_index, child);
                for (std::size_t pixel = 0; pixel < canvas.size(); ++pixel) {
                    composite_over(canvas[pixel], child_pixels[pixel]);
                }
            }
        } else if (const CelData* placed = find_cel(frame_index, layer_index);
                   placed != nullptr) {
            const CelData& source = placed->type == 1
                ? resolve_cel(frame_index, layer_index)
                : *placed;
            draw_cel(canvas, frames_[frame_index].palette, layer, *placed,
                     source);
        }
        if (layer.output.opacity != 255) {
            for (auto& pixel : canvas) {
                pixel.red = multiply_byte(pixel.red, layer.output.opacity);
                pixel.green = multiply_byte(pixel.green, layer.output.opacity);
                pixel.blue = multiply_byte(pixel.blue, layer.output.opacity);
                pixel.alpha = multiply_byte(pixel.alpha, layer.output.opacity);
            }
        }
        return canvas;
    }

    void draw_cel(std::vector<PremultipliedPixel>& canvas,
                  const Palette& palette, const LayerData& layer,
                  const CelData& placed, const CelData& source) const {
        const std::size_t bytes_per_pixel = color_depth_ / 8U;
        for (std::uint32_t y = 0; y < source.height; ++y) {
            const std::int64_t destination_y =
                static_cast<std::int64_t>(placed.y) + y;
            if (destination_y < 0 || destination_y >= height_) {
                continue;
            }
            for (std::uint32_t x = 0; x < source.width; ++x) {
                const std::int64_t destination_x =
                    static_cast<std::int64_t>(placed.x) + x;
                if (destination_x < 0 || destination_x >= width_) {
                    continue;
                }
                const std::size_t source_index =
                    (static_cast<std::size_t>(y) * source.width + x) *
                    bytes_per_pixel;
                std::array<std::uint8_t, 4> color{};
                if (color_depth_ == 32) {
                    color = {source.pixels[source_index],
                             source.pixels[source_index + 1],
                             source.pixels[source_index + 2],
                             source.pixels[source_index + 3]};
                } else if (color_depth_ == 16) {
                    color = {source.pixels[source_index],
                             source.pixels[source_index],
                             source.pixels[source_index],
                             source.pixels[source_index + 1]};
                } else {
                    const std::uint8_t palette_index =
                        source.pixels[source_index];
                    if (palette_index >= palette.size) {
                        fail(AsepriteErrorCode::invalid_palette, 128,
                             "indexed cel references a missing palette entry");
                    }
                    color = palette.colors[palette_index];
                    if (!layer.background && palette_index == transparent_index_) {
                        color[3] = 0;
                    }
                }
                color[3] = multiply_byte(color[3], placed.opacity);
                const PremultipliedPixel pixel{
                    .red = multiply_byte(color[0], color[3]),
                    .green = multiply_byte(color[1], color[3]),
                    .blue = multiply_byte(color[2], color[3]),
                    .alpha = color[3],
                };
                composite_over(
                    canvas[static_cast<std::size_t>(destination_y) * width_ +
                           static_cast<std::size_t>(destination_x)],
                    pixel);
            }
        }
    }

    RasterImage render_frame(const std::uint32_t frame_index) const {
        const auto canvas_size = static_cast<std::size_t>(width_) * height_;
        std::vector<PremultipliedPixel> composed(canvas_size);
        for (std::uint32_t layer = 0; layer < layers_.size(); ++layer) {
            if (layers_[layer].output.parent.has_value()) {
                continue;
            }
            auto pixels = render_layer(frame_index, layer);
            for (std::size_t pixel = 0; pixel < composed.size(); ++pixel) {
                composite_over(composed[pixel], pixels[pixel]);
            }
        }
        RasterImage image{
            .width = width_,
            .height = height_,
            .rgba8 = std::vector<std::uint8_t>(canvas_size * 4U),
        };
        for (std::size_t index = 0; index < composed.size(); ++index) {
            const auto& source = composed[index];
            const std::size_t destination = index * 4U;
            if (source.alpha == 0) {
                continue;
            }
            image.rgba8[destination] = static_cast<std::uint8_t>(std::min(
                255U, (static_cast<std::uint32_t>(source.red) * 255U +
                       source.alpha / 2U) /
                          source.alpha));
            image.rgba8[destination + 1] = static_cast<std::uint8_t>(std::min(
                255U, (static_cast<std::uint32_t>(source.green) * 255U +
                       source.alpha / 2U) /
                          source.alpha));
            image.rgba8[destination + 2] = static_cast<std::uint8_t>(std::min(
                255U, (static_cast<std::uint32_t>(source.blue) * 255U +
                       source.alpha / 2U) /
                          source.alpha));
            image.rgba8[destination + 3] = source.alpha;
        }
        return image;
    }

    std::span<const std::uint8_t> source_;
    std::uint32_t frame_count_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint16_t color_depth_{};
    std::uint32_t header_flags_{};
    std::uint16_t speed_{};
    std::uint8_t transparent_index_{};
    Palette palette_;
    std::vector<LayerData> layers_;
    std::vector<FrameData> frames_;
    std::vector<AsepriteTag> tags_;
    std::vector<AsepriteSlice> slices_;
};

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
    return extension;
}

AsepriteResult error_result(const AsepriteErrorCode code,
                            const std::uint64_t offset, std::string message) {
    return {.error = AsepriteError{code, offset, std::move(message)}};
}

} // namespace

std::string_view to_string(const AsepriteErrorCode code) noexcept {
    switch (code) {
    case AsepriteErrorCode::invalid_extension: return "invalid_extension";
    case AsepriteErrorCode::io_error: return "io_error";
    case AsepriteErrorCode::source_too_large: return "source_too_large";
    case AsepriteErrorCode::truncated: return "truncated";
    case AsepriteErrorCode::invalid_header: return "invalid_header";
    case AsepriteErrorCode::invalid_dimensions: return "invalid_dimensions";
    case AsepriteErrorCode::unsupported_color_depth:
        return "unsupported_color_depth";
    case AsepriteErrorCode::invalid_frame: return "invalid_frame";
    case AsepriteErrorCode::invalid_chunk: return "invalid_chunk";
    case AsepriteErrorCode::unsupported_external_reference:
        return "unsupported_external_reference";
    case AsepriteErrorCode::unsupported_tilemap: return "unsupported_tilemap";
    case AsepriteErrorCode::unsupported_blend_mode:
        return "unsupported_blend_mode";
    case AsepriteErrorCode::unsupported_z_index:
        return "unsupported_z_index";
    case AsepriteErrorCode::inflate_failed: return "inflate_failed";
    case AsepriteErrorCode::invalid_reference: return "invalid_reference";
    case AsepriteErrorCode::invalid_palette: return "invalid_palette";
    case AsepriteErrorCode::invalid_metadata: return "invalid_metadata";
    }
    return "unknown_error";
}

AsepriteResult load_aseprite(const std::filesystem::path& path) {
    const std::string extension = lowercase_extension(path);
    if (extension != ".ase" && extension != ".aseprite") {
        return error_result(AsepriteErrorCode::invalid_extension, 0,
                            "source file must use .ase or .aseprite");
    }
    std::error_code filesystem_error;
    const std::uintmax_t source_size =
        std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error) {
        return error_result(AsepriteErrorCode::io_error, 0,
                            "cannot inspect the Aseprite source");
    }
    if (source_size > maximum_aseprite_source_bytes) {
        return error_result(AsepriteErrorCode::source_too_large, 0,
                            "Aseprite source exceeds the 256 MiB limit");
    }
    if (source_size < 128) {
        return error_result(AsepriteErrorCode::truncated, source_size,
                            "Aseprite source is shorter than its header");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return error_result(AsepriteErrorCode::io_error, 0,
                            "cannot open the Aseprite source");
    }
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(source_size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return error_result(AsepriteErrorCode::io_error, 0,
                            "cannot read the complete Aseprite source");
    }
    try {
        return {.document = Parser(bytes).parse()};
    } catch (const ParseFailure& failure) {
        return error_result(failure.code, failure.offset, failure.what());
    } catch (const std::bad_alloc&) {
        return error_result(AsepriteErrorCode::source_too_large, 0,
                            "Aseprite source exceeds available memory");
    }
}

} // namespace fabric::render
