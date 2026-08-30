#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/render/vector_geometry.hpp"
#include "fabric/render/opengl_vector_renderer.hpp"

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

void svg_can_be_converted_to_native_paths() {
    const auto path = temporary_path(".svg");
    {
        std::ofstream output(path, std::ios::binary);
        output << R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="2" viewBox="0 0 4 2"><path id="panel" d="M0 0h4v2H0z" fill="#d9a441" stroke="#112233" stroke-width="0.5"/></svg>)";
    }
    const auto converted = fabric::render::convert_svg_to_native(
        path, fabric::core::ResourceId{.value = "panel-native"}, "Panel");
    std::filesystem::remove(path);

    require(converted.ok() && converted.asset->native.has_value(),
            "SVG native conversion failed");
    require(converted.asset->native->size == fabric::core::Vec2{4.0F, 2.0F} &&
                converted.asset->native->nodes.size() == 1U,
            "SVG native conversion lost document or node dimensions");
    const auto& node = converted.asset->native->nodes.front();
    require(node.name == "panel" && node.fill.kind ==
                fabric::project::VectorFillKind::solid && node.stroke.has_value() &&
                node.shape.path.size() >= 2U,
            "SVG native conversion lost path style or commands");
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
                first.packets.front().fill_indices.size() == 3U &&
                first.packets.front().closed_outline,
            "triangle did not produce one deterministic fill triangle");
}

void raster_views_produce_shared_deterministic_packets() {
    const fabric::project::ResourceReference texture{
        {.value = "woven-source"}, "texture"};
    const fabric::project::RasterView view{
        .crop = {{2.0F, 0.0F}, {2.0F, 2.0F}},
        .pivot = {0.25F, 0.5F},
        .transform = {.position = {1.0F, 2.0F}, .scale = {2.0F, 1.0F}},
        .filter = fabric::project::RasterFilter::nearest,
    };
    const fabric::render::RasterViewPacketInput input{
        .node_id = "cropped",
        .texture = texture,
        .source_width = 4U,
        .source_height = 2U,
        .pixels_per_unit = 2.0F,
        .view = view,
    };
    const auto first = fabric::render::build_raster_view_draw_packets(input);
    const auto second = fabric::render::build_raster_view_draw_packets(input);
    require(first.ok() && second.ok() && first.packets == second.packets &&
                first.errors == second.errors && first.packets.size() == 1U,
            "raster view packet was not deterministic");
    const auto& packet = first.packets.front();
    require(packet.fill_vertices == std::vector<fabric::core::Vec2>{
                {0.5F, 1.5F}, {2.5F, 1.5F},
                {2.5F, 2.5F}, {0.5F, 2.5F}} &&
                packet.fill_uv == std::vector<fabric::core::Vec2>{
                    {0.5F, 0.0F}, {1.0F, 0.0F},
                    {1.0F, 1.0F}, {0.5F, 1.0F}},
            "raster crop geometry or UV coordinates changed");

    const auto historical = fabric::render::build_raster_view_draw_packets({
        .node_id = "historical",
        .texture = texture,
        .source_width = 4U,
        .source_height = 2U,
        .pixels_per_unit = 2.0F,
    });
    require(historical.ok() && historical.packets.front().fill_uv ==
                std::vector<fabric::core::Vec2>{{0.0F, 0.0F}, {1.0F, 0.0F},
                                                {1.0F, 1.0F}, {0.0F, 1.0F}},
            "historical texture did not retain a full-source view");
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

void native_geometry_preserves_image_fill_payload() {
    auto asset = native_geometry_fixture();
    auto& fill = asset.native->nodes.front().fill;
    fill.kind = fabric::project::VectorFillKind::image;
    fill.color.reset();
    fill.image = fabric::project::VectorImageFill{
        .texture = {{.value = "fabric-photo"}, "texture"},
        .fit = fabric::project::VectorImageFit::contain,
        .transform = {.position = {0.1F, -0.2F},
                      .rotation_degrees = 15.0F,
                      .scale = {1.2F, 0.8F},
                      .pivot = {0.5F, 0.5F}},
        .opacity = 0.75F,
        .deform_with_shape = true,
    };
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U,
            "image fill geometry packet build failed");
    const auto& packet = result.packets.front();
    require(!packet.fill_color.has_value() && packet.image_fill == fill.image &&
                packet.fill_indices.size() == 3U &&
                packet.fill_uv.size() == packet.fill_vertices.size(),
            "image fill payload or silhouette was lost in draw packet");
    require(packet.fill_vertices == packet.outline,
            "image fill transform moved the shape geometry");
    require(std::abs(packet.fill_uv.front().x - 0.124F) < 0.002F &&
                std::abs(packet.fill_uv.front().y + 0.243F) < 0.002F,
            "image fill transform was not applied independently to UVs");
}

void native_geometry_builds_textured_stroke_payload() {
    auto asset = native_geometry_fixture();
    asset.native->nodes.front().stroke = fabric::project::VectorStroke{
        .width = 2.0F,
        .image = fabric::project::VectorImageFill{
            .texture = {{.value = "beam-thread"}, "texture"},
            .transform = {.position = {0.25F, -0.5F}, .scale = {2.0F, 3.0F},
                          .pivot = {0.0F, 0.0F}}},
        .repeat_texture_x = true};
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U,
            "textured stroke geometry packet build failed");
    const auto& packet = result.packets.front();
    require(packet.stroke_image.has_value() && packet.stroke_repeat_texture_x &&
                packet.stroke_uv.size() == packet.stroke_vertices.size() &&
                packet.stroke_uv.front() == fabric::core::Vec2{0.25F, -0.5F} &&
                packet.stroke_uv[1] == fabric::core::Vec2{20.25F, -0.5F},
            "textured stroke payload or UVs were not propagated");
}

void native_geometry_applies_node_and_parent_transforms() {
    auto asset = native_geometry_fixture();
    auto& child = asset.native->nodes.front();
    child.parent_id = "parent-1";
    child.transform.position = {2.0F, 3.0F};
    child.transform.scale = {2.0F, 2.0F};
    asset.native->nodes.push_back({
        .id = "parent-1",
        .name = "Parent",
        .transform = {.position = {10.0F, -1.0F}},
        .shape = {.id = "parent-shape", .kind = fabric::project::VectorShapeKind::line,
                  .bounds = {.origin = {0.0F, 0.0F}, .size = {1.0F, 1.0F}},
                  .points = {{0.0F, 0.0F}, {1.0F, 0.0F}}},
    });
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 2U,
            "hierarchical transform packet build failed");
    const auto& child_packet = result.packets.front();
    require(child_packet.outline.front() == fabric::core::Vec2{12.0F, 2.0F},
            "parent and child transforms were not composed in draw packet");
}

void opengl_vector_renderer_reports_uninitialized_use() {
    fabric::render::OpenGLVectorRenderer renderer;
    const auto stats = renderer.draw(
        std::span<const fabric::render::VectorDrawPacket>{}, {
        .width = 64,
        .height = 64,
        .world_bounds = {.origin = {0.0F, 0.0F}, .size = {10.0F, 10.0F}},
    });
    require(!stats.ok() && !renderer.ready(),
            "uninitialized OpenGL renderer did not report its state");
}

void native_geometry_marks_open_strokes() {
    auto asset = native_geometry_fixture();
    auto& node = asset.native->nodes.front();
    node.shape.kind = fabric::project::VectorShapeKind::line;
    node.shape.points = {{0.0F, 0.0F}, {4.0F, 2.0F}};
    node.fill = {.kind = fabric::project::VectorFillKind::none};
    node.stroke = fabric::project::VectorStroke{};
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U &&
                !result.packets.front().closed_outline &&
                result.packets.front().stroke.has_value(),
            "line stroke packet was not marked as open");
}

void native_geometry_tessellates_strokes() {
    auto asset = native_geometry_fixture();
    auto& node = asset.native->nodes.front();
    node.stroke = fabric::project::VectorStroke{
        .width = 2.0F, .join = fabric::project::VectorStrokeJoin::bevel};
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U,
            "stroke geometry packet build failed");
    const auto& packet = result.packets.front();
    require(packet.stroke_vertices.size() == 30U &&
                packet.stroke_indices.size() == 36U,
            "stroke was not tessellated into segment quads");
    require(packet.stroke_vertices[0] == fabric::core::Vec2{0.0F, 1.0F} &&
                packet.stroke_vertices[3] == fabric::core::Vec2{0.0F, -1.0F},
            "stroke width was not applied to segment geometry");
}

void native_geometry_applies_square_stroke_caps() {
    auto asset = native_geometry_fixture();
    auto& node = asset.native->nodes.front();
    node.shape.kind = fabric::project::VectorShapeKind::line;
    node.shape.points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
    node.stroke = fabric::project::VectorStroke{
        .width = 2.0F, .cap = fabric::project::VectorStrokeCap::square};
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U,
            "square cap geometry packet build failed");
    const auto& vertices = result.packets.front().stroke_vertices;
    require(vertices.size() == 4U && vertices.front().x == -1.0F &&
                vertices[1].x == 5.0F,
            "square caps did not extend the stroke endpoints");
}

void native_geometry_applies_round_stroke_caps() {
    auto asset = native_geometry_fixture();
    auto& node = asset.native->nodes.front();
    node.shape.kind = fabric::project::VectorShapeKind::line;
    node.shape.points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
    node.stroke = fabric::project::VectorStroke{
        .width = 2.0F, .cap = fabric::project::VectorStrokeCap::round};
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U,
            "round cap geometry packet build failed");
    const auto& packet = result.packets.front();
    require(packet.stroke_vertices.size() == 32U &&
                packet.stroke_indices.size() == 78U,
            "round caps did not add semicircle geometry");
}

void native_geometry_covers_round_stroke_joins() {
    auto asset = native_geometry_fixture();
    auto& node = asset.native->nodes.front();
    node.stroke = fabric::project::VectorStroke{
        .width = 2.0F, .join = fabric::project::VectorStrokeJoin::round};
    const auto result = fabric::render::build_native_draw_packets(asset);
    require(result.ok() && result.packets.size() == 1U &&
                result.packets.front().stroke_vertices.size() == 54U,
            "round joins did not add vertex coverage");
}

void native_geometry_distinguishes_bevel_and_miter_joins() {
    auto bevel = native_geometry_fixture();
    bevel.native->nodes.front().stroke = fabric::project::VectorStroke{
        .width = 2.0F, .join = fabric::project::VectorStrokeJoin::bevel};
    auto miter = bevel;
    miter.native->nodes.front().stroke->join =
        fabric::project::VectorStrokeJoin::miter;
    const auto bevel_result = fabric::render::build_native_draw_packets(bevel);
    const auto miter_result = fabric::render::build_native_draw_packets(miter);
    require(bevel_result.ok() && miter_result.ok(),
            "bevel and miter geometry packet build failed");
    require(bevel_result.packets.front().stroke_vertices.size() == 30U &&
                miter_result.packets.front().stroke_vertices.size() == 48U,
            "bevel and miter joins did not produce distinct geometry");
}

} // namespace

int main() {
    valid_png_is_decoded_to_rgba8();
    invalid_inputs_are_rejected();
    valid_svg_is_rasterized_to_a_bounded_preview();
    invalid_svg_inputs_are_rejected_before_publication();
    svg_can_be_converted_to_native_paths();
    native_geometry_produces_deterministic_packets();
    raster_views_produce_shared_deterministic_packets();
    native_geometry_cache_invalidates_on_document_or_tolerance_change();
    native_geometry_preserves_image_fill_payload();
    native_geometry_builds_textured_stroke_payload();
    native_geometry_applies_node_and_parent_transforms();
    opengl_vector_renderer_reports_uninitialized_use();
    native_geometry_marks_open_strokes();
    native_geometry_tessellates_strokes();
    native_geometry_applies_square_stroke_caps();
    native_geometry_applies_round_stroke_caps();
    native_geometry_covers_round_stroke_joins();
    native_geometry_distinguishes_bevel_and_miter_joins();
    return 0;
}
