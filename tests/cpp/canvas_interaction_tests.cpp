#include "fabric/editor/canvas_interaction.hpp"

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
