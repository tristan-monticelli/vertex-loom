#include "fabric/runtime/preview_runtime.hpp"
#include "fabric/runtime/scene_session.hpp"

#include "fabric/project/entity.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "runtime-test"},
            .name = "Runtime Test"};
}

fabric::project::MapDocument map() {
    return {.document = {.schema_version = 1,
                         .type = "map",
                         .id = {.value = "preview"},
                         .name = "Preview"},
            .layers = {{"instances", "Instances",
                        fabric::project::MapLayerKind::instances, true, false, 0.0F}}};
}

fabric::project::SceneDocument scene() {
    return {.document = {.schema_version = 1,
                         .type = "scene",
                         .id = {.value = "preview-scene"},
                         .name = "Preview Scene"},
            .maps = {{{{.value = "preview"}, "map"}, "instances"}},
            .entry_map = fabric::project::ResourceReference{{.value = "preview"}, "map"}};
}

fabric::project::MapDocument map_with_gameplay_trigger() {
    auto result = map();
    result.layers.push_back({"collision", "Collision",
                             fabric::project::MapLayerKind::collision, true, false, 0.0F});
    result.layers.push_back({"triggers", "Triggers",
                             fabric::project::MapLayerKind::triggers, true, false, 0.0F});
    result.collisions.push_back({.kind = fabric::project::CollisionShapeKind::circle,
                                 .layer_id = "collision",
                                 .sensor = true,
                                 .center = {0.0F, 0.0F},
                                 .radius = 8.0F});
    result.events.push_back({{.value = "open-door"}, {}});
    result.triggers.push_back({"spawn-zone", "triggers", 0,
                               {.value = "open-door"}, {}});
    return result;
}

fabric::project::AnimationClip animation() {
    return {.document = {.schema_version = 1,
                         .type = "animation",
                         .id = {.value = "runtime-animation"},
                         .name = "Runtime Animation"},
            .duration = 1.0F,
            .tracks = {{{.node_id = "root",
                         .component_id = "transform",
                         .property_id = "position"},
                       fabric::project::AnimationInterpolation::linear,
                       {{0.0F, fabric::core::Vec2{0.0F, 0.0F}},
                        {1.0F, fabric::core::Vec2{2.0F, 4.0F}}}}}};
}

fabric::project::AnimationClip transform_animation() {
    auto result = animation();
    result.tracks.push_back({
        {.node_id = "root", .component_id = "transform", .property_id = "rotationDegrees"},
        fabric::project::AnimationInterpolation::linear,
        {{0.0F, 0.0F}, {1.0F, 90.0F}}});
    result.tracks.push_back({
        {.node_id = "root", .component_id = "transform", .property_id = "scale"},
        fabric::project::AnimationInterpolation::linear,
        {{0.0F, fabric::core::Vec2{1.0F, 1.0F}},
         {1.0F, fabric::core::Vec2{2.0F, 3.0F}}}});
    return result;
}

fabric::project::ReplayDocument replay() {
    return {.document = {.schema_version = 1,
                         .type = "replay",
                         .id = {.value = "smoke-replay"},
                         .name = "Smoke Replay"},
            .build = "test-build",
            .seed = 42,
            .inputs = {{0, "jump", true, false}, {1, "jump", false, true}},
            .events = {{1, "opened", ""}},
            .checkpoints = {{1, {{"player", 0, 0, 0}}}}};
}

fabric::project::VectorAsset vector_asset() {
    return {.document = {.schema_version = 2,
                         .type = "vector",
                         .id = {.value = "runtime-vector"},
                         .name = "Runtime Vector"},
            .source_kind = fabric::project::VectorSourceKind::native,
            .native = fabric::project::NativeVectorDefinition{
                .size = {2.0F, 2.0F},
                .nodes = {{.id = "shape",
                           .name = "Shape",
                           .shape = {.id = "shape",
                                     .bounds = {{-1.0F, -1.0F}, {2.0F, 2.0F}}},
                           .fill = {.kind = fabric::project::VectorFillKind::solid,
                                    .color = fabric::core::Color{1.0F, 0.2F, 0.1F, 1.0F}}}}}};
}

fabric::project::EntityDefinition entity() {
    return {.document = {.schema_version = 1,
                         .type = "entity",
                         .id = {.value = "runtime-entity"},
                         .name = "Runtime Entity"},
            .nodes = {{.id = "root",
                       .name = "Root",
                       .drawable = {.kind = fabric::project::EntityDrawableKind::vector,
                                     .resource = fabric::project::ResourceReference{
                                         {.value = "runtime-vector"}, "vector"}}}}};
}

fabric::project::MapDocument map_with_entity() {
    auto result = map();
    result.instances.push_back({"marker",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    result.instances.push_back({"offscreen",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-entity"}, "entity"},
                                std::nullopt, "instances",
                                {.position = {10000.0F, 10000.0F}}, 156, 156, {}});
    return result;
}

fabric::project::EntityDefinition ordered_entity() {
    auto result = entity();
    result.nodes.front().z_order = 10.0F;
    result.nodes.push_back({.id = "front",
                            .name = "Front",
                            .z_order = -1.0F,
                            .drawable = {.kind = fabric::project::EntityDrawableKind::vector,
                                         .resource = fabric::project::ResourceReference{
                                             {.value = "runtime-vector"}, "vector"}}});
    return result;
}

fabric::project::MapDocument map_with_ordered_entity() {
    auto result = map();
    result.instances.push_back({"ordered", fabric::project::ResourceReference{
                                    {.value = "ordered-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::MapDocument map_with_animated_entity() {
    auto result = map_with_entity();
    result.instances.front().properties.push_back({
        "animation", fabric::project::ResourceReference{
            {.value = "runtime-animation"}, "animation"}});
    return result;
}

fabric::project::MapDocument map_with_animated_prefab() {
    auto result = map();
    result.prefabs.push_back({
        "animated", fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"},
        {{"animation", fabric::project::ResourceReference{
            {.value = "runtime-animation"}, "animation"}}}});
    result.instances.push_back({
        "marker", std::nullopt,
        fabric::project::ResourceReference{{.value = "animated"}, "prefab"},
        "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::EntityDefinition texture_entity() {
    return {.document = {.schema_version = 1,
                         .type = "entity",
                         .id = {.value = "runtime-texture-entity"},
                         .name = "Runtime Texture Entity"},
            .nodes = {{.id = "root",
                       .name = "Root",
                       .drawable = {.kind = fabric::project::EntityDrawableKind::texture,
                                     .resource = fabric::project::ResourceReference{
                                         {.value = "runtime-texture"}, "texture"}}}}};
}

fabric::project::EntityDefinition simulated_entity() {
    auto result = entity();
    result.deformation_mesh = fabric::project::DeformationMesh{
        .vertices = {
            {.rest_position = {-1.0F, 0.0F},
             .influences = {{.node_id = "root", .weight = 1.0F}}},
            {.rest_position = {1.0F, 0.0F},
             .influences = {{.node_id = "root", .weight = 1.0F}}}},
        .triangles = {}};
    result.xpbd = fabric::project::XpbdSystem{
        .particles = {{.position = {0.0F, 0.0F}, .inverse_mass = 1.0F},
                      {.position = {2.0F, 0.0F}, .inverse_mass = 1.0F}},
        .distance_constraints = {{.first = 0, .second = 1,
                                  .rest_length = 1.0F,
                                  .compliance = 0.0F, .lambda = 0.0F}}};
    return result;
}

fabric::project::MapDocument map_with_simulated_entity() {
    auto result = map();
    result.instances.push_back({"simulated",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::EntityDefinition render_deformed_entity() {
    auto result = entity();
    result.document.id = {.value = "render-deformed-entity"};
    result.deformation_mesh = fabric::project::DeformationMesh{
        .vertices = {
            {.rest_position = {-1.0F, -1.0F},
             .influences = {{.node_id = "root", .weight = 1.0F}}},
            {.rest_position = {1.0F, -1.0F},
             .influences = {{.node_id = "root", .weight = 1.0F}}},
            {.rest_position = {1.0F, 1.0F},
             .influences = {{.node_id = "root", .weight = 1.0F}}},
            {.rest_position = {-1.0F, 1.0F},
             .influences = {{.node_id = "root", .weight = 1.0F}}}},
        .triangles = {{.first = 3, .second = 0, .third = 1},
                      {.first = 1, .second = 2, .third = 3}}};
    return result;
}

fabric::project::MapDocument map_with_render_deformed_entity() {
    auto result = map();
    result.instances.push_back({"deformed", fabric::project::ResourceReference{
                                    {.value = "render-deformed-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::MapDocument map_with_animated_render_deformed_entity() {
    auto result = map_with_render_deformed_entity();
    result.instances.front().properties.push_back({
        "animation", fabric::project::ResourceReference{
            {.value = "runtime-animation"}, "animation"}});
    return result;
}

fabric::project::EntityDefinition constrained_entity() {
    auto result = entity();
    result.document.id = {.value = "constrained-entity"};
    result.nodes.push_back({.id = "target", .name = "Target"});
    result.nodes.front().transform.position = {3.0F, 4.0F};
    result.constraints.push_back({
        .id = "copy-root-to-target",
        .kind = fabric::project::AnimationConstraintKind::copy_transform,
        .target_node = "target",
        .source_node = "root",
        .order = 1,
        .constrain_position = true,
        .constrain_rotation = false,
        .constrain_scale = false});
    result.deformation_mesh = fabric::project::DeformationMesh{
        .vertices = {{.rest_position = {0.0F, 0.0F},
                      .influences = {{.node_id = "target", .weight = 1.0F}}}},
        .triangles = {}};
    return result;
}

fabric::project::MapDocument map_with_constrained_entity() {
    auto result = map();
    result.instances.push_back({"constrained", fabric::project::ResourceReference{
                                    {.value = "constrained-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::EntityDefinition ik_entity() {
    auto result = entity();
    result.document.id = {.value = "ik-entity"};
    result.nodes.push_back({.id = "joint", .name = "Joint"});
    result.nodes.push_back({.id = "goal", .name = "Goal"});
    result.nodes[1].transform.position = {1.0F, 0.0F};
    result.nodes[2].transform.position = {0.0F, 1.0F};
    result.ik_chains.push_back({.id = "arm", .joints = {"root", "joint"},
                                .target_node = "goal", .max_iterations = 32,
                                .tolerance = 1.0e-4F});
    result.deformation_mesh = fabric::project::DeformationMesh{
        .vertices = {{.rest_position = {0.0F, 0.0F},
                      .influences = {{.node_id = "joint", .weight = 1.0F}}}},
        .triangles = {}};
    return result;
}

fabric::project::MapDocument map_with_ik_entity() {
    auto result = map();
    result.instances.push_back({"ik", fabric::project::ResourceReference{
                                    {.value = "ik-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::EntityDefinition state_machine_entity() {
    auto result = entity();
    result.document.id = {.value = "state-machine-entity"};
    result.animation_state_machine = fabric::project::AnimationStateMachine{
        .initial_state = "idle",
        .states = {{"idle", {{.value = "runtime-animation"}, "animation"}},
                   {"run", {{.value = "runtime-animation"}, "animation"}}},
        .transitions = {{"start", "idle", "run",
                         {{"speed", fabric::project::AnimationConditionOperator::greater,
                           0.1F}}, 0.5F, 1}}};
    return result;
}

fabric::project::MapDocument map_with_state_machine_entity() {
    auto result = map();
    result.instances.push_back({"state-machine", fabric::project::ResourceReference{
                                    {.value = "state-machine-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0,
                                {{"animationParameter.speed", 1.0F}}});
    return result;
}

fabric::project::MapDocument map_with_texture_entity() {
    auto result = map();
    result.instances.push_back({"textured",
                                fabric::project::ResourceReference{
                                    {.value = "runtime-texture-entity"}, "entity"},
                                std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

TEST_CASE("preview runtime validates and loads a map before graphics") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-runtime-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.loaded());
    REQUIRE(runtime.map()->instances.empty());
    REQUIRE(runtime.errors().empty());
    const auto ran = runtime.run();
    if (!ran) for (const auto& error : runtime.errors()) std::cerr << error << '\n';
    REQUIRE(ran);
    REQUIRE(runtime.stats().frames == 1);
    REQUIRE(runtime.stats().physics_steps == 1);
    REQUIRE(runtime.stats().p95_frame_ms >= 0.0);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime resolves a scene entry map before graphics") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-scene-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map()).ok());
    REQUIRE(fabric::project::publish_scene(root, project_manifest, scene()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root,
                          .scene_id = fabric::core::ResourceId{.value = "preview-scene"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.scene().has_value());
    REQUIRE(runtime.map().has_value());
    CHECK(runtime.scene()->document.id.value == "preview-scene");
    CHECK(runtime.map()->document.id.value == "preview");
    CHECK(runtime.errors().empty());
    REQUIRE(runtime.run());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime rejects ambiguous map and scene selection") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-selection-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE_FALSE(runtime.load({.project_root = root,
                                .map_id = fabric::core::ResourceId{.value = "preview"},
                                .scene_id = fabric::core::ResourceId{.value = "preview-scene"},
                                .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE_FALSE(runtime.errors().empty());
    CHECK(runtime.errors().front().find("exactly one") != std::string::npos);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime handler can stop after a gameplay event") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-event-handler-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest,
                                         map_with_gameplay_trigger()).ok());

    bool handled = false;
    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root,
                          .map_id = {.value = "preview"},
                          .gameplay_event_handler = [&](const auto& event) {
                              handled = event.id.value == "open-door" &&
                                  event.trigger_id == "spawn-zone";
                              return false;
                          },
                          .enable_character = true,
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    CHECK(handled);
    CHECK(runtime.stats().gameplay_events == 1U);
    CHECK(runtime.stats().frames == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime accepts a custom locomotion binding table") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-input-config-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root,
                          .map_id = {.value = "preview"},
                          .input_actions = {
                              {"move_left", {{fabric::runtime::InputDevice::keyboard, 74}}},
                              {"move_right", {{fabric::runtime::InputDevice::keyboard, 76}}},
                              {"jump", {{fabric::runtime::InputDevice::gamepad, 1}}}},
                          .enable_character = true,
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    CHECK(runtime.stats().physics_steps == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("runtime handoff transitions from a triggered scene to its target") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-runtime-scene-handoff-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());

    auto source_map = map_with_gameplay_trigger();
    auto target_map = map();
    target_map.document.id = {.value = "target-map"};
    target_map.document.name = "Target Map";
    REQUIRE(fabric::project::publish_map(root, project_manifest, source_map).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, target_map).ok());

    auto source_scene = scene();
    source_scene.document.id = {.value = "source-scene"};
    source_scene.document.name = "Source Scene";
    source_scene.transitions.push_back({"open-target",
                                        {{.value = "target-scene"}, "scene"},
                                        "start",
                                        fabric::core::ResourceId{.value = "open-door"}});
    auto target_scene = scene();
    target_scene.document.id = {.value = "target-scene"};
    target_scene.document.name = "Target Scene";
    target_scene.maps.front().map.id = {.value = "target-map"};
    target_scene.entry_map->id = {.value = "target-map"};
    REQUIRE(fabric::project::publish_scene(root, project_manifest, source_scene).ok());
    REQUIRE(fabric::project::publish_scene(root, project_manifest, target_scene).ok());

    fabric::runtime::SceneRuntimeSession session;
    REQUIRE(session.load(root, {.value = "source-scene"}));
    bool transitioned = false;
    for (int pass = 0; pass < 2; ++pass) {
        fabric::runtime::PreviewRuntime runtime;
        REQUIRE(runtime.load({.project_root = root,
                              .scene_id = session.scene()->document.id,
                              .gameplay_event_handler = [&](const auto& event) {
                                  if (!session.transition_for_event(event.id)) return true;
                                  transitioned = true;
                                  return false;
                              },
                              .enable_character = true,
                              .mode = fabric::runtime::RuntimeMode::smoke_test}));
        REQUIRE(runtime.run());
    }
    REQUIRE(transitioned);
    REQUIRE(session.scene().has_value());
    REQUIRE(session.map().has_value());
    CHECK(session.scene()->document.id.value == "target-scene");
    CHECK(session.map()->document.id.value == "target-map");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime loads and evaluates project animations") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-animation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());
    REQUIRE(fabric::project::publish_animation(
        root, manifest(), transform_animation()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.animation_count() == 1U);
    const auto evaluated = runtime.evaluate_animation(
        {.value = "runtime-animation"}, 0.5F);
    REQUIRE(evaluated.has_value());
    REQUIRE(evaluated->ok());
    REQUIRE(evaluated->properties.size() == 3U);
    const auto position = std::get<fabric::core::Vec2>(
        evaluated->properties.front().value);
    CHECK(position == fabric::core::Vec2{1.0F, 2.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime evaluates entity animation state transitions") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-state-machine-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_state_machine_entity()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), animation()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), state_machine_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto evaluated = runtime.evaluate_instance_animation("state-machine", 0.75F);
    REQUIRE(evaluated.has_value());
    REQUIRE(evaluated->ok());
    REQUIRE(evaluated->properties.size() == 1U);
    CHECK(std::get<fabric::core::Vec2>(evaluated->properties.front().value) ==
          fabric::core::Vec2{0.5F, 1.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime resolves an animation assigned to a map instance") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-instance-animation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_animated_entity()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), animation()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto evaluated = runtime.evaluate_instance_animation("marker", 0.5F);
    REQUIRE(evaluated.has_value());
    REQUIRE(evaluated->ok());
    REQUIRE(evaluated->properties.size() == 1U);
    const auto position = std::get<fabric::core::Vec2>(
        evaluated->properties.front().value);
    CHECK(position == fabric::core::Vec2{1.0F, 2.0F});
    CHECK_FALSE(runtime.evaluate_instance_animation("missing", 0.5F).has_value());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime loads per-instance deformation and XPBD state headlessly") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-simulation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_simulated_entity()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), simulated_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.stats().deformation_instances == 1U);
    const auto deformation = runtime.evaluate_instance_deformation("simulated");
    REQUIRE(deformation.has_value());
    REQUIRE(deformation->ok());
    REQUIRE(deformation->positions.size() == 2U);
    CHECK(deformation->positions[0] == fabric::core::Vec2{0.0F, 0.0F});
    CHECK(deformation->positions[1] == fabric::core::Vec2{2.0F, 0.0F});
    const auto xpbd = runtime.instance_xpbd_state("simulated");
    REQUIRE(xpbd.has_value());
    REQUIRE(xpbd->particles.size() == 2U);
    CHECK_FALSE(runtime.evaluate_instance_deformation("missing").has_value());
    REQUIRE(runtime.run());
    CHECK(runtime.stats().xpbd_steps == 1U);
    const auto solved_deformation = runtime.evaluate_instance_deformation("simulated");
    REQUIRE(solved_deformation.has_value());
    REQUIRE(solved_deformation->positions.size() == 2U);
    CHECK(solved_deformation->positions[0] == fabric::core::Vec2{0.5F, 0.0F});
    CHECK(solved_deformation->positions[1] == fabric::core::Vec2{1.5F, 0.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime inherits animation bindings from prefabs") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-prefab-animation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_animated_prefab()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), animation()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto evaluated = runtime.evaluate_instance_animation("marker", 0.5F);
    REQUIRE(evaluated.has_value());
    REQUIRE(evaluated->ok());
    CHECK_FALSE(runtime.evaluate_instance_animation("missing", 0.5F).has_value());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime applies deformation positions to matching draw packets") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-render-deformation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_render_deformed_entity()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), render_deformed_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    CHECK(runtime.stats().deformed_packets > 0U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime resolves ordered entity constraints before deformation") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-constraints-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_constrained_entity()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), constrained_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto deformation = runtime.evaluate_instance_deformation("constrained");
    REQUIRE(deformation.has_value());
    REQUIRE(deformation->ok());
    REQUIRE(deformation->positions.size() == 1U);
    CHECK(deformation->positions.front() == fabric::core::Vec2{3.0F, 4.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime applies animation transforms to deformation poses") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-animated-deformation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_animated_render_deformed_entity()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), animation()).ok());
    auto source = render_deformed_entity();
    REQUIRE(fabric::project::publish_entity(root, manifest(), source).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto deformation = runtime.evaluate_instance_deformation("deformed", 0.5F);
    REQUIRE(deformation.has_value());
    REQUIRE(deformation->ok());
    REQUIRE(deformation->positions.size() == 4U);
    CHECK(deformation->positions.front() == fabric::core::Vec2{0.0F, 1.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime evaluates animation before entity constraints") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-animation-constraints-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    auto animated_map = map_with_constrained_entity();
    animated_map.instances.front().properties.push_back({
        "animation", fabric::project::ResourceReference{
            {.value = "runtime-animation"}, "animation"}});
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), animated_map).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), animation()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), constrained_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto deformation = runtime.evaluate_instance_deformation("constrained", 0.5F);
    REQUIRE(deformation.has_value());
    REQUIRE(deformation->ok());
    REQUIRE(deformation->positions.size() == 1U);
    CHECK(deformation->positions.front() == fabric::core::Vec2{1.0F, 2.0F});
    const auto nodes = runtime.evaluate_instance_nodes("constrained", 0.5F);
    REQUIRE(nodes.has_value());
    const auto target = std::find_if(nodes->begin(), nodes->end(),
        [](const auto& node) { return node.id == "target"; });
    REQUIRE(target != nodes->end());
    CHECK(target->transform.position == fabric::core::Vec2{1.0F, 2.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime resolves persisted FABRIK chains before deformation") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-ik-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_ik_entity()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), ik_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto deformation = runtime.evaluate_instance_deformation("ik");
    REQUIRE(deformation.has_value());
    REQUIRE(deformation->ok());
    REQUIRE(deformation->positions.size() == 1U);
    CHECK(deformation->positions.front() == fabric::core::Vec2{0.0F, 1.0F});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime rejects invalid projects before loading") {
    fabric::runtime::PreviewRuntime runtime;
    REQUIRE_FALSE(runtime.load({.project_root = "/definitely/missing/project",
                                 .map_id = {.value = "preview"}}));
    REQUIRE_FALSE(runtime.loaded());
    REQUIRE_FALSE(runtime.errors().empty());
}

TEST_CASE("preview runtime consumes a replay on fixed physics frames") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-replay-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());
    REQUIRE(fabric::project::publish_replay(root, manifest(), replay()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .replay_id = fabric::core::ResourceId{.value = "smoke-replay"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test,
                          .frame_limit = 2}));
    REQUIRE(runtime.replay().has_value());
    REQUIRE(runtime.run());
    CHECK(runtime.stats().replay_events == 1);
    CHECK(runtime.stats().replay_checkpoints == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime drives the character controller from replay actions") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-character-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map()).ok());
    auto movement = replay();
    movement.document.id.value = "movement-replay";
    movement.inputs = {{0, "move_right", true, false}};
    movement.events.clear();
    movement.checkpoints.clear();
    REQUIRE(fabric::project::publish_replay(root, project_manifest, movement).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .replay_id = fabric::core::ResourceId{.value = "movement-replay"},
                          .enable_character = true,
                          .mode = fabric::runtime::RuntimeMode::smoke_test,
                          .frame_limit = 2}));
    REQUIRE(runtime.run());
    CHECK(runtime.stats().character_x > 0.0F);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime resolves native vector entity drawables") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-drawables-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.stats().vector_geometry_cache_entries == 1U);
    REQUIRE(runtime.run());
    REQUIRE(runtime.stats().visible_instances == 1);
    CHECK(runtime.stats().culled_packets >= 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime sorts packets by layer depth and node z order") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-packet-order-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(root, manifest(), vector_asset()).ok());
    auto source = ordered_entity();
    source.document.id = {.value = "ordered-entity"};
    REQUIRE(fabric::project::publish_entity(root, manifest(), source).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_ordered_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    const auto order = runtime.packet_order();
    REQUIRE(order.size() == 2U);
    CHECK(order.front().starts_with("ordered:front:"));
    CHECK(order.back().starts_with("ordered:root:"));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime uploads texture entity drawables") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-texture-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    const auto input = root / "input.png";
    constexpr std::array<unsigned char, 70> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0x60, 0xf8, 0xcf, 0xc0,
        0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xdd,
        0x8d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::ofstream output(input, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
    output.close();
    REQUIRE(fabric::project::publish_texture_asset(
        root, manifest(),
        {.document = {.schema_version = 1,
                      .type = "texture",
                      .id = {.value = "runtime-texture"},
                      .name = "Runtime Texture"},
         .source = "assets/textures/runtime-texture.png",
         .width = 1,
         .height = 1}, input).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), texture_entity()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_texture_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.stats().visible_instances == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace
