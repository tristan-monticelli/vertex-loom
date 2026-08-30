#include "fabric/editor/canvas_interaction.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

fabric::project::VectorNode rectangle(const char* id) {
    return {
        .id = id,
        .name = id,
        .shape = {
            .kind = fabric::project::VectorShapeKind::rectangle,
            .bounds = {{-1.0F, -0.5F}, {2.0F, 1.0F}},
        },
    };
}

} // namespace

TEST_CASE("canvas hit test follows the node transform") {
    auto node = rectangle("shape");
    node.transform.position = {4.0F, 3.0F};
    node.transform.rotation_degrees = 90.0F;
    node.transform.scale = {2.0F, 1.0F};

    CHECK(fabric::editor::point_hits_vector_node({4.0F, 4.5F}, node));
    CHECK_FALSE(fabric::editor::point_hits_vector_node({6.0F, 3.0F}, node));
    CHECK(fabric::editor::point_hits_vector_node({4.0F, 5.1F}, node, 0.1F));
}

TEST_CASE("canvas selection chooses the topmost visible node") {
    std::vector nodes{rectangle("bottom"), rectangle("top")};
    REQUIRE(fabric::editor::topmost_vector_node_at(nodes, {0.0F, 0.0F}) == 1U);
    nodes[1].visible = false;
    REQUIRE(fabric::editor::topmost_vector_node_at(nodes, {0.0F, 0.0F}) == 0U);
    CHECK_FALSE(fabric::editor::topmost_vector_node_at(nodes, {4.0F, 4.0F}));
}

TEST_CASE("rotation handle extends along the transformed edge") {
    const auto upright = fabric::editor::extend_canvas_handle(
        {10.0F, 10.0F}, {10.0F, 5.0F}, 3.0F);
    CHECK(upright.x == 10.0F);
    CHECK(upright.y == 2.0F);

    const auto rotated = fabric::editor::extend_canvas_handle(
        {10.0F, 10.0F}, {10.0F, 15.0F}, 3.0F);
    CHECK(rotated.x == 10.0F);
    CHECK(rotated.y == 18.0F);
}

TEST_CASE("Bezier handle modes preserve their editing contract") {
    using Mode = fabric::editor::BezierHandleMode;
    using Kind = fabric::project::VectorPathCommandKind;
    const auto make_shape = [] {
        return fabric::project::VectorShape{
            .kind = fabric::project::VectorShapeKind::path,
            .path = {
                {.kind = Kind::move, .point = {0.0F, 0.0F}},
                {.kind = Kind::cubic,
                 .point = {4.0F, 0.0F},
                 .control1 = {1.0F, 1.0F},
                 .control2 = {3.0F, 0.0F}},
            },
        };
    };

    auto free_shape = make_shape();
    REQUIRE(fabric::editor::update_bezier_handle(
        free_shape, 1, true, {8.0F, 2.0F}, Mode::free));
    CHECK(free_shape.path[1].control1 == fabric::core::Vec2{8.0F, 2.0F});
    CHECK(free_shape.path[1].control2 == fabric::core::Vec2{3.0F, 0.0F});

    auto symmetric_shape = make_shape();
    REQUIRE(fabric::editor::update_bezier_handle(
        symmetric_shape, 1, true, {8.0F, 2.0F}, Mode::symmetric));
    CHECK(symmetric_shape.path[1].control2 == fabric::core::Vec2{0.0F, -2.0F});

    auto linked_shape = make_shape();
    REQUIRE(fabric::editor::update_bezier_handle(
        linked_shape, 1, true, {8.0F, 2.0F}, Mode::linked));
    CHECK(linked_shape.path[1].control2.x ==
          Catch::Approx(4.0F - 4.0F / std::sqrt(20.0F)));
    CHECK(linked_shape.path[1].control2.y ==
          Catch::Approx(-2.0F / std::sqrt(20.0F)));

    CHECK_FALSE(fabric::editor::update_bezier_handle(
        linked_shape, 0, true, {1.0F, 1.0F}, Mode::linked));
}

TEST_CASE("raster crop drag stays inside the immutable source") {
    fabric::project::RasterView view{
        .crop = {{10.0F, 10.0F}, {40.0F, 30.0F}},
    };
    const auto moved = fabric::editor::drag_raster_crop(
        view, fabric::editor::RasterCropDrag::move, {80.0F, -20.0F}, 100, 80);
    CHECK(moved.crop == fabric::core::Rect{{60.0F, 0.0F}, {40.0F, 30.0F}});

    const auto resized = fabric::editor::drag_raster_crop(
        view, fabric::editor::RasterCropDrag::bottom_right,
        {100.0F, 100.0F}, 100, 80);
    CHECK(resized.crop == fabric::core::Rect{{10.0F, 10.0F}, {90.0F, 70.0F}});

    const auto minimum = fabric::editor::drag_raster_crop(
        view, fabric::editor::RasterCropDrag::top_left,
        {100.0F, 100.0F}, 100, 80);
    CHECK(minimum.crop == fabric::core::Rect{{49.0F, 39.0F}, {1.0F, 1.0F}});
}
