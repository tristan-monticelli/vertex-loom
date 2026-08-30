#include "fabric/project/mesh_deformation.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("weighted mesh deformation blends node poses deterministically") {
    using namespace fabric::project;
    const DeformationMesh mesh{
        .vertices = {{{0.0F, 0.0F}, {{"left", 0.5F}, {"right", 0.5F}}}},
        .triangles = {},
    };
    const auto result = deform_mesh(mesh, {
        {"left", {.position = {2.0F, 0.0F}}},
        {"right", {.position = {0.0F, 2.0F}}},
    });
    REQUIRE(result.ok());
    REQUIRE(result.positions.front() == fabric::core::Vec2{1.0F, 1.0F});
}

TEST_CASE("mesh deformation rejects missing poses and invalid triangles") {
    using namespace fabric::project;
    const DeformationMesh mesh{
        .vertices = {{{0.0F, 0.0F}, {{"missing", 1.0F}}}},
        .triangles = {{0, 0, 1}},
    };
    REQUIRE_FALSE(validate_deformation_mesh(mesh).ok());
    const DeformationMesh valid_mesh{
        .vertices = {{{0.0F, 0.0F}, {{"missing", 1.0F}}}},
        .triangles = {},
    };
    REQUIRE_FALSE(deform_mesh(valid_mesh, {}).ok());
}
