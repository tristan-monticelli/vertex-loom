#include "fabric/runtime/preview_runtime.hpp"
#include "fabric/runtime/scene_session.hpp"

#include "fabric/project/entity.hpp"
#include "fabric/project/behavior_graph.hpp"
#include "fabric/project/entity_transformation.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/map_package.hpp"
#include "fabric/project/mechanic_graph.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/visual_composition.hpp"
#include "fabric/render/vector_geometry.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <catch2/catch_approx.hpp>
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
            .loop = true,
            .markers = {{"start", 0.0F}, {"end", 1.0F}},
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

fabric::project::AnimationClip material_animation() {
    fabric::project::AnimationClip result{
        .document = {.schema_version = 1,
                     .type = "animation",
                     .id = {.value = "runtime-animation"},
                     .name = "Runtime Material Animation"},
        .duration = 1.0F,
        .loop = true};
    result.tracks.push_back({
        {.node_id = "root", .component_id = "material", .property_id = "color"},
        fabric::project::AnimationInterpolation::linear,
        {{0.0F, fabric::core::Color{0.1F, 0.1F, 0.1F, 0.1F}},
         {1.0F, fabric::core::Color{0.1F, 0.1F, 0.1F, 0.1F}}},
        fabric::project::AnimationComposition::additive});
    result.tracks.push_back({
        {.node_id = "root", .component_id = "material", .property_id = "opacity"},
        fabric::project::AnimationInterpolation::linear,
        {{0.0F, 0.1F}, {1.0F, 0.1F}},
        fabric::project::AnimationComposition::additive});
    return result;
}

fabric::project::AnimationClip component_animation() {
    return {.document = {.schema_version = 1,
                         .type = "animation",
                         .id = {.value = "runtime-animation"},
                         .name = "Runtime Component Animation"},
            .duration = 1.0F,
            .loop = true,
            .tracks = {{{.node_id = "root",
                         .component_id = "runtime-component",
                         .property_id = "scale"},
                        fabric::project::AnimationInterpolation::linear,
                        {{0.0F, fabric::core::Vec2{4.0F, 4.0F}},
                         {1.0F, fabric::core::Vec2{4.0F, 4.0F}}}}}};
}

fabric::project::MaterialDefinition material() {
    return {.document = {.schema_version = 1,
                         .type = "material",
                         .id = {.value = "runtime-material"},
                         .name = "Runtime Material"},
            .color = {0.5F, 0.5F, 0.5F, 0.5F},
            .opacity = 0.5F};
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

fabric::project::BehaviorGraph shared_behavior() {
    using Direction = fabric::project::BehaviorPortDirection;
    using Type = fabric::project::BehaviorValueType;
    fabric::project::BehaviorGraph result;
    result.document.id = {.value = "runtime-behavior"};
    result.document.name = "Runtime Behavior";
    for (const auto& [id, type] : std::array{
             std::pair{"player", "action_source"},
             std::pair{"monster", "ai_source"},
             std::pair{"map", "event_source"}}) {
        result.nodes.push_back({.id = id, .type = type,
            .ports = {{"out", Direction::output, Type::signal}},
            .properties = {{"semantic_id", std::string{"advance"}}}});
        result.connections.push_back({.id = std::string{id} + "-move",
            .from_node = id, .from_port = "out", .to_node = "move",
            .to_port = "in"});
    }
    result.nodes.push_back({.id = "move", .type = "move",
        .ports = {{"in", Direction::input, Type::signal},
                  {"out", Direction::output, Type::signal}},
        .properties = {{"vector", fabric::core::Vec2{6.0F, 0.0F}}}});
    return result;
}

fabric::project::BehaviorGraph transformation_behavior() {
    using Direction = fabric::project::BehaviorPortDirection;
    using Type = fabric::project::BehaviorValueType;
    fabric::project::BehaviorGraph result;
    result.document.id = {.value = "transformation-behavior"};
    result.document.name = "Transformation Behavior";
    result.nodes = {
        {.id = "seed", .type = "event_source",
         .ports = {{"out", Direction::output, Type::signal}},
         .properties = {{"semantic_id", std::string{"seed"}}}},
        {.id = "seed-health", .type = "set_property",
         .ports = {{"in", Direction::input, Type::signal},
                   {"out", Direction::output, Type::signal}},
         .properties = {{"target", std::string{"health"}},
                        {"value", std::int64_t{42}}}},
        {.id = "transform", .type = "event_source",
         .ports = {{"out", Direction::output, Type::signal}},
         .properties = {{"semantic_id", std::string{"transform"}}}},
        {.id = "become-beast", .type = "transform_entity",
         .ports = {{"in", Direction::input, Type::signal},
                   {"out", Direction::output, Type::signal}},
         .properties = {{"transformation",
             fabric::project::ResourceReference{
                 {.value = "hero-to-beast"}, "transformation"}}}},
    };
    result.connections = {
        {.id = "seed-flow", .from_node = "seed", .from_port = "out",
         .to_node = "seed-health", .to_port = "in"},
        {.id = "transform-flow", .from_node = "transform", .from_port = "out",
         .to_node = "become-beast", .to_port = "in"},
    };
    return result;
}

fabric::project::BehaviorGraph replay_transformation_behavior() {
    using Direction = fabric::project::BehaviorPortDirection;
    using Type = fabric::project::BehaviorValueType;
    fabric::project::BehaviorGraph result;
    result.document.id = {.value = "replay-transformation-behavior"};
    result.document.name = "Replay Transformation Behavior";
    result.nodes = {
        {.id = "morph", .type = "action_source",
         .ports = {{"out", Direction::output, Type::signal}},
         .properties = {{"semantic_id", std::string{"morph"}}}},
        {.id = "transform", .type = "transform_entity",
         .ports = {{"in", Direction::input, Type::signal},
                   {"out", Direction::output, Type::signal}},
         .properties = {{"transformation",
             fabric::project::ResourceReference{
                 {.value = "hero-to-beast"}, "transformation"}}}},
    };
    result.connections = {{.id = "morph-flow", .from_node = "morph",
        .from_port = "out", .to_node = "transform", .to_port = "in"}};
    return result;
}

fabric::project::MechanicValue mechanic_default_value(
    const fabric::project::MechanicValueType type,
    const std::string_view id) {
    using Type = fabric::project::MechanicValueType;
    switch (type) {
    case Type::boolean: return false;
    case Type::integer: return std::int64_t{};
    case Type::scalar: return 0.0F;
    case Type::text:
        if (id == "body-type") return std::string{"dynamic"};
        return std::string{"value"};
    case Type::vec2: return fabric::core::Vec2{};
    case Type::resource:
        return fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"};
    case Type::body_handle:
    case Type::pivot_handle:
    case Type::joint_handle: break;
    }
    return false;
}

fabric::project::MechanicNodeDefinition mechanic_node(
    const fabric::project::MechanicNodeKind kind, std::string id) {
    const auto& schema = fabric::project::mechanic_node_schema(kind);
    fabric::project::MechanicNodeDefinition result{
        .id = std::move(id), .type = std::string{schema.type}};
    for (const auto& port : schema.ports)
        result.ports.push_back({
            .id = std::string{port.id}, .name = std::string{port.id},
            .direction = port.direction, .type = port.type});
    for (const auto& property : schema.properties)
        if (property.required)
            result.properties.push_back({
                .id = std::string{property.id},
                .value = mechanic_default_value(property.type, property.id)});
    return result;
}

void set_mechanic_property(fabric::project::MechanicNodeDefinition& node,
                           const std::string& id,
                           fabric::project::MechanicValue value) {
    const auto property = std::ranges::find(
        node.properties, id, &fabric::project::MechanicNodeProperty::id);
    if (property == node.properties.end())
        node.properties.push_back({id, std::move(value)});
    else
        property->value = std::move(value);
}

fabric::project::MechanicGraph always_running_mechanic() {
    using Kind = fabric::project::MechanicNodeKind;
    auto body = mechanic_node(Kind::body, "platform");
    set_mechanic_property(body, "position", fabric::core::Vec2{0.0F, 0.0F});
    set_mechanic_property(body, "size", fabric::core::Vec2{2.0F, 2.0F});
    set_mechanic_property(body, "density", 1.0F);
    set_mechanic_property(body, "friction", 0.8F);
    set_mechanic_property(body, "entity",
        fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"});
    auto pivot = mechanic_node(Kind::pivot, "anchor");
    set_mechanic_property(pivot, "position", fabric::core::Vec2{0.0F, 0.0F});
    auto joint = mechanic_node(Kind::joint, "hinge");
    auto motor = mechanic_node(Kind::motor, "drive");
    set_mechanic_property(motor, "speed", 90.0F);
    set_mechanic_property(motor, "max-torque", 100.0F);
    return {
        .document = {.schema_version = 1,
                     .type = "mechanic",
                     .id = {.value = "runtime-mechanic"},
                     .name = "Runtime Mechanic"},
        .nodes = {std::move(body), std::move(pivot), std::move(joint),
                  std::move(motor)},
        .connections = {{"platform", "body", "anchor", "body"},
                        {"platform", "body", "hinge", "body-a"},
                        {"anchor", "pivot", "hinge", "pivot"},
                        {"hinge", "joint", "drive", "joint"}}};
}

fabric::project::MapDocument map_with_mechanic_instance() {
    auto result = map();
    result.prefabs.push_back({
        .id = "runtime-prefab",
        .entity = {{.value = "runtime-entity"}, "entity"},
        .mechanic = fabric::project::ResourceReference{
            {.value = "runtime-mechanic"}, "mechanic"}});
    result.instances.push_back({
        .id = "runtime-instance",
        .prefab = fabric::project::ResourceReference{
            {.value = "runtime-prefab"}, "prefab"},
        .layer_id = "instances",
        .transform = {.position = {3.0F, 2.0F}}});
    return result;
}

fabric::project::VisualComposition visual_composition() {
    return {.document = {.schema_version = 1,
                         .type = "visualComposition",
                         .id = {.value = "runtime-composition"},
                         .name = "Runtime Composition"},
            .size = {2.0F, 2.0F},
            .layers = {{.id = "artwork", .name = "Artwork",
                        .kind = fabric::project::VisualLayerKind::vector,
                        .resource = {{.value = "runtime-vector"}, "vector"}}}};
}

fabric::project::VisualComponent visual_component() {
    return {.document = {.schema_version = 1,
                         .type = "visualComponent",
                         .id = {.value = "runtime-component"},
                         .name = "Runtime Component"},
            .composition = {{.value = "runtime-composition"},
                            "visualComposition"},
            .parameters = {{"scale", "Scale",
                            fabric::project::VisualParameterType::vec2,
                            fabric::core::Vec2{1.0F, 1.0F},
                            {"artwork", "transform", "scale"}, true}}};
}

fabric::project::EntityDefinition component_entity() {
    return {.document = {.schema_version =
                             fabric::project::current_entity_schema_version,
                         .type = "entity",
                         .id = {.value = "runtime-component-entity"},
                         .name = "Runtime Component Entity"},
            .nodes = {{.id = "root", .name = "Root",
                       .transform = {.position = {3.0F, 4.0F}},
                       .drawable = {
                           .kind = fabric::project::EntityDrawableKind::visual_component,
                           .resource = fabric::project::ResourceReference{
                               {.value = "runtime-component"}, "visualComponent"},
                           .component_instance =
                               fabric::project::VisualComponentInstance{
                                   .overrides = {{"scale",
                                       fabric::core::Vec2{2.0F, 2.0F}}}}}}}};
}

fabric::project::MapDocument map_with_component_entity() {
    auto result = map();
    result.instances.push_back({
        "component", fabric::project::ResourceReference{
            {.value = "runtime-component-entity"}, "entity"},
        std::nullopt, "instances", {}, 0, 0, {}});
    return result;
}

fabric::project::MapDocument map_with_animated_component_entity() {
    auto result = map_with_component_entity();
    result.instances.front().properties.push_back({
        "animation", fabric::project::ResourceReference{
            {.value = "runtime-animation"}, "animation"}});
    return result;
}

fabric::project::EntityDefinition material_entity() {
    auto result = entity();
    result.nodes.front().drawable.material =
        fabric::project::ResourceReference{{.value = "runtime-material"}, "material"};
    return result;
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
        .id = "animated",
        .entity = {{.value = "runtime-entity"}, "entity"},
        .overrides = {{"animation", fabric::project::ResourceReference{
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

TEST_CASE("preview runtime loads the published map package directly") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-package-source-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto package = root / "published.map-package";
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());
    REQUIRE(fabric::project::publish_map_package(root, {.value = "preview"}, package).ok());

    fabric::runtime::PreviewRuntime runtime;
    const auto loaded = runtime.load({.package_root = package,
                                      .mode = fabric::runtime::RuntimeMode::smoke_test});
    if (!loaded) for (const auto& error : runtime.errors()) std::cerr << error << '\n';
    REQUIRE(loaded);
    REQUIRE(runtime.loaded());
    REQUIRE(runtime.map()->document.id.value == "preview");
    REQUIRE(runtime.errors().empty());
    REQUIRE(runtime.run());
    REQUIRE(runtime.stats().frames == 1);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime loads root and selected scenes from a campaign package") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-scene-package-source-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto package = root.string() + "-published.scene-package";
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map()).ok());
    auto root_scene = scene();
    root_scene.transitions.push_back(
        {"continue", {{.value = "target-scene"}, "scene"}, "start",
         std::nullopt});
    auto target_scene = scene();
    target_scene.document.id = {.value = "target-scene"};
    target_scene.document.name = "Target Scene";
    REQUIRE(fabric::project::publish_scene(root, manifest(), root_scene).ok());
    REQUIRE(fabric::project::publish_scene(root, manifest(), target_scene).ok());
    REQUIRE(fabric::project::publish_scene_package(
        root, {.value = "preview-scene"}, package).ok());

    fabric::runtime::PreviewRuntime root_runtime;
    REQUIRE(root_runtime.load({
        .package_root = package,
        .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(root_runtime.scene().has_value());
    CHECK(root_runtime.scene()->document.id.value == "preview-scene");
    CHECK(root_runtime.map()->document.id.value == "preview-scene");
    REQUIRE(root_runtime.run());

    fabric::runtime::PreviewRuntime target_runtime;
    REQUIRE(target_runtime.load({
        .package_root = package,
        .package_scene_id = fabric::core::ResourceId{.value = "target-scene"},
        .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(target_runtime.scene().has_value());
    CHECK(target_runtime.scene()->document.id.value == "target-scene");
    CHECK(target_runtime.map()->document.id.value == "target-scene");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::remove_all(package, ignored);
}

TEST_CASE("preview runtime resolves a scene entry map before graphics") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-scene-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map()).ok());
    auto overlay = map();
    overlay.document.id = {.value = "overlay"};
    overlay.document.name = "Overlay";
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, overlay).ok());
    auto multi_map_scene = scene();
    multi_map_scene.maps.push_back(
        {{{.value = "overlay"}, "map"}, "overlay"});
    REQUIRE(fabric::project::publish_scene(
        root, project_manifest, multi_map_scene).ok());

    fabric::runtime::PreviewRuntime runtime;
    const std::map<std::string, fabric::project::ProgressValue> progress{
        {"has-key", true}, {"coins", std::int64_t{12}}};
    REQUIRE(runtime.load({.project_root = root,
                          .scene_id = fabric::core::ResourceId{.value = "preview-scene"},
                          .progress_properties = progress,
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.scene().has_value());
    REQUIRE(runtime.map().has_value());
    CHECK(runtime.scene()->document.id.value == "preview-scene");
    CHECK(runtime.map()->document.id.value == "preview-scene");
    REQUIRE(runtime.map()->layers.size() == 2U);
    CHECK(runtime.map()->layers[0].id == "instances-instances");
    CHECK(runtime.map()->layers[1].id == "overlay-instances");
    CHECK(runtime.progress_properties() == progress);
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

TEST_CASE("preview runtime emits trigger events for map entities without a CLI character") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-entity-trigger-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, project_manifest, vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, project_manifest, entity()).ok());
    auto trigger_map = map_with_gameplay_trigger();
    trigger_map.triggers.front().properties = {
        {"source", std::string{"entity-zone"}}};
    trigger_map.instances.push_back({
        .id = "monster-one",
        .entity = fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"},
        .layer_id = "instances",
        .transform = {.position = {0.0F, 0.0F}},
        .properties = {{"triggerHalfExtents",
                        fabric::core::Vec2{0.75F, 0.75F}}}});
    REQUIRE(fabric::project::publish_map(
        root, project_manifest, trigger_map).ok());

    std::optional<fabric::runtime::GameplayEvent> received;
    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({
        .project_root = root,
        .map_id = {.value = "preview"},
        .gameplay_event_handler = [&](const auto& event) {
            received = event;
            return false;
        },
        .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(received.has_value());
    CHECK(received->actor_id == "monster-one");
    REQUIRE(received->payload.size() == 1U);
    CHECK(std::get<std::string>(received->payload.front().value) ==
          "entity-zone");

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
                              {"west", {{fabric::runtime::InputDevice::keyboard, 74}}},
                              {"east", {{fabric::runtime::InputDevice::keyboard, 76}}},
                              {"leap", {{fabric::runtime::InputDevice::gamepad, 1}}}},
                          .enable_character = true,
                          .character_actions = fabric::runtime::PreviewRuntimeOptions::CharacterActions{
                              .left = "west", .right = "east", .jump = "leap"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    CHECK(runtime.stats().physics_steps == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime loads the default persisted locomotion bindings") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-persisted-input-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto project_manifest = manifest();
    REQUIRE(fabric::project::create_project(root, project_manifest).ok());
    REQUIRE(fabric::project::publish_map(root, project_manifest, map()).ok());
    const fabric::project::InputDocument input{
        .document = {.schema_version = fabric::project::current_input_schema_version,
                     .type = "input",
                     .id = {.value = "default"},
                     .name = "Default Input"},
        .actions = {
            {"west", {{fabric::project::InputDevice::keyboard, 74}}},
            {"east", {{fabric::project::InputDevice::keyboard, 76}}},
            {"leap", {{fabric::project::InputDevice::gamepad, 1}}}}};
    REQUIRE(fabric::project::publish_input(root, project_manifest, input).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root,
                          .map_id = {.value = "preview"},
                          .enable_character = true,
                          .character_actions = fabric::runtime::PreviewRuntimeOptions::CharacterActions{
                              .left = "west", .right = "east", .jump = "leap"},
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
    target_map.instances.push_back({
        .id = "start-marker",
        .entity = fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"},
        .layer_id = "instances",
        .transform = {.position = {9.0F, 4.0F}},
        .properties = {{"sceneEntryPoint", std::string{"start"}}}});
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, project_manifest, vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, project_manifest, entity()).ok());
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
    fabric::core::Vec2 final_character_position;
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
                              .character_spawn = session.entry_point()
                                  ? std::optional<fabric::core::Vec2>{
                                        session.entry_point()->position}
                                  : std::nullopt,
                              .mode = fabric::runtime::RuntimeMode::smoke_test}));
        REQUIRE(runtime.run());
        final_character_position = {runtime.stats().character_x,
                                    runtime.stats().character_y};
    }
    REQUIRE(transitioned);
    REQUIRE(session.scene().has_value());
    REQUIRE(session.map().has_value());
    CHECK(session.scene()->document.id.value == "target-scene");
    CHECK(session.map()->document.id.value == "target-scene");
    REQUIRE(session.entry_point().has_value());
    CHECK(session.entry_point()->position == fabric::core::Vec2{9.0F, 4.0F});
    CHECK(final_character_position.x == Catch::Approx(9.0F));
    CHECK(final_character_position.y > 3.9F);

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
    const auto marker_hits = runtime.animation_markers(
        {.value = "runtime-animation"}, 0.25F, 1.25F);
    REQUIRE(marker_hits.size() == 2U);
    CHECK(marker_hits.front().id == "end");
    CHECK(marker_hits.back().id == "start");
    CHECK(marker_hits.back().loop_index == 1);

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
    const auto state = runtime.evaluate_instance_state("state-machine", 0.75F);
    REQUIRE(state.has_value());
    CHECK(state->state_id == "run");
    CHECK(state->clip_id.value == "runtime-animation");
    CHECK(state->local_time == 0.25F);
    CHECK_FALSE(runtime.evaluate_instance_state("missing", 0.75F).has_value());

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

TEST_CASE("preview runtime emits crossed markers for animated instances") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-marker-events-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source_animation = animation();
    source_animation.markers = {{"frame", 0.01F}};
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_animated_entity()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), source_animation).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.animation_marker_events().size() == 1U);
    const auto& event = runtime.animation_marker_events().front();
    CHECK(event.instance_id == "marker");
    CHECK(event.clip_id.value == "runtime-animation");
    CHECK(event.marker.id == "frame");
    CHECK(runtime.stats().animation_marker_events == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime emits markers for state machine clips") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-state-marker-events-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    auto source_animation = animation();
    source_animation.markers = {{"state-frame", 0.01F}};
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_state_machine_entity()).ok());
    REQUIRE(fabric::project::publish_animation(root, manifest(), source_animation).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), state_machine_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.animation_marker_events().size() == 1U);
    CHECK(runtime.animation_marker_events().front().instance_id == "state-machine");
    CHECK(runtime.animation_marker_events().front().marker.id == "state-frame");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime applies animated material tracks to submitted packets") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-material-animation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_animated_entity()).ok());
    REQUIRE(fabric::project::publish_animation(
        root, manifest(), material_animation()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_material(root, manifest(), material()).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), material_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    const auto& packets = runtime.last_frame_packets();
    REQUIRE(packets.size() == 1U);
    REQUIRE(packets.front().fill_color.has_value());
    const auto& color = *packets.front().fill_color;
    CHECK(color == fabric::core::Color{0.6F, 0.2F, 0.15F, 0.45F});

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
    movement.inputs = {{0, "advance", true, false}};
    movement.events.clear();
    movement.checkpoints.clear();
    REQUIRE(fabric::project::publish_replay(root, project_manifest, movement).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .replay_id = fabric::core::ResourceId{.value = "movement-replay"},
                          .input_actions = {{"retreat", {}}, {"advance", {}},
                                            {"ascend", {}}},
                          .enable_character = true,
                          .character_actions = fabric::runtime::PreviewRuntimeOptions::CharacterActions{
                              .left = "retreat", .right = "advance", .jump = "ascend"},
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

TEST_CASE("published Preview Runtime evaluates one attached behavior for player AI and map") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-behavior-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto package = root.string() + "-package";
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_behavior_graph(
        root, manifest(), shared_behavior()).ok());
    auto actor = entity();
    actor.behavior = fabric::project::ResourceReference{
        {.value = "runtime-behavior"}, "behavior"};
    REQUIRE(fabric::project::publish_entity(root, manifest(), actor).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_entity()).ok());
    REQUIRE(fabric::project::publish_map_package(
        root, {.value = "preview"}, package).ok());
    REQUIRE(std::filesystem::is_regular_file(
        std::filesystem::path{package} /
        "assets/behaviors/runtime-behavior.behavior.json"));

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.package_root = package,
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    for (const auto source : {
             fabric::runtime::BehaviorSignalSource::action,
             fabric::runtime::BehaviorSignalSource::ai_decision,
             fabric::runtime::BehaviorSignalSource::map_event}) {
        const auto actions = runtime.evaluate_instance_behavior(
            "marker", {source, "advance", {}}, 1.0F / 60.0F);
        REQUIRE(actions);
        REQUIRE(actions->size() == 1U);
        CHECK(actions->front().kind == fabric::runtime::BehaviorActionKind::move);
    }
    CHECK(runtime.stats().behavior_actions == 3U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::remove_all(package, ignored);
}

TEST_CASE("published behavior transforms an entity atomically with mapped state") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-transformation-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto package = root.string() + "-package";
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_behavior_graph(
        root, manifest(), transformation_behavior()).ok());
    auto hero = entity();
    hero.behavior = fabric::project::ResourceReference{
        {.value = "transformation-behavior"}, "behavior"};
    REQUIRE(fabric::project::publish_entity(root, manifest(), hero).ok());
    auto beast = entity();
    beast.document.id = {.value = "beast-entity"};
    beast.document.name = "Beast Entity";
    beast.behavior.reset();
    beast.nodes.front().transform.position = {20.0F, 0.0F};
    REQUIRE(fabric::project::publish_entity(root, manifest(), beast).ok());
    fabric::project::EntityTransformation transformation;
    transformation.document.id = {.value = "hero-to-beast"};
    transformation.document.name = "Hero to Beast";
    transformation.source_entity = {{.value = "runtime-entity"}, "entity"};
    transformation.destination_entity = {{.value = "beast-entity"}, "entity"};
    transformation.policy.properties = fabric::project::TransferMode::mapping;
    transformation.policy.mappings.push_back({
        fabric::project::TransferDomain::property, "health", "hit-points"});
    REQUIRE(fabric::project::publish_entity_transformation(
        root, manifest(), transformation).ok());
    auto source_map = map();
    source_map.instances.push_back({
        "hero", fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"},
        std::nullopt, "instances", {.position = {7.0F, 9.0F}}, 0, 0, {}});
    REQUIRE(fabric::project::publish_map(root, manifest(), source_map).ok());
    REQUIRE(fabric::project::publish_map_package(
        root, {.value = "preview"}, package).ok());
    REQUIRE(std::filesystem::is_regular_file(
        std::filesystem::path{package} /
        "assets/transformations/hero-to-beast.transformation.json"));
    REQUIRE(std::filesystem::is_regular_file(
        std::filesystem::path{package} /
        "entities/beast-entity.entity.json"));

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.package_root = package,
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.instance_entity_id("hero") ==
            fabric::core::ResourceId{.value = "runtime-entity"});
    REQUIRE(runtime.evaluate_instance_behavior(
        "hero", {fabric::runtime::BehaviorSignalSource::map_event, "seed", {}},
        1.0F / 60.0F));
    REQUIRE(runtime.instance_property("hero", "health") ==
            fabric::project::BehaviorValue{std::int64_t{42}});
    const auto actions = runtime.evaluate_instance_behavior(
        "hero", {fabric::runtime::BehaviorSignalSource::map_event,
                   "transform", {}}, 1.0F / 60.0F);
    REQUIRE(actions);
    REQUIRE(actions->size() == 1U);
    CHECK(actions->front().kind ==
          fabric::runtime::BehaviorActionKind::transform_entity);
    REQUIRE(runtime.instance_entity_id("hero") ==
            fabric::core::ResourceId{.value = "beast-entity"});
    REQUIRE(runtime.instance_property("hero", "hit-points") ==
            fabric::project::BehaviorValue{std::int64_t{42}});
    CHECK_FALSE(runtime.instance_property("hero", "health"));
    REQUIRE(runtime.run());
    REQUIRE(runtime.last_frame_packets().size() == 1U);
    CHECK(runtime.last_frame_packets().front().fill_vertices.front().x ==
          Catch::Approx(26.0F));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::remove_all(package, ignored);
}

TEST_CASE("failed transformation leaves the complete source instance intact") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-transformation-failure-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_behavior_graph(
        root, manifest(), transformation_behavior()).ok());
    auto hero = entity();
    hero.behavior = fabric::project::ResourceReference{
        {.value = "transformation-behavior"}, "behavior"};
    REQUIRE(fabric::project::publish_entity(root, manifest(), hero).ok());
    auto beast = entity();
    beast.document.id = {.value = "beast-entity"};
    beast.document.name = "Beast Entity";
    beast.behavior.reset();
    REQUIRE(fabric::project::publish_entity(root, manifest(), beast).ok());
    auto transformation = fabric::project::EntityTransformation{};
    transformation.document.id = {.value = "hero-to-beast"};
    transformation.document.name = "Invalid Hero to Beast";
    transformation.source_entity = {{.value = "runtime-entity"}, "entity"};
    transformation.destination_entity = {{.value = "beast-entity"}, "entity"};
    transformation.policy.properties = fabric::project::TransferMode::mapping;
    transformation.policy.incompatible_values = fabric::project::TransferMode::error;
    transformation.policy.mappings.push_back({
        fabric::project::TransferDomain::property, "missing", "hit-points"});
    REQUIRE(fabric::project::publish_entity_transformation(
        root, manifest(), transformation).ok());
    auto source_map = map();
    source_map.instances.push_back({
        "hero", fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"},
        std::nullopt, "instances", {}, 0, 0, {}});
    REQUIRE(fabric::project::publish_map(root, manifest(), source_map).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE_FALSE(runtime.transform_instance(
        "hero", {.value = "hero-to-beast"}));
    CHECK(runtime.instance_entity_id("hero") ==
          fabric::core::ResourceId{.value = "runtime-entity"});
    CHECK(runtime.packet_order().size() == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("runtime supports forward reverse and chained transformations") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-transformation-chain-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    for (const auto& [id, name] : {
             std::pair{"entity-a", "Entity A"},
             std::pair{"entity-b", "Entity B"},
             std::pair{"entity-c", "Entity C"}}) {
        auto value = entity();
        value.document.id = {.value = id};
        value.document.name = name;
        REQUIRE(fabric::project::publish_entity(root, manifest(), value).ok());
    }
    const auto publish = [&](const char* id, const char* source,
                             const char* destination) {
        fabric::project::EntityTransformation value;
        value.document.id = {.value = id};
        value.document.name = id;
        value.source_entity = {{.value = source}, "entity"};
        value.destination_entity = {{.value = destination}, "entity"};
        return fabric::project::publish_entity_transformation(
            root, manifest(), value).ok();
    };
    REQUIRE(publish("a-to-b", "entity-a", "entity-b"));
    REQUIRE(publish("b-to-a", "entity-b", "entity-a"));
    REQUIRE(publish("b-to-c", "entity-b", "entity-c"));
    auto source_map = map();
    source_map.instances.push_back({
        "actor", fabric::project::ResourceReference{
            {.value = "entity-a"}, "entity"},
        std::nullopt, "instances", {}, 0, 0, {}});
    REQUIRE(fabric::project::publish_map(root, manifest(), source_map).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.transform_instance("actor", {.value = "a-to-b"}));
    CHECK(runtime.instance_entity_id("actor") ==
          fabric::core::ResourceId{.value = "entity-b"});
    REQUIRE(runtime.transform_instance("actor", {.value = "b-to-a"}));
    CHECK(runtime.instance_entity_id("actor") ==
          fabric::core::ResourceId{.value = "entity-a"});
    REQUIRE(runtime.transform_instance("actor", {.value = "a-to-b"}));
    REQUIRE(runtime.transform_instance("actor", {.value = "b-to-c"}));
    CHECK(runtime.instance_entity_id("actor") ==
          fabric::core::ResourceId{.value = "entity-c"});
    CHECK_FALSE(runtime.transform_instance("actor", {.value = "a-to-b"}));
    CHECK(runtime.instance_entity_id("actor") ==
          fabric::core::ResourceId{.value = "entity-c"});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("reloaded replay deterministically triggers an entity transformation") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-transformation-replay-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_behavior_graph(
        root, manifest(), replay_transformation_behavior()).ok());
    auto hero = entity();
    hero.behavior = fabric::project::ResourceReference{
        {.value = "replay-transformation-behavior"}, "behavior"};
    REQUIRE(fabric::project::publish_entity(root, manifest(), hero).ok());
    auto beast = entity();
    beast.document.id = {.value = "beast-entity"};
    beast.document.name = "Beast Entity";
    REQUIRE(fabric::project::publish_entity(root, manifest(), beast).ok());
    fabric::project::EntityTransformation transformation;
    transformation.document.id = {.value = "hero-to-beast"};
    transformation.document.name = "Hero to Beast";
    transformation.source_entity = {{.value = "runtime-entity"}, "entity"};
    transformation.destination_entity = {{.value = "beast-entity"}, "entity"};
    REQUIRE(fabric::project::publish_entity_transformation(
        root, manifest(), transformation).ok());
    fabric::project::ReplayDocument replay;
    replay.document.id = {.value = "transformation-replay"};
    replay.document.name = "Transformation Replay";
    replay.build = "test";
    replay.inputs = {{0, "morph", true, false}};
    REQUIRE(fabric::project::publish_replay(root, manifest(), replay).ok());
    auto source_map = map();
    source_map.instances.push_back({
        "hero", fabric::project::ResourceReference{
            {.value = "runtime-entity"}, "entity"},
        std::nullopt, "instances", {}, 0, 0, {}});
    REQUIRE(fabric::project::publish_map(root, manifest(), source_map).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({
        .project_root = root, .map_id = {.value = "preview"},
        .replay_id = fabric::core::ResourceId{.value = "transformation-replay"},
        .input_actions = {{"morph", {}}},
        .mode = fabric::runtime::RuntimeMode::smoke_test, .frame_limit = 1U}));
    REQUIRE(runtime.run());
    CHECK(runtime.instance_entity_id("hero") ==
          fabric::core::ResourceId{.value = "beast-entity"});

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime resolves visual component entity drawables") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-components-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_visual_composition(
        root, manifest(), visual_composition()).ok());
    REQUIRE(fabric::project::publish_visual_component(
        root, manifest(), visual_component()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), component_entity()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_component_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root,
                          .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.last_frame_packets().size() == 1U);
    const auto& point = runtime.last_frame_packets().front().fill_vertices.front();
    CHECK(point.x == Catch::Approx(1.0F));
    CHECK(point.y == Catch::Approx(2.0F));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("preview runtime rebuilds generically animated visual components") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-animated-component-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_visual_composition(
        root, manifest(), visual_composition()).ok());
    REQUIRE(fabric::project::publish_visual_component(
        root, manifest(), visual_component()).ok());
    REQUIRE(fabric::project::publish_entity(
        root, manifest(), component_entity()).ok());
    REQUIRE(fabric::project::publish_animation(
        root, manifest(), component_animation()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_animated_component_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root,
                          .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.last_frame_packets().size() == 1U);
    const auto& point =
        runtime.last_frame_packets().front().fill_vertices.front();
    CHECK(point.x == Catch::Approx(-1.0F));
    CHECK(point.y == Catch::Approx(0.0F));

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
         .height = 1,
         .view = fabric::project::RasterView{
             .crop = {{0.0F, 0.0F}, {0.5F, 1.0F}},
             .pivot = {0.5F, 0.5F},
         }}, input).ok());
    REQUIRE(fabric::project::publish_entity(root, manifest(), texture_entity()).ok());
    REQUIRE(fabric::project::publish_map(root, manifest(), map_with_texture_entity()).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({.project_root = root, .map_id = {.value = "preview"},
                          .mode = fabric::runtime::RuntimeMode::smoke_test}));
    REQUIRE(runtime.run());
    REQUIRE(runtime.stats().visible_instances == 1);
    const auto& packets = runtime.last_frame_packets();
    REQUIRE(packets.size() == 1U);
    REQUIRE(packets.front().image_fill.has_value());
    REQUIRE(packets.front().fill_uv.size() == 4U);
    const auto studio_packet = fabric::render::build_raster_view_draw_packets({
        .node_id = "textured:root",
        .texture = {{.value = "runtime-texture"}, "texture"},
        .source_width = 1U,
        .source_height = 1U,
        .pixels_per_unit = 100.0F,
        .view = fabric::project::RasterView{
            .crop = {{0.0F, 0.0F}, {0.5F, 1.0F}},
            .pivot = {0.5F, 0.5F},
        },
    });
    REQUIRE(studio_packet.ok());
    REQUIRE(studio_packet.packets.size() == 1U);
    CHECK(packets.front() == studio_packet.packets.front());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("published mechanic instances execute and move their visual in runtime") {
    const auto root = std::filesystem::temp_directory_path() /
        ("fabric-preview-mechanic-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto package = root.string() + "-package";
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    REQUIRE(fabric::project::publish_native_vector_asset(
        root, manifest(), vector_asset()).ok());
    REQUIRE(fabric::project::publish_behavior_graph(
        root, manifest(), transformation_behavior()).ok());
    auto platform = entity();
    platform.behavior = fabric::project::ResourceReference{
        {.value = "transformation-behavior"}, "behavior"};
    REQUIRE(fabric::project::publish_entity(root, manifest(), platform).ok());
    auto beast = entity();
    beast.document.id = {.value = "beast-entity"};
    beast.document.name = "Beast Entity";
    REQUIRE(fabric::project::publish_entity(root, manifest(), beast).ok());
    fabric::project::EntityTransformation transformation;
    transformation.document.id = {.value = "hero-to-beast"};
    transformation.document.name = "Platform to Beast";
    transformation.source_entity = {{.value = "runtime-entity"}, "entity"};
    transformation.destination_entity = {{.value = "beast-entity"}, "entity"};
    REQUIRE(fabric::project::publish_entity_transformation(
        root, manifest(), transformation).ok());
    REQUIRE(fabric::project::publish_mechanic_graph(
        root, manifest(), always_running_mechanic()).ok());
    REQUIRE(fabric::project::publish_map(
        root, manifest(), map_with_mechanic_instance()).ok());
    REQUIRE(fabric::project::publish_map_package(
        root, {.value = "preview"}, package).ok());

    fabric::runtime::PreviewRuntime runtime;
    REQUIRE(runtime.load({
        .package_root = package,
        .mode = fabric::runtime::RuntimeMode::smoke_test,
        .frame_limit = 60U}));
    REQUIRE(runtime.mechanic_instance_count() == 1U);
    const auto initial = runtime.mechanic_body_states("runtime-instance");
    REQUIRE(initial.has_value());
    REQUIRE(initial->size() == 1U);
    CHECK(initial->front().position.x == Catch::Approx(3.0F));
    CHECK(initial->front().position.y == Catch::Approx(2.0F));
    CHECK(initial->front().rotation_degrees == Catch::Approx(0.0F));

    REQUIRE(runtime.run());
    const auto moved = runtime.mechanic_body_states("runtime-instance");
    REQUIRE(moved.has_value());
    REQUIRE(moved->size() == 1U);
    CHECK(std::abs(moved->front().rotation_degrees) > 45.0F);
    CHECK(runtime.stats().mechanic_steps == 60U);
    REQUIRE(runtime.last_frame_packets().size() == 1U);
    const auto& moved_point =
        runtime.last_frame_packets().front().fill_vertices.front();
    CHECK(moved_point.x != Catch::Approx(2.0F));
    CHECK(moved_point.y != Catch::Approx(1.0F));
    REQUIRE(runtime.transform_instance(
        "runtime-instance", {.value = "hero-to-beast"}));
    CHECK(runtime.mechanic_instance_count() == 0U);
    CHECK(runtime.instance_entity_id("runtime-instance") ==
          fabric::core::ResourceId{.value = "beast-entity"});
    CHECK(runtime.packet_order().size() == 1U);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::remove_all(package, ignored);
}

} // namespace
