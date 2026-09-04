#include "fabric/editor/map_session.hpp"
#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/mechanic_presets.hpp"
#include "fabric/editor/mechanic_session.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/visual_presets.hpp"
#include "fabric/physics/mechanic_plan.hpp"
#include "fabric/physics/mechanic_simulation.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/render/map_preview.hpp"
#include "fabric/render/visual_composition_renderer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "studio-rotating-platform"},
            .name = "Studio Rotating Platform"};
}

fabric::project::MapDocument map() {
    return {
        .document = {.schema_version = fabric::project::current_map_schema_version,
                     .type = "map", .id = {.value = "platform-preview"},
                     .name = "Platform Preview"},
        .layers = {{"gameplay", "Gameplay",
                    fabric::project::MapLayerKind::gameplay,
                    true, false, 0.0F},
                   {"instances", "Instances",
                    fabric::project::MapLayerKind::instances,
                    true, false, 0.0F}},
        .events = {{{.value = "platform-activate"}, {}}}};
}

std::filesystem::path temporary_root(const std::string& prefix) {
    return std::filesystem::temp_directory_path() /
        (prefix + "-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::vector<std::filesystem::path> fixture_files(
    const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.is_regular_file() && entry.path().filename() != ".keep")
            files.push_back(entry.path().lexically_relative(root));
    std::ranges::sort(files);
    return files;
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_thread_png(const std::filesystem::path& path) {
    constexpr std::array<unsigned char, 79> png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00,
        0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72,
        0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44, 0x41,
        0x54, 0x78, 0xda, 0x63, 0x64, 0x60, 0xf8, 0xff, 0x9f, 0x81,
        0x81, 0x81, 0x81, 0x89, 0x01, 0x0a, 0x00, 0x1e, 0x04, 0x02,
        0x01, 0x06, 0xca, 0xf1, 0x64, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
}

void create_studio_platform_fixture(const std::filesystem::path& root) {
    const bool updating = std::filesystem::exists(root);
    fabric::editor::ProjectSession project;
    const bool project_opened = updating
        ? project.open(root) : project.create(root, manifest());
    REQUIRE(project_opened);
    if (!updating) {
        auto thread_source = temporary_root("fabric-platform-thread");
        thread_source += ".png";
        write_thread_png(thread_source);
        REQUIRE(project.import_png(thread_source, {.value = "platform-thread"},
                                   "Platform Thread"));
        std::error_code ignored;
        std::filesystem::remove(thread_source, ignored);
        REQUIRE(project.create_visual_preset({
            .kind = fabric::editor::VisualPresetKind::seam,
            .id = {.value = "platform-strip"},
            .name = "Platform Strip",
            .thread_texture = fabric::project::ResourceReference{
                {.value = "platform-thread"}, "texture"}}));
        fabric::editor::CreateEntityPrompt entity;
        entity.name = "Platform Visual";
        entity.node_name = "Textile strip";
        entity.drawable = fabric::project::EntityDrawableKind::visual_component;
        entity.resource_id = "platform-strip";
        REQUIRE(project.create_entity(entity));
    }
    fabric::editor::MapSession map_session;
    const bool map_opened = updating
        ? map_session.open(root, {.value = "platform-preview"})
        : map_session.create(root, map());
    REQUIRE(map_opened);
    if (std::ranges::none_of(map_session.map()->layers, [](const auto& layer) {
            return layer.id == "instances";
        }))
        REQUIRE(map_session.add_layer({
            "instances", "Instances",
            fabric::project::MapLayerKind::instances,
            true, false, 0.0F}));
    const auto preset = fabric::editor::build_rotating_platform_preset(
        manifest(), *map_session.map(), {
            .id = {.value = "rotating-platform"},
            .name = "Rotating Platform",
            .activation = fabric::editor::RotatingPlatformActivation::sensor,
            .visual_entity = fabric::project::ResourceReference{
                {.value = "platform-visual"}, "entity"},
            .position = {0.0F, 0.0F},
            .size = {6.0F, 0.6F},
            .sensor_center = {0.0F, 1.0F},
            .sensor_size = {7.0F, 2.0F},
            .speed_degrees_per_second = 90.0F,
            .direction = -1,
            .acceleration_degrees_per_second_squared = 180.0F,
            .maximum_torque = 250.0F,
            .limit_enabled = true,
            .minimum_angle_degrees = -35.0F,
            .maximum_angle_degrees = 35.0F});
    REQUIRE(preset.ok());
    fabric::editor::MechanicSession mechanic;
    if (updating) {
        REQUIRE(mechanic.open(root, *map_session.map(),
                              {.value = "rotating-platform"}));
        REQUIRE(mechanic.set_node_property(
            {.value = "platform"}, "position", fabric::core::Vec2{}));
        REQUIRE(mechanic.set_node_property(
            {.value = "anchor"}, "position", fabric::core::Vec2{}));
        REQUIRE(mechanic.set_node_property(
            {.value = "presence"}, "center",
            fabric::core::Vec2{0.0F, 1.0F}));
        REQUIRE(mechanic.set_parameter_default(
            {.value = "sensor-center"},
            fabric::core::Vec2{0.0F, 1.0F}));
        REQUIRE(mechanic.set_node_property(
            {.value = "platform"}, "entity",
            fabric::project::ResourceReference{
                {.value = "platform-visual"}, "entity"}));
    } else {
        REQUIRE(mechanic.create(root, *map_session.map(), *preset.graph));
    }
    REQUIRE(mechanic.save());
    const auto prefab = std::ranges::find(
        map_session.map()->prefabs, "rotating-platform-prefab",
        &fabric::project::PrefabDefinition::id);
    if (prefab == map_session.map()->prefabs.end()) {
        REQUIRE(map_session.add_prefab({
            .id = "rotating-platform-prefab",
            .entity = {{.value = "platform-visual"}, "entity"},
            .mechanic = fabric::project::ResourceReference{
                {.value = "rotating-platform"}, "mechanic"}}));
    }
    REQUIRE(map_session.set_prefab_mechanic_override(
        {.value = "rotating-platform-prefab"}, {"speed", 60.0F}));
    REQUIRE(map_session.set_prefab_mechanic_override(
        {.value = "rotating-platform-prefab"},
        {"sensor-size", fabric::core::Vec2{8.0F, 2.5F}}));
    if (std::ranges::none_of(map_session.map()->instances,
            [](const auto& instance) {
                return instance.id == "rotating-platform-instance";
            }))
        REQUIRE(map_session.place_instance({
            .id = "rotating-platform-instance",
            .prefab = fabric::project::ResourceReference{
                {.value = "rotating-platform-prefab"}, "prefab"},
            .layer_id = "instances",
            .transform = {.position = {0.0F, 2.0F}}},
            {.enabled = false}));
    REQUIRE(map_session.save());
}

float platform_angle(const fabric::physics::MechanicSimulation& simulation) {
    const auto found = std::ranges::find(
        simulation.body_states(), "platform",
        &fabric::physics::MechanicBodyState::node_id);
    REQUIRE(found != simulation.body_states().end());
    return found->rotation_degrees;
}

} // namespace

TEST_CASE("rotating platform preset exposes typed parameters and two activations") {
    auto sensor_request = fabric::editor::RotatingPlatformPresetRequest{
        .limit_enabled = true,
        .minimum_angle_degrees = -30.0F,
        .maximum_angle_degrees = 30.0F};
    const auto sensor = fabric::editor::build_rotating_platform_preset(
        manifest(), map(), sensor_request);
    REQUIRE(sensor.ok());
    CHECK(sensor.graph->nodes.size() == 5U);
    CHECK(sensor.graph->parameters.size() == 10U);
    CHECK(std::ranges::any_of(sensor.graph->nodes, [](const auto& node) {
        return node.type == "sensor";
    }));
    const auto sensor_plan = fabric::physics::compile_mechanic_graph(
        *sensor.graph, map());
    REQUIRE(sensor_plan.ok());
    REQUIRE(sensor_plan.plan->motors.size() == 1U);
    CHECK(sensor_plan.plan->motors.front().direction == 1);
    CHECK(sensor_plan.plan->motors.front().acceleration_degrees_per_second_squared ==
          180.0F);
    const auto overridden_plan = fabric::physics::compile_mechanic_graph(
        *sensor.graph, map(),
        {{"speed", 45.0F}, {"direction", std::int64_t{-1}},
         {"sensor-size", fabric::core::Vec2{9.0F, 4.0F}}});
    REQUIRE(overridden_plan.ok());
    CHECK(overridden_plan.plan->motors.front().speed_degrees_per_second ==
          45.0F);
    CHECK(overridden_plan.plan->motors.front().direction == -1);
    CHECK(overridden_plan.plan->sensors.front().size ==
          fabric::core::Vec2{9.0F, 4.0F});
    CHECK_FALSE(fabric::physics::compile_mechanic_graph(
        *sensor.graph, map(), {{"speed", std::int64_t{45}}}).ok());
    const auto transformed_plan = fabric::physics::compile_mechanic_graph(
        *sensor.graph, map(), {},
        fabric::core::Transform{.position = {3.0F, 4.0F},
                                .rotation_degrees = 90.0F,
                                .scale = {2.0F, 2.0F}});
    REQUIRE(transformed_plan.ok());
    CHECK(transformed_plan.plan->bodies.front().position ==
          fabric::core::Vec2{3.0F, 4.0F});
    CHECK(transformed_plan.plan->bodies.front().size ==
          fabric::core::Vec2{8.0F, 1.0F});
    CHECK(std::abs(transformed_plan.plan->sensors.front().center.x - 1.0F) <
          0.001F);
    CHECK(std::abs(transformed_plan.plan->sensors.front().center.y - 4.0F) <
          0.001F);
    CHECK_FALSE(fabric::physics::compile_mechanic_graph(
        *sensor.graph, map(), {},
        fabric::core::Transform{.scale = {2.0F, 1.0F}}).ok());
    auto unlimited_request = sensor_request;
    unlimited_request.id = {.value = "unlimited-platform"};
    unlimited_request.limit_enabled = false;
    const auto unlimited = fabric::editor::build_rotating_platform_preset(
        manifest(), map(), unlimited_request);
    REQUIRE(unlimited.ok());
    const auto unlimited_plan = fabric::physics::compile_mechanic_graph(
        *unlimited.graph, map());
    REQUIRE(unlimited_plan.ok());
    CHECK_FALSE(unlimited_plan.plan->joints.front().limit_enabled);

    auto event_request = sensor_request;
    event_request.id = {.value = "event-platform"};
    event_request.activation = fabric::editor::RotatingPlatformActivation::event;
    event_request.direction = -1;
    const auto event = fabric::editor::build_rotating_platform_preset(
        manifest(), map(), event_request);
    REQUIRE(event.ok());
    CHECK(event.graph->parameters.size() == 8U);
    const auto event_node = std::ranges::find(
        event.graph->nodes, "event", &fabric::project::MechanicNodeDefinition::type);
    REQUIRE(event_node != event.graph->nodes.end());
    CHECK(std::get<std::string>(std::ranges::find(
        event_node->properties, "mode",
        &fabric::project::MechanicNodeProperty::id)->value) == "listen");

    auto invalid = event_request;
    invalid.event_id = {.value = "missing-event"};
    CHECK_FALSE(fabric::editor::build_rotating_platform_preset(
        manifest(), map(), invalid).ok());
    invalid = sensor_request;
    invalid.direction = 0;
    CHECK_FALSE(fabric::editor::build_rotating_platform_preset(
        manifest(), map(), invalid).ok());
}

TEST_CASE("rotating platform responds to sensor and event signals with limits") {
    auto request = fabric::editor::RotatingPlatformPresetRequest{
        .limit_enabled = true,
        .minimum_angle_degrees = -25.0F,
        .maximum_angle_degrees = 25.0F};
    auto built = fabric::editor::build_rotating_platform_preset(
        manifest(), map(), request);
    REQUIRE(built.ok());
    auto compiled = fabric::physics::compile_mechanic_graph(*built.graph, map());
    REQUIRE(compiled.ok());
    fabric::physics::MechanicSimulation simulation;
    REQUIRE(simulation.load(*compiled.plan));
    for (int step = 0; step < 30; ++step) REQUIRE(simulation.step_once());
    CHECK(std::abs(platform_angle(simulation)) < 0.1F);
    REQUIRE(simulation.set_sensor_active({.value = "presence"}, true));
    REQUIRE(simulation.step_once());
    CHECK(std::abs(platform_angle(simulation)) < 1.0F);
    for (int step = 0; step < 120; ++step) REQUIRE(simulation.step_once());
    CHECK(std::abs(platform_angle(simulation)) > 10.0F);
    CHECK(std::abs(platform_angle(simulation)) <= 26.0F);
    const auto sensor_direction_angle = platform_angle(simulation);

    request.id = {.value = "event-platform"};
    request.activation = fabric::editor::RotatingPlatformActivation::event;
    request.direction = -1;
    built = fabric::editor::build_rotating_platform_preset(
        manifest(), map(), request);
    REQUIRE(built.ok());
    compiled = fabric::physics::compile_mechanic_graph(*built.graph, map());
    REQUIRE(compiled.ok());
    REQUIRE(simulation.load(*compiled.plan));
    CHECK_FALSE(simulation.set_event_active({.value = "missing"}, true));
    REQUIRE(simulation.set_event_active({.value = "platform-activate"}, true));
    for (int step = 0; step < 60; ++step) REQUIRE(simulation.step_once());
    CHECK(std::abs(platform_angle(simulation)) > 5.0F);
    CHECK(sensor_direction_angle * platform_angle(simulation) < 0.0F);
}

TEST_CASE("physical presence activates and carries the preview character") {
    auto request = fabric::editor::RotatingPlatformPresetRequest{
        .limit_enabled = true,
        .minimum_angle_degrees = -30.0F,
        .maximum_angle_degrees = 30.0F};
    const auto built = fabric::editor::build_rotating_platform_preset(
        manifest(), map(), request);
    REQUIRE(built.ok());
    const auto compiled = fabric::physics::compile_mechanic_graph(
        *built.graph, map());
    REQUIRE(compiled.ok());
    fabric::physics::MechanicSimulation simulation;
    REQUIRE(simulation.load(*compiled.plan));
    REQUIRE(simulation.place_preview_character({
        .position = {1.0F, 0.76F},
        .size = {0.6F, 1.0F},
        .density = 1.0F,
        .friction = 1.2F}));

    REQUIRE(simulation.step_once());
    REQUIRE(simulation.signal_states().front().active);
    CHECK(simulation.signal_states().front().physical_overlap_count == 1U);
    REQUIRE(simulation.sensor_states().front().active);
    CHECK(simulation.sensor_states().front().size == request.sensor_size);
    REQUIRE(simulation.activation_states().front().active);
    REQUIRE(simulation.debug_events().size() == 1U);
    CHECK(simulation.debug_events().front().transition ==
          fabric::physics::MechanicActivationTransition::begin);
    for (int step = 0; step < 40; ++step) REQUIRE(simulation.step_once());
    const auto transported = simulation.preview_character_state();
    REQUIRE(transported.has_value());
    CHECK(transported->position.x > 1.15F);
    CHECK(std::abs(platform_angle(simulation)) > 5.0F);

    REQUIRE(simulation.place_preview_character({.position = {10.0F, 4.0F}}));
    CHECK_FALSE(simulation.signal_states().front().active);
    CHECK_FALSE(simulation.activation_states().front().active);
    REQUIRE(simulation.debug_events().size() == 2U);
    CHECK(simulation.debug_events().back().transition ==
          fabric::physics::MechanicActivationTransition::end);
}

TEST_CASE("Studio regenerates the rotating platform fixture byte for byte") {
    const auto fixture = std::filesystem::path{FABRIC_SOURCE_DIR} /
        "tests/fixtures/studio-rotating-platform";
    if (std::getenv("FABRIC_UPDATE_STUDIO_PLATFORM_FIXTURE") != nullptr) {
        create_studio_platform_fixture(fixture);
    }
    REQUIRE(std::filesystem::is_directory(fixture));
    const auto regenerated = temporary_root("fabric-regenerated-platform");
    create_studio_platform_fixture(regenerated);
    REQUIRE(fixture_files(regenerated) == fixture_files(fixture));
    for (const auto& relative : fixture_files(fixture))
        CHECK(read_binary(regenerated / relative) == read_binary(fixture / relative));
    REQUIRE(fabric::project::validate_project(fixture).ok());

    const auto loaded_manifest = fabric::project::load_manifest(fixture);
    REQUIRE(loaded_manifest.ok());
    const auto loaded_map = fabric::project::load_map(
        fixture, *loaded_manifest.manifest,
        "maps/platform-preview.map.json");
    const auto loaded_graph = fabric::project::load_mechanic_graph(
        fixture, *loaded_manifest.manifest,
        "assets/mechanics/rotating-platform.mechanic.json");
    REQUIRE(loaded_map.ok());
    REQUIRE(loaded_graph.ok());
    REQUIRE(loaded_map.asset->prefabs.size() == 1U);
    REQUIRE(loaded_map.asset->instances.size() == 1U);
    const auto compiled = fabric::physics::compile_mechanic_graph(
        *loaded_graph.asset, *loaded_map.asset,
        loaded_map.asset->prefabs.front().mechanic_overrides,
        loaded_map.asset->instances.front().transform);
    REQUIRE(compiled.ok());
    REQUIRE(loaded_map.asset->prefabs.front().mechanic.has_value());
    CHECK(loaded_map.asset->prefabs.front().mechanic->id.value ==
          "rotating-platform");
    CHECK(compiled.plan->motors.front().speed_degrees_per_second == 60.0F);
    CHECK(compiled.plan->sensors.front().size ==
          fabric::core::Vec2{8.0F, 2.5F});
    CHECK(compiled.plan->bodies.front().position ==
          fabric::core::Vec2{0.0F, 2.0F});
    CHECK(compiled.plan->sensors.front().center ==
          fabric::core::Vec2{0.0F, 3.0F});
    REQUIRE(compiled.plan->bodies.front().visual_entity.has_value());
    CHECK(compiled.plan->bodies.front().visual_entity->id.value ==
          "platform-visual");
    fabric::physics::MechanicSimulation fixture_simulation;
    REQUIRE(fixture_simulation.load(*compiled.plan));
    REQUIRE(fixture_simulation.place_preview_character({
        .position = {-1.0F, 2.81F},
        .size = {0.6F, 1.0F},
        .density = 1.0F,
        .friction = 1.2F}));
    for (int step = 0; step < 41; ++step)
        REQUIRE(fixture_simulation.step_once());
    const auto fixture_character = fixture_simulation.preview_character_state();
    REQUIRE(fixture_character.has_value());
    CHECK(std::abs(fixture_character->position.x + 1.0F) > 0.15F);
    CHECK(fixture_simulation.activation_states().front().active);
    REQUIRE_FALSE(fixture_simulation.debug_events().empty());
    CHECK(fixture_simulation.debug_events().front().transition ==
          fabric::physics::MechanicActivationTransition::begin);
    fabric::editor::MechanicSession prefab_preview;
    REQUIRE(prefab_preview.open_prefab_instance(
        fixture, *loaded_map.asset,
        {.value = "rotating-platform-instance"}));
    REQUIRE(prefab_preview.place_preview_character({
        .position = {-1.0F, 2.81F}, .size = {0.6F, 1.0F}}));
    REQUIRE(prefab_preview.step_once());
    CHECK(prefab_preview.simulation().activation_states().front().active);
    REQUIRE(prefab_preview.preview_instance_id().has_value());
    CHECK(prefab_preview.preview_instance_id()->value ==
          "rotating-platform-instance");
    auto synchronized_map = *loaded_map.asset;
    const auto synchronized_instance = std::ranges::find(
        synchronized_map.instances, std::string{"rotating-platform-instance"},
        &fabric::project::MapInstance::id);
    REQUIRE(synchronized_instance != synchronized_map.instances.end());
    synchronized_instance->transform.position.x = 3.0F;
    const auto synchronized_prefab = std::ranges::find(
        synchronized_map.prefabs, std::string{"rotating-platform-prefab"},
        &fabric::project::PrefabDefinition::id);
    REQUIRE(synchronized_prefab != synchronized_map.prefabs.end());
    synchronized_prefab->mechanic_overrides.push_back(
        {"size", fabric::core::Vec2{9.0F, 3.0F}});
    REQUIRE(prefab_preview.sync_preview_instance(synchronized_map));
    REQUIRE(prefab_preview.simulation().body_states().size() == 1U);
    CHECK(prefab_preview.simulation().body_states().front().position ==
          fabric::core::Vec2{3.0F, 2.0F});
    CHECK(prefab_preview.simulation().body_states().front().size ==
          fabric::core::Vec2{9.0F, 3.0F});
    REQUIRE(prefab_preview.open_prefab(
        fixture, *loaded_map.asset,
        {.value = "rotating-platform-prefab"}));
    CHECK_FALSE(prefab_preview.preview_instance_id().has_value());
    fabric::editor::MapSession prefab_editor;
    REQUIRE(prefab_editor.open(
        regenerated, {.value = "platform-preview"}));
    CHECK_FALSE(prefab_editor.set_prefab_mechanic_override(
        {.value = "rotating-platform-prefab"},
        {"speed", std::int64_t{60}}));
    CHECK_FALSE(prefab_editor.set_prefab_mechanic_override(
        {.value = "rotating-platform-prefab"}, {"unknown", 1.0F}));
    REQUIRE(prefab_editor.set_prefab_mechanic_override(
        {.value = "rotating-platform-prefab"}, {"speed", 75.0F}));
    REQUIRE(prefab_editor.undo());
    CHECK(std::get<float>(prefab_editor.map()->prefabs.front()
                              .mechanic_overrides.front().value) == 60.0F);
    const auto visual = fabric::project::load_entity(
        fixture, *loaded_manifest.manifest,
        "entities/platform-visual.entity.json");
    REQUIRE(visual.ok());
    const auto component = fabric::project::load_visual_component(
        fixture, *loaded_manifest.manifest,
        "assets/components/platform-strip.component.json");
    REQUIRE(component.ok());
    const auto rendered = fabric::render::resolve_visual_component(
        fixture, *loaded_manifest.manifest, *component.asset);
    REQUIRE(rendered.ok());
    REQUIRE(rendered.packets.size() == 2U);
    CHECK(rendered.packets.front().image_fill.has_value());
    REQUIRE(rendered.packets.back().stroke.has_value());
    const auto map_preview = fabric::render::resolve_map_preview(
        fixture, *loaded_manifest.manifest, *loaded_map.asset);
    REQUIRE(map_preview.ok());
    REQUIRE(map_preview.packets.size() == 2U);
    CHECK(std::ranges::any_of(map_preview.packets, [](const auto& packet) {
        return packet.image_fill.has_value();
    }));
    CHECK(std::ranges::any_of(map_preview.packets, [](const auto& packet) {
        return packet.stroke.has_value();
    }));
    auto invalid_map = *loaded_map.asset;
    invalid_map.prefabs.front().mechanic_overrides.push_back(
        {"unknown", 1.0F});
    REQUIRE(fabric::project::publish_map(
        regenerated, *loaded_manifest.manifest, invalid_map).ok());
    CHECK_FALSE(fabric::project::validate_project(regenerated).ok());
    std::error_code ignored;
    std::filesystem::remove_all(regenerated, ignored);
}
