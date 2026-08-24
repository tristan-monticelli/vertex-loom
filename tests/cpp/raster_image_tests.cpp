#include "fabric/render/raster_image.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::filesystem::path temporary_path(const std::string_view extension) {
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    return std::filesystem::temp_directory_path() /
           ("fabric-raster-test-" + std::to_string(unique) +
            std::string(extension));
}

void write_bytes(const std::filesystem::path& path,
                 const std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void valid_png_is_decoded_to_rgba8() {
    constexpr std::array<std::uint8_t, 68> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00,
        0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00,
        0x02, 0xeb, 0x01, 0xf5, 0x69, 0x76, 0x9d, 0x7b, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    const auto path = temporary_path(".png");
    write_bytes(path, png);
    const auto loaded = fabric::render::load_png(path);
    std::filesystem::remove(path);

    require(loaded.ok(), "valid PNG did not decode");
    require(loaded.image->width == 1 && loaded.image->height == 1,
            "decoded PNG has incorrect dimensions");
    require(loaded.image->rgba8.size() == 4,
            "decoded PNG is not RGBA8");
}

void invalid_inputs_are_rejected() {
    const auto wrong_extension = fabric::render::load_png("texture.jpg");
    require(!wrong_extension.ok() &&
                wrong_extension.error->code ==
                    fabric::render::RasterErrorCode::invalid_extension,
            "wrong extension was accepted");

    const auto path = temporary_path(".png");
    constexpr std::array<std::uint8_t, 4> corrupt{0x89, 0x50, 0x4e, 0x47};
    write_bytes(path, corrupt);
    const auto decoded = fabric::render::load_png(path);
    std::filesystem::remove(path);
    require(!decoded.ok() &&
                decoded.error->code == fabric::render::RasterErrorCode::decode_failed,
            "corrupt PNG was accepted");

    const auto oversized_path = temporary_path(".png");
    constexpr std::array<std::uint8_t, 24> oversized_header{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x01,
    };
    write_bytes(oversized_path, oversized_header);
    const auto oversized = fabric::render::load_png(oversized_path);
    std::filesystem::remove(oversized_path);
    require(!oversized.ok() &&
                oversized.error->code ==
                    fabric::render::RasterErrorCode::invalid_dimensions,
            "oversized PNG reached the decoder");
}

} // namespace

int main() {
    valid_png_is_decoded_to_rgba8();
    invalid_inputs_are_rejected();
    return 0;
}
