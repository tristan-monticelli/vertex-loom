#include "fabric/project/entity.hpp"
#include "fabric/project/material.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "contract-test"},
            .name = "Contract Test",
            .directories = {}};
}

fabric::project::MaterialDefinition material() {
    return {
        .document = {.schema_version =
                         fabric::project::current_material_schema_version,
                     .type = "material",
                     .id = {.value = "wool-material"},
                     .name = "Wool Material"},
        .color = {0.8F, 0.6F, 0.3F, 1.0F},
        .opacity = 0.75F,
        .blend = fabric::project::MaterialBlendMode::multiply,
        .texture = fabric::project::ResourceReference{
            {.value = "wool-fill"}, "texture"},
        .uv_transform = {.position = {0.1F, -0.2F},
                         .rotation_degrees = 15.0F,
                         .scale = {1.2F, 0.8F},
                         .pivot = {0.5F, 0.5F}},
    };
}

fabric::project::EntityDefinition entity() {
    return {
        .document = {.schema_version =
                         fabric::project::current_entity_schema_version,
                     .type = "entity",
                     .id = {.value = "wool-entity"},
                     .name = "Wool Entity"},
        .nodes = {{
            .id = "root",
            .name = "Root",
            .drawable = {.kind = fabric::project::EntityDrawableKind::vector,
                         .resource = fabric::project::ResourceReference{
                             {.value = "thread-outline"}, "vector"},
                         .material = fabric::project::ResourceReference{
                             {.value = "wool-material"}, "material"}},
        }, {
            .id = "child",
            .name = "Child",
            .parent = "root",
            .z_order = 2.0F,
            .drawable = {.kind = fabric::project::EntityDrawableKind::texture,
                         .resource = fabric::project::ResourceReference{
                             {.value = "wool-fill"}, "texture"}},
        }},
    };
}

TEST_CASE("material definition round trips and publishes atomically") {
    const auto source = material();
    const auto parsed = fabric::project::parse_material(
        manifest(), fabric::project::serialize_material(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.asset == source);

    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-material-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "assets");
    const auto published = fabric::project::publish_material(root, manifest(), source);
    REQUIRE(published.ok());
    REQUIRE(std::filesystem::is_regular_file(
        root / "assets/materials/wool-material.material.json"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("entity definition round trips and rejects parent cycles") {
    const auto source = entity();
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.entity == source);

    auto cyclic = source;
    cyclic.nodes[0].parent = "child";
    const auto report = fabric::project::validate_entity(manifest(), cyclic);
    REQUIRE_FALSE(report.ok());
    REQUIRE(std::ranges::any_of(report.errors, [](const auto& error) {
        return error.code == fabric::project::ErrorCode::resource_cycle;
    }));
}

TEST_CASE("entity v1 migrates to v2 without changing legacy drawables") {
    auto legacy = entity();
    legacy.document.schema_version = 1;
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(legacy));
    REQUIRE(parsed.ok());
    CHECK(parsed.entity->document.schema_version ==
          fabric::project::current_entity_schema_version);
    CHECK(parsed.entity->nodes == legacy.nodes);
}

TEST_CASE("entity visual component instances round-trip and expose resources") {
    auto source = entity();
    auto& drawable = source.nodes.front().drawable;
    drawable.kind = fabric::project::EntityDrawableKind::visual_component;
    drawable.resource = fabric::project::ResourceReference{
        {.value = "button-eye"}, "visualComponent"};
    drawable.material.reset();
    drawable.component_instance = fabric::project::VisualComponentInstance{
        .variant_id = "stitched",
        .anchor_id = "center",
        .overrides = {
            {"scale", fabric::core::Vec2{1.5F, 0.75F}},
            {"thread-texture", fabric::project::ResourceReference{
                                   {.value = "blue-thread"}, "texture"}}}};

    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.entity == source);

    const auto references = fabric::project::entity_resource_references(source);
    CHECK(std::ranges::any_of(references, [](const auto& reference) {
        return reference.id.value == "button-eye" &&
            reference.expected_type == "visualComponent";
    }));
    CHECK(std::ranges::any_of(references, [](const auto& reference) {
        return reference.id.value == "blue-thread" &&
            reference.expected_type == "texture";
    }));
}

TEST_CASE("material and entity references retain their expected types") {
    auto invalid_material = material();
    invalid_material.texture->expected_type = "vector";
    REQUIRE_FALSE(fabric::project::validate_material(
        manifest(), invalid_material).ok());

    auto invalid_entity = entity();
    invalid_entity.nodes.front().drawable.resource->expected_type = "texture";
    REQUIRE_FALSE(fabric::project::validate_entity(
        manifest(), invalid_entity).ok());
}

TEST_CASE("entity constraints round-trip and require existing nodes") {
    auto source = entity();
    source.constraints.push_back({
        .id = "copy-root-child",
        .kind = fabric::project::AnimationConstraintKind::copy_transform,
        .target_node = "child",
        .source_node = "root",
        .order = 1,
        .constrain_position = true,
        .constrain_rotation = false,
        .constrain_scale = true,
        .min_position = fabric::core::Vec2{-2.0F, -3.0F},
        .max_position = fabric::core::Vec2{2.0F, 3.0F},
        .min_rotation_degrees = -45.0F,
        .max_rotation_degrees = 45.0F,
        .min_scale = fabric::core::Vec2{0.5F, 0.5F},
        .max_scale = fabric::core::Vec2{2.0F, 2.0F}});
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.entity == source);

    auto invalid = source;
    invalid.constraints.front().source_node = "missing";
    REQUIRE_FALSE(fabric::project::validate_entity(manifest(), invalid).ok());
}

TEST_CASE("entity FABRIK chains round-trip and validate node references") {
    auto source = entity();
    source.nodes.push_back({.id = "goal", .name = "Goal"});
    source.ik_chains.push_back({.id = "arm", .joints = {"root", "child"},
                                .target_node = "goal", .max_iterations = 24,
                                .tolerance = 1.0e-4F});
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.entity == source);

    auto invalid = source;
    invalid.ik_chains.front().target_node = "child";
    REQUIRE_FALSE(fabric::project::validate_entity(manifest(), invalid).ok());
}

TEST_CASE("entity animation state machines round-trip and expose clip references") {
    auto source = entity();
    source.animation_state_machine = fabric::project::AnimationStateMachine{
        .initial_state = "idle",
        .states = {{"idle", {{.value = "idle-clip"}, "animation"}},
                   {"run", {{.value = "run-clip"}, "animation"}}},
        .transitions = {{"start", "idle", "run", {}, 0.5F, 3}}};
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    REQUIRE(*parsed.entity == source);
    const auto references = fabric::project::entity_resource_references(source);
    REQUIRE(std::ranges::any_of(references, [](const auto& reference) {
        return reference.id.value == "idle-clip";
    }));
}

TEST_CASE("entity deformation mesh round-trips and requires valid nodes") {
    auto source = entity();
    source.deformation_mesh = fabric::project::DeformationMesh{};
    source.deformation_mesh->vertices.push_back({
        .rest_position = {-1.0F, -1.0F},
        .influences = {{.node_id = "root", .weight = 1.0F}}});
    source.deformation_mesh->vertices.push_back({
        .rest_position = {1.0F, -1.0F},
        .influences = {{.node_id = "root", .weight = 1.0F}}});
    source.deformation_mesh->vertices.push_back({
        .rest_position = {0.0F, 1.0F},
        .influences = {{.node_id = "child", .weight = 1.0F}}});
    source.deformation_mesh->triangles.push_back({.first = 0, .second = 1, .third = 2});
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.entity == source);

    auto invalid = source;
    invalid.deformation_mesh->vertices.front().influences.front().node_id = "missing";
    REQUIRE_FALSE(fabric::project::validate_entity(manifest(), invalid).ok());
}

TEST_CASE("entity XPBD state round-trips and rejects invalid particle indices") {
    auto source = entity();
    source.xpbd = fabric::project::XpbdSystem{};
    source.xpbd->particles = {
        {.position = {0.0F, 0.0F}, .inverse_mass = 1.0F},
        {.position = {1.0F, 0.0F}, .inverse_mass = 1.0F},
        {.position = {0.0F, 1.0F}, .inverse_mass = 1.0F}};
    source.xpbd->distance_constraints.push_back({
        .first = 0, .second = 1, .rest_length = 1.0F,
        .compliance = 0.01F, .lambda = 0.2F});
    source.xpbd->pin_constraints.push_back({
        .particle = 0, .target = {0.0F, 0.0F}, .compliance = 0.0F,
        .lambda = {0.0F, 0.0F}});
    source.xpbd->bending_constraints.push_back({
        .first = 0, .middle = 1, .third = 2, .rest_length = 1.0F,
        .compliance = 0.01F, .lambda = 0.0F});
    source.xpbd->area_constraints.push_back({
        .first = 0, .second = 1, .third = 2, .rest_area = 0.5F,
        .compliance = 0.01F, .lambda = 0.0F});
    source.xpbd->collision_constraints.push_back({
        .particle = 2, .normal = {0.0F, 1.0F}, .offset = -1.0F,
        .compliance = 0.01F, .lambda = 0.0F});
    const auto parsed = fabric::project::parse_entity(
        manifest(), fabric::project::serialize_entity(source));
    REQUIRE(parsed.ok());
    CHECK(*parsed.entity == source);

    auto invalid = source;
    invalid.xpbd->distance_constraints.front().second = 99;
    REQUIRE_FALSE(fabric::project::validate_entity(manifest(), invalid).ok());

    auto mismatched = source;
    mismatched.deformation_mesh = fabric::project::DeformationMesh{};
    mismatched.deformation_mesh->vertices.push_back({
        .rest_position = {0.0F, 0.0F},
        .influences = {{.node_id = "root", .weight = 1.0F}}});
    REQUIRE_FALSE(fabric::project::validate_entity(manifest(), mismatched).ok());
}

} // namespace
