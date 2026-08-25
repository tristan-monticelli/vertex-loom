#include "fabric/render/raster_image.hpp"
#include "fabric/render/vector_geometry.hpp"

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

void valid_svg_is_rasterized_to_a_bounded_preview() {
    const auto path = temporary_path(".svg");
    {
        std::ofstream output(path, std::ios::binary);
        output << R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="2" viewBox="0 0 4 2"><rect width="4" height="2" fill="#d9a441"/></svg>)";
    }
    const auto loaded = fabric::render::load_svg_preview(path);
    std::filesystem::remove(path);

    require(loaded.ok(), "valid SVG did not rasterize");
    require(loaded.image->width == 2048 && loaded.image->height == 1024,
            "SVG preview did not preserve its aspect ratio");
    require(loaded.image->rgba8.size() ==
                static_cast<std::size_t>(loaded.image->width) *
                    loaded.image->height * 4U,
            "SVG preview is not RGBA8");
}

void invalid_svg_inputs_are_rejected_before_publication() {
    const auto wrong_extension = fabric::render::load_svg_preview("vector.png");
    require(!wrong_extension.ok() &&
                wrong_extension.error->code ==
                    fabric::render::RasterErrorCode::invalid_extension,
            "wrong SVG extension was accepted");

    const auto corrupt_path = temporary_path(".svg");
    {
        std::ofstream output(corrupt_path, std::ios::binary);
        output << "not an svg";
    }
    const auto corrupt = fabric::render::load_svg_preview(corrupt_path);
    std::filesystem::remove(corrupt_path);
    require(!corrupt.ok() &&
                corrupt.error->code ==
                    fabric::render::RasterErrorCode::decode_failed,
            "corrupt SVG was accepted");

    const auto oversized_path = temporary_path(".svg");
    {
        std::ofstream output(oversized_path, std::ios::binary);
        const std::string oversized(
            fabric::render::maximum_svg_source_bytes + 1U, ' ');
        output << oversized;
    }
    const auto oversized = fabric::render::load_svg_preview(oversized_path);
    std::filesystem::remove(oversized_path);
    require(!oversized.ok() &&
                oversized.error->code ==
                    fabric::render::RasterErrorCode::source_too_large,
            "oversized SVG reached the decoder");
}

fabric::project::VectorAsset native_geometry_fixture() {
    fabric::project::VectorAsset asset{
        .source_kind = fabric::project::VectorSourceKind::native,
        .native = fabric::project::NativeVectorDefinition{
            .size = {10.0F, 10.0F},
            .nodes = {{
                .id = "node-1",
                .name = "Triangle",
                .shape = {
                    .id = "shape-1",
                    .kind = fabric::project::VectorShapeKind::path,
                    .bounds = {.origin = {0.0F, 0.0F}, .size = {10.0F, 10.0F}},
                    .path = {
                        {.kind = fabric::project::VectorPathCommandKind::move,
                         .point = {0.0F, 0.0F}},
                        {.kind = fabric::project::VectorPathCommandKind::line,
                         .point = {10.0F, 0.0F}},
                        {.kind = fabric::project::VectorPathCommandKind::line,
                         .point = {5.0F, 10.0F}},
                        {.kind = fabric::project::VectorPathCommandKind::close},
                    },
                },
                .fill = {.kind = fabric::project::VectorFillKind::solid,
                         .color = fabric::core::Color{1.0F, 0.0F, 0.0F, 1.0F}},
            }},
        },
    };
    return asset;
}

void native_geometry_produces_deterministic_packets() {
    const auto first = fabric::render::build_native_draw_packets(
        native_geometry_fixture());
    const auto second = fabric::render::build_native_draw_packets(
        native_geometry_fixture());
    require(first.ok() && second.ok(), "native geometry packet build failed");
    require(first.packets == second.packets,
            "native geometry packets were not deterministic");
    require(first.packets.size() == 1U &&
                first.packets.front().fill_indices.size() == 3U,
            "triangle did not produce one deterministic fill triangle");
}

void native_geometry_cache_invalidates_on_document_or_tolerance_change() {
    fabric::render::VectorGeometryCache cache;
    auto asset = native_geometry_fixture();
    const auto first = cache.get_or_build(asset);
    const auto second = cache.get_or_build(asset);
    require(first.packets == second.packets && cache.size() == 1U,
            "vector geometry cache did not reuse the document version");

    asset.native->nodes.front().fill.color->green = 0.5F;
    const auto changed = cache.get_or_build(asset);
    require(changed.packets != first.packets && cache.size() == 2U,
            "vector geometry cache did not invalidate changed document data");

    static_cast<void>(cache.get_or_build(asset, 0.125F));
    require(cache.size() == 3U,
            "vector geometry cache did not separate curve tolerances");
}

} // namespace

int main() {
    valid_png_is_decoded_to_rgba8();
    invalid_inputs_are_rejected();
    valid_svg_is_rasterized_to_a_bounded_preview();
    invalid_svg_inputs_are_rejected_before_publication();
    native_geometry_produces_deterministic_packets();
    native_geometry_cache_invalidates_on_document_or_tolerance_change();
    return 0;
}
