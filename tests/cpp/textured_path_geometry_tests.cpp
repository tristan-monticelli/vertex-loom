#include "fabric/render/textured_path_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace {

fabric::project::TexturedPath line_path() {
    using Kind = fabric::project::TexturedPathCommandKind;
    return {
        .document = {.schema_version = 1,
                     .type = "texturedPath",
                     .id = {.value = "beam"},
                     .name = "Beam"},
        .commands = {{.kind = Kind::move, .point = {0.0F, 0.0F}},
                     {.kind = Kind::line, .point = {2.0F, 0.0F}}},
        .width = 1.0F,
        .texture = {{.value = "thread"}, "texture"},
        .uv_scale = {2.0F, 0.5F},
        .uv_offset = {0.25F, 0.1F},
        .color = {0.8F, 0.5F, 0.25F, 0.75F},
        .opacity = 0.6F,
    };
}

void check_close(const float actual, const float expected) {
    CHECK(std::abs(actual - expected) < 1.0e-5F);
}

} // namespace

TEST_CASE("open textured path emits an exact continuous ribbon") {
    const auto path = line_path();
    const auto result = fabric::render::build_textured_path_draw_packets(path);
    REQUIRE(result.ok());
    REQUIRE(result.packets.size() == 1U);
    const auto& packet = result.packets.front();
    CHECK(packet.node_id == "beam");
    CHECK(packet.fill_color == path.color);
    REQUIRE(packet.image_fill.has_value());
    CHECK(packet.image_fill->texture == path.texture);
    CHECK(packet.image_fill->opacity == path.opacity);
    CHECK(packet.repeat_texture_x);
    CHECK(packet.fill_vertices == std::vector<fabric::core::Vec2>{
        {0.0F, 0.5F}, {0.0F, -0.5F},
        {2.0F, 0.5F}, {2.0F, -0.5F}});
    CHECK(packet.fill_indices == std::vector<std::uint32_t>{
        0U, 1U, 2U, 1U, 3U, 2U});
    REQUIRE(packet.fill_uv.size() == 4U);
    check_close(packet.fill_uv[0].x, 0.25F);
    check_close(packet.fill_uv[0].y, 0.1F);
    check_close(packet.fill_uv[1].y, 0.6F);
    check_close(packet.fill_uv[2].x, 4.25F);
    CHECK_FALSE(packet.closed_outline);
}

TEST_CASE("textured path draw packet carries its custom shader settings") {
    auto path = line_path();
    path.shader.profile = fabric::project::SurfaceShaderProfile::thread;
    path.shader.shine = 0.6F;
    path.shader.holography = 0.25F;
    path.shader.intensity = 1.3F;
    const auto result = fabric::render::build_textured_path_draw_packets(path);
    REQUIRE(result.ok());
    REQUIRE(result.packets.front().shader.has_value());
    CHECK(*result.packets.front().shader == path.shader);
}

TEST_CASE("stretch UVs and width profiles follow normalized path distance") {
    auto path = line_path();
    path.uv_mode = fabric::project::TexturedPathUvMode::stretch;
    path.width_profile = {{.position = 0.0F, .width = 0.5F},
                          {.position = 1.0F, .width = 2.0F}};
    const auto result = fabric::render::build_textured_path_draw_packets(path);
    REQUIRE(result.ok());
    const auto& packet = result.packets.front();
    CHECK_FALSE(packet.repeat_texture_x);
    check_close(packet.fill_vertices[0].y, 0.25F);
    check_close(packet.fill_vertices[2].y, 1.0F);
    check_close(packet.fill_uv[2].x, 2.25F);
}

TEST_CASE("Bezier tessellation is bounded and deterministic") {
    auto path = line_path();
    path.commands[1] = {
        .kind = fabric::project::TexturedPathCommandKind::cubic,
        .point = {3.0F, 0.0F},
        .control1 = {0.5F, 3.0F},
        .control2 = {2.5F, -3.0F},
    };
    const auto first = fabric::render::build_textured_path_draw_packets(
        path, 0.1F);
    const auto second = fabric::render::build_textured_path_draw_packets(
        path, 0.1F);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.packets == second.packets);
    CHECK(first.packets.front().fill_vertices.size() > 4U);
    CHECK(first.packets.front().fill_vertices.size() <= 8194U);
}

TEST_CASE("Beam keeps repeated texture UVs continuous across external segments") {
    auto beam = line_path();
    beam.document.id = {.value = "external-beam"};
    beam.texture = {{.value = "external-thread"}, "texture"};
    beam.uv_scale = {3.0F, 1.0F};
    beam.shader.profile = fabric::project::SurfaceShaderProfile::thread;
    beam.shader.classification = fabric::project::TextureClassification::beam;
    beam.shader.primary_color = {0.15F, 0.3F, 1.0F, 1.0F};
    beam.shader.effect_color = {1.0F, 0.1F, 0.75F, 1.0F};
    beam.shader.holography = 0.8F;
    beam.commands = {
        {.kind = fabric::project::TexturedPathCommandKind::move,
         .point = {-2.0F, 0.0F}},
        {.kind = fabric::project::TexturedPathCommandKind::line,
         .point = {-0.5F, 1.0F}},
        {.kind = fabric::project::TexturedPathCommandKind::cubic,
         .point = {1.0F, -1.0F},
         .control1 = {0.0F, 2.0F},
         .control2 = {0.5F, -2.0F}},
        {.kind = fabric::project::TexturedPathCommandKind::line,
         .point = {2.0F, 0.5F}},
    };
    const auto result = fabric::render::build_textured_path_draw_packets(
        beam, 0.05F);
    REQUIRE(result.ok());
    REQUIRE(result.packets.size() == 1U);
    const auto& packet = result.packets.front();
    REQUIRE(packet.image_fill.has_value());
    CHECK(packet.image_fill->texture == beam.texture);
    CHECK(packet.repeat_texture_x);
    REQUIRE(packet.shader.has_value());
    CHECK(*packet.shader == beam.shader);
    REQUIRE(packet.fill_uv.size() == packet.fill_vertices.size());
    for (std::size_t index = 0; index < packet.fill_uv.size(); index += 2U) {
        CHECK(std::isfinite(packet.fill_uv[index].x));
        CHECK(std::isfinite(packet.fill_uv[index].y));
        if (index > 0U)
            CHECK(packet.fill_uv[index].x >= packet.fill_uv[index - 2U].x);
    }
    CHECK(packet.fill_uv.back().x > packet.fill_uv.front().x + 2.0F);
}

TEST_CASE("closed ribbon duplicates its seam with continuous UVs") {
    auto path = line_path();
    path.commands.push_back({
        .kind = fabric::project::TexturedPathCommandKind::line,
        .point = {1.0F, 1.5F}});
    path.closed = true;
    const auto result = fabric::render::build_textured_path_draw_packets(path);
    REQUIRE(result.ok());
    const auto& packet = result.packets.front();
    REQUIRE(packet.fill_vertices.size() >= 8U);
    CHECK(packet.fill_vertices[0] ==
          packet.fill_vertices[packet.fill_vertices.size() - 2U]);
    CHECK(packet.fill_vertices[1] == packet.fill_vertices.back());
    CHECK(packet.fill_uv[packet.fill_uv.size() - 2U].x >
          packet.fill_uv.front().x);
    CHECK(packet.closed_outline);
    CHECK(packet.fill_indices.size() ==
          (packet.fill_vertices.size() / 2U - 1U) * 6U);
}

TEST_CASE("join and cap modes derive distinct valid geometry") {
    auto corner = line_path();
    corner.commands.push_back({
        .kind = fabric::project::TexturedPathCommandKind::line,
        .point = {2.0F, 2.0F}});
    const auto miter = fabric::render::build_textured_path_draw_packets(corner);
    REQUIRE(miter.ok());

    corner.join = fabric::project::TexturedPathJoin::bevel;
    const auto bevel = fabric::render::build_textured_path_draw_packets(corner);
    REQUIRE(bevel.ok());
    CHECK(bevel.packets.front().fill_vertices.size() >
          miter.packets.front().fill_vertices.size());

    corner.join = fabric::project::TexturedPathJoin::round;
    const auto round_join =
        fabric::render::build_textured_path_draw_packets(corner);
    REQUIRE(round_join.ok());
    CHECK(round_join.packets.front().fill_vertices.size() >
          bevel.packets.front().fill_vertices.size());

    auto caps = line_path();
    caps.cap = fabric::project::TexturedPathCap::square;
    const auto square = fabric::render::build_textured_path_draw_packets(caps);
    REQUIRE(square.ok());
    check_close(square.packets.front().fill_vertices.front().x, -0.5F);
    check_close(square.packets.front().fill_vertices[2].x, 2.5F);

    caps.cap = fabric::project::TexturedPathCap::round;
    const auto round_cap =
        fabric::render::build_textured_path_draw_packets(caps);
    REQUIRE(round_cap.ok());
    CHECK(round_cap.packets.front().fill_indices.size() >
          miter.packets.front().fill_indices.size());
}

TEST_CASE("textured path geometry rejects invalid tolerance and reversals") {
    const auto invalid_tolerance =
        fabric::render::build_textured_path_draw_packets(
            line_path(), std::numeric_limits<float>::infinity());
    CHECK_FALSE(invalid_tolerance.ok());

    auto reversal = line_path();
    reversal.commands.push_back({
        .kind = fabric::project::TexturedPathCommandKind::line,
        .point = {0.0F, 0.0F}});
    CHECK_FALSE(fabric::render::build_textured_path_draw_packets(
                    reversal).ok());

    auto overflowing_uv = line_path();
    overflowing_uv.uv_scale.x = std::numeric_limits<float>::max();
    CHECK_FALSE(fabric::render::build_textured_path_draw_packets(
                    overflowing_uv).ok());
}
