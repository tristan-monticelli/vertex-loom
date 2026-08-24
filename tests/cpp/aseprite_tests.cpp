#include "fabric/render/aseprite.hpp"

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void u8(Bytes& bytes, const std::uint8_t value) {
    bytes.push_back(value);
}

void u16(Bytes& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void s16(Bytes& bytes, const std::int16_t value) {
    u16(bytes, static_cast<std::uint16_t>(value));
}

void u32(Bytes& bytes, const std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void s32(Bytes& bytes, const std::int32_t value) {
    u32(bytes, static_cast<std::uint32_t>(value));
}

void zeros(Bytes& bytes, const std::size_t count) {
    bytes.insert(bytes.end(), count, 0);
}

void string(Bytes& bytes, const std::string& value) {
    REQUIRE(value.size() <= 65'535);
    u16(bytes, static_cast<std::uint16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void patch_u32(Bytes& bytes, const std::size_t offset,
               const std::uint32_t value) {
    REQUIRE(offset + 4 <= bytes.size());
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

Bytes layer(const std::uint16_t flags, const std::uint16_t type,
            const std::uint16_t level, const std::uint8_t opacity,
            const std::string& name, const std::uint16_t blend = 0) {
    Bytes payload;
    u16(payload, flags);
    u16(payload, type);
    u16(payload, level);
    u16(payload, 0);
    u16(payload, 0);
    u16(payload, blend);
    u8(payload, opacity);
    zeros(payload, 3);
    string(payload, name);
    return chunk(0x2004, payload);
}

Bytes cel(const std::uint16_t layer_index, const std::int16_t x,
          const std::int16_t y, const std::uint8_t opacity,
          const std::uint16_t type, const std::uint16_t width,
          const std::uint16_t height, const Bytes& pixels) {
    Bytes payload;
    u16(payload, layer_index);
    s16(payload, x);
    s16(payload, y);
    u8(payload, opacity);
    u16(payload, type);
    s16(payload, 0);
    zeros(payload, 5);
    u16(payload, width);
    u16(payload, height);
    if (type == 2) {
        uLongf compressed_size = compressBound(pixels.size());
        Bytes compressed(compressed_size);
        REQUIRE(compress2(compressed.data(), &compressed_size, pixels.data(),
                          pixels.size(), Z_BEST_COMPRESSION) == Z_OK);
        compressed.resize(compressed_size);
        payload.insert(payload.end(), compressed.begin(), compressed.end());
    } else {
        payload.insert(payload.end(), pixels.begin(), pixels.end());
    }
    return chunk(0x2005, payload);
}

Bytes linked_cel(const std::uint16_t layer_index,
                 const std::uint16_t target_frame) {
    Bytes payload;
    u16(payload, layer_index);
    s16(payload, 0);
    s16(payload, 0);
    u8(payload, 255);
    u16(payload, 1);
    s16(payload, 0);
    zeros(payload, 5);
    u16(payload, target_frame);
    return chunk(0x2005, payload);
}

Bytes frame(const std::uint16_t duration,
            const std::vector<Bytes>& chunks) {
    Bytes result;
    u32(result, 0);
    u16(result, 0xf1fa);
    u16(result, static_cast<std::uint16_t>(chunks.size()));
    u16(result, duration);
    zeros(result, 2);
    u32(result, static_cast<std::uint32_t>(chunks.size()));
    for (const auto& item : chunks) {
        result.insert(result.end(), item.begin(), item.end());
    }
    patch_u32(result, 0, static_cast<std::uint32_t>(result.size()));
    return result;
}

Bytes file(const std::uint16_t width, const std::uint16_t height,
           const std::uint16_t depth, const std::vector<Bytes>& frames,
           const std::uint8_t transparent = 0) {
    Bytes result;
    u32(result, 0);
    u16(result, 0xa5e0);
    u16(result, static_cast<std::uint16_t>(frames.size()));
    u16(result, width);
    u16(result, height);
    u16(result, depth);
    u32(result, 3);
    u16(result, 100);
    zeros(result, 8);
    u8(result, transparent);
    zeros(result, 3);
    u16(result, depth == 8 ? 2 : 0);
    u8(result, 1);
    u8(result, 1);
    zeros(result, 8);
    zeros(result, 84);
    REQUIRE(result.size() == 128);
    for (const auto& item : frames) {
        result.insert(result.end(), item.begin(), item.end());
    }
    patch_u32(result, 0, static_cast<std::uint32_t>(result.size()));
    return result;
}

Bytes tags() {
    Bytes payload;
    u16(payload, 1);
    zeros(payload, 8);
    u16(payload, 0);
    u16(payload, 2);
    u8(payload, 2);
    u16(payload, 3);
    zeros(payload, 6);
    zeros(payload, 4);
    string(payload, "walk");
    return chunk(0x2018, payload);
}

Bytes slice() {
    Bytes payload;
    u32(payload, 1);
    u32(payload, 2);
    u32(payload, 0);
    string(payload, "pivot");
    u32(payload, 0);
    s32(payload, 0);
    s32(payload, 0);
    u32(payload, 2);
    u32(payload, 1);
    s32(payload, 1);
    s32(payload, 0);
    return chunk(0x2022, payload);
}

Bytes palette() {
    Bytes payload;
    u32(payload, 2);
    u32(payload, 0);
    u32(payload, 1);
    zeros(payload, 8);
    u16(payload, 0);
    u8(payload, 0);
    u8(payload, 0);
    u8(payload, 0);
    u8(payload, 0);
    u16(payload, 0);
    u8(payload, 12);
    u8(payload, 34);
    u8(payload, 56);
    u8(payload, 255);
    return chunk(0x2019, payload);
}

class TemporaryFile {
public:
    TemporaryFile(const Bytes& contents, const std::string& extension) {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        directory_ = std::filesystem::temp_directory_path() /
            ("vertex-loom-aseprite-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(directory_);
        path_ = directory_ / ("fixture" + extension);
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
        REQUIRE(output.good());
    }

    ~TemporaryFile() {
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

TEST_CASE("RGBA groups compressed and linked cels preserve animation metadata") {
    const Bytes first_pixels{255, 0, 0, 255, 0, 255, 0, 128};
    const Bytes second_pixels{0, 0, 255, 255, 0, 0, 0, 0};
    const auto source = file(
        2, 1, 32,
        {frame(80, {layer(1, 1, 0, 128, "group"),
                    layer(1, 0, 1, 255, "paint"),
                    cel(1, 0, 0, 255, 0, 2, 1, first_pixels), tags(),
                    slice()}),
         frame(120, {cel(1, 0, 0, 255, 2, 2, 1, second_pixels)}),
         frame(0, {linked_cel(1, 0)})});
    const TemporaryFile fixture(source, ".aseprite");

    const auto result = fabric::render::load_aseprite(fixture.path());

    REQUIRE(result.ok());
    REQUIRE(result.document->frames.size() == 3);
    CHECK(result.document->frames[0].duration_ms == 80);
    CHECK(result.document->frames[1].duration_ms == 120);
    CHECK(result.document->frames[2].duration_ms == 100);
    CHECK(result.document->layers.size() == 2);
    CHECK(result.document->layers[1].parent == 0);
    CHECK(result.document->frames[0].image.rgba8 ==
          Bytes{255, 0, 0, 128, 0, 255, 0, 64});
    CHECK(result.document->frames[1].image.rgba8 ==
          Bytes{0, 0, 255, 128, 0, 0, 0, 0});
    CHECK(result.document->frames[2].image.rgba8 ==
          result.document->frames[0].image.rgba8);
    REQUIRE(result.document->tags.size() == 1);
    CHECK(result.document->tags[0].name == "walk");
    CHECK(result.document->tags[0].direction ==
          fabric::render::AsepriteLoopDirection::ping_pong);
    REQUIRE(result.document->slices.size() == 1);
    REQUIRE(result.document->slices[0].keys[0].pivot.has_value());
    CHECK(*result.document->slices[0].keys[0].pivot ==
          fabric::render::AsepritePoint{1, 0});
}

TEST_CASE("grayscale and indexed sprites convert to RGBA8") {
    SECTION("grayscale") {
        const auto source = file(
            1, 1, 16,
            {frame(50, {layer(1, 0, 0, 255, "gray"),
                        cel(0, 0, 0, 255, 0, 1, 1, Bytes{80, 200})})});
        const TemporaryFile fixture(source, ".ase");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE(result.ok());
        CHECK(result.document->frames[0].image.rgba8 ==
              Bytes{80, 80, 80, 200});
    }

    SECTION("indexed palette and transparent index") {
        const auto source = file(
            2, 1, 8,
            {frame(50, {layer(1, 0, 0, 255, "indexed"), palette(),
                        cel(0, 0, 0, 255, 0, 2, 1, Bytes{1, 0})})});
        const TemporaryFile fixture(source, ".aseprite");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE(result.ok());
        CHECK(result.document->frames[0].image.rgba8 ==
              Bytes{12, 34, 56, 255, 0, 0, 0, 0});
    }
}

TEST_CASE("unsafe and unsupported Aseprite data is rejected precisely") {
    SECTION("truncated") {
        Bytes source = file(
            1, 1, 32,
            {frame(50, {layer(1, 0, 0, 255, "paint"),
                        cel(0, 0, 0, 255, 0, 1, 1,
                            Bytes{255, 0, 0, 255})})});
        source.pop_back();
        patch_u32(source, 0, static_cast<std::uint32_t>(source.size()));
        const TemporaryFile fixture(source, ".aseprite");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE_FALSE(result.ok());
        CHECK(result.error->code == fabric::render::AsepriteErrorCode::truncated);
    }

    SECTION("oversized canvas") {
        const TemporaryFile fixture(
            file(20'000, 1, 32, {frame(50, {})}), ".aseprite");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE_FALSE(result.ok());
        CHECK(result.error->code ==
              fabric::render::AsepriteErrorCode::invalid_dimensions);
    }

    SECTION("external file chunk") {
        const TemporaryFile fixture(
            file(1, 1, 32, {frame(50, {chunk(0x2008, {})})}),
            ".aseprite");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE_FALSE(result.ok());
        CHECK(result.error->code == fabric::render::AsepriteErrorCode::
                                        unsupported_external_reference);
    }

    SECTION("unsupported blend mode") {
        const TemporaryFile fixture(
            file(1, 1, 32,
                 {frame(50, {layer(1, 0, 0, 255, "multiply", 1)})}),
            ".aseprite");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE_FALSE(result.ok());
        CHECK(result.error->code ==
              fabric::render::AsepriteErrorCode::unsupported_blend_mode);
    }

    SECTION("unknown chunk") {
        const TemporaryFile fixture(
            file(1, 1, 32, {frame(50, {chunk(0x7fff, {})})}),
            ".aseprite");
        const auto result = fabric::render::load_aseprite(fixture.path());
        REQUIRE_FALSE(result.ok());
        CHECK(result.error->code ==
              fabric::render::AsepriteErrorCode::invalid_chunk);
    }
}
