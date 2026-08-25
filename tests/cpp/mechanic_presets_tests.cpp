#include "fabric/editor/map_session.hpp"
#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/mechanic_presets.hpp"
#include "fabric/editor/mechanic_session.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/visual_presets.hpp"
#include "fabric/physics/mechanic_plan.hpp"
#include "fabric/physics/mechanic_simulation.hpp"
#include "fabric/project/entity.hpp"
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
        if (entry.is_regular_file()) files.push_back(entry.path().lexically_relative(root));
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
    fabric::editor::MapSession map_session;
    const bool map_opened = updating
        ? map_session.open(root, {.value = "platform-preview"})
        : map_session.create(root, map());
    REQUIRE(map_opened);
    const auto preset = fabric::editor::build_rotating_platform_preset(
        manifest(), *map_session.map(), {
            .id = {.value = "rotating-platform"},
            .name = "Rotating Platform",
            .activation = fabric::editor::RotatingPlatformActivation::sensor,
            .visual_entity = fabric::project::ResourceReference{
                {.value = "platform-visual"}, "entity"},
            .position = {0.0F, 2.0F},
            .size = {6.0F, 0.6F},
            .sensor_center = {0.0F, 3.0F},
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
            {.value = "platform"}, "entity",
            fabric::project::ResourceReference{
                {.value = "platform-visual"}, "entity"}));
    } else {
        REQUIRE(mechanic.create(root, *map_session.map(), *preset.graph));
    }
    REQUIRE(mechanic.save());
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
    const auto compiled = fabric::physics::compile_mechanic_graph(
        *loaded_graph.asset, *loaded_map.asset);
    REQUIRE(compiled.ok());
    REQUIRE(compiled.plan->bodies.front().visual_entity.has_value());
    CHECK(compiled.plan->bodies.front().visual_entity->id.value ==
          "platform-visual");
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
    REQUIRE(rendered.packets.size() == 1U);
    CHECK(rendered.packets.front().image_fill.has_value());
    std::error_code ignored;
    std::filesystem::remove_all(regenerated, ignored);
}
