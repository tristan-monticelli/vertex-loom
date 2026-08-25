#include "fabric/editor/mechanic_presets.hpp"

#include "fabric/physics/mechanic_plan.hpp"

#include <cmath>
#include <ranges>
#include <string_view>
#include <utility>

namespace fabric::editor {
namespace {

void error(MechanicPresetResult& result, const project::ErrorCode code,
           std::string field, std::string message) {
    result.errors.push_back({code, std::move(field), std::move(message)});
}

project::MechanicValue default_value(const project::MechanicValueType type,
                                     const std::string_view id) {
    using Type = project::MechanicValueType;
    switch (type) {
    case Type::boolean: return false;
    case Type::integer: return std::int64_t{};
    case Type::scalar:
        if (id == "density" || id == "friction" || id == "max-torque")
            return 1.0F;
        return 0.0F;
    case Type::text:
        if (id == "body-type") return std::string{"dynamic"};
        if (id == "event-id") return std::string{"event"};
        return std::string{"value"};
    case Type::vec2: return core::Vec2{1.0F, 1.0F};
    case Type::resource:
        return project::ResourceReference{{.value = "resource"}, "entity"};
    case Type::body_handle:
    case Type::pivot_handle:
    case Type::joint_handle: break;
    }
    return false;
}

project::MechanicNodeDefinition node(const project::MechanicNodeKind kind,
                                     std::string id) {
    const auto& schema = project::mechanic_node_schema(kind);
    project::MechanicNodeDefinition result{
        .id = std::move(id), .type = std::string{schema.type}};
    for (const auto& port : schema.ports)
        result.ports.push_back({
            .id = std::string{port.id}, .name = std::string{port.id},
            .direction = port.direction, .type = port.type});
    for (const auto& property : schema.properties)
        if (property.required)
            result.properties.push_back({
                .id = std::string{property.id},
                .value = default_value(property.type, property.id)});
    return result;
}

void set(project::MechanicNodeDefinition& node, std::string id,
         project::MechanicValue value) {
    const auto found = std::ranges::find(
        node.properties, id, &project::MechanicNodeProperty::id);
    if (found == node.properties.end())
        node.properties.push_back({std::move(id), std::move(value)});
    else
        found->value = std::move(value);
}

project::MechanicParameterDefinition parameter(
    std::string id, std::string name, const project::MechanicValueType type,
    project::MechanicValue value, std::string node_id, std::string property) {
    return {.id = std::move(id), .name = std::move(name), .type = type,
            .default_value = std::move(value), .target_node = std::move(node_id),
            .target_property = std::move(property)};
}

bool event_declared(const project::MapDocument& map,
                    const core::ResourceId& event_id) {
    return std::ranges::any_of(map.events, [&](const auto& event) {
        return event.id == event_id;
    });
}

} // namespace

MechanicPresetResult build_rotating_platform_preset(
    const project::ProjectManifest& manifest, const project::MapDocument& map,
    const RotatingPlatformPresetRequest& request) {
    MechanicPresetResult result;
    if (!core::ResourceId::is_valid(request.id.value))
        error(result, project::ErrorCode::invalid_resource_id, "id",
              "platform preset id must be valid");
    if (request.name.empty())
        error(result, project::ErrorCode::invalid_asset, "name",
              "platform preset name must not be empty");
    const auto finite = [](const float value) { return std::isfinite(value); };
    if (!finite(request.position.x) || !finite(request.position.y) ||
        !finite(request.size.x) || !finite(request.size.y) ||
        request.size.x <= 0.0F || request.size.y <= 0.0F)
        error(result, project::ErrorCode::invalid_asset, "body",
              "platform position and positive size must be finite");
    if (!finite(request.speed_degrees_per_second) ||
        request.speed_degrees_per_second < 0.0F ||
        !finite(request.acceleration_degrees_per_second_squared) ||
        request.acceleration_degrees_per_second_squared < 0.0F ||
        !finite(request.maximum_torque) || request.maximum_torque < 0.0F)
        error(result, project::ErrorCode::invalid_asset, "motor",
              "speed, acceleration and torque must be finite and non-negative");
    if (request.direction != -1 && request.direction != 1)
        error(result, project::ErrorCode::invalid_asset, "direction",
              "direction must be -1 or 1");
    if (!finite(request.minimum_angle_degrees) ||
        !finite(request.maximum_angle_degrees) ||
        request.minimum_angle_degrees > request.maximum_angle_degrees)
        error(result, project::ErrorCode::invalid_asset, "limits",
              "platform angle limits are invalid");
    if (request.activation == RotatingPlatformActivation::sensor &&
        (!finite(request.sensor_center.x) || !finite(request.sensor_center.y) ||
         !finite(request.sensor_size.x) || !finite(request.sensor_size.y) ||
         request.sensor_size.x <= 0.0F || request.sensor_size.y <= 0.0F))
        error(result, project::ErrorCode::invalid_asset, "sensor",
              "sensor center and positive size must be finite");
    if (request.activation == RotatingPlatformActivation::event &&
        (!core::ResourceId::is_valid(request.event_id.value) ||
         !event_declared(map, request.event_id)))
        error(result, project::ErrorCode::missing_resource, "eventId",
              "activation event must be declared by the map");
    if (request.visual_entity &&
        (request.visual_entity->expected_type != "entity" ||
         !core::ResourceId::is_valid(request.visual_entity->id.value)))
        error(result, project::ErrorCode::resource_type_mismatch,
              "visualEntity", "platform visual must reference an entity");
    if (!result.errors.empty()) return result;

    using Kind = project::MechanicNodeKind;
    using Type = project::MechanicValueType;
    project::MechanicGraph graph{
        .document = {.schema_version = project::current_mechanic_graph_schema_version,
                     .type = "mechanic", .id = request.id,
                     .name = request.name}};
    auto body = node(Kind::body, "platform");
    set(body, "body-type", std::string{"dynamic"});
    set(body, "position", request.position);
    set(body, "size", request.size);
    set(body, "rotation", 0.0F);
    set(body, "density", 1.0F);
    set(body, "friction", 0.9F);
    if (request.visual_entity) set(body, "entity", *request.visual_entity);
    auto pivot = node(Kind::pivot, "anchor");
    set(pivot, "position", request.position);
    auto joint = node(Kind::joint, "hinge");
    set(joint, "limit-enabled", request.limit_enabled);
    set(joint, "min-angle", request.minimum_angle_degrees);
    set(joint, "max-angle", request.maximum_angle_degrees);
    auto motor = node(Kind::motor, "drive");
    set(motor, "speed", request.speed_degrees_per_second);
    set(motor, "direction", request.direction);
    set(motor, "acceleration",
        request.acceleration_degrees_per_second_squared);
    set(motor, "max-torque", request.maximum_torque);
    graph.nodes = {std::move(body), std::move(pivot), std::move(joint),
                   std::move(motor)};
    graph.connections = {
        {"platform", "body", "anchor", "body"},
        {"platform", "body", "hinge", "body-a"},
        {"anchor", "pivot", "hinge", "pivot"},
        {"hinge", "joint", "drive", "joint"}};

    if (request.activation == RotatingPlatformActivation::sensor) {
        auto sensor = node(Kind::sensor, "presence");
        set(sensor, "center", request.sensor_center);
        set(sensor, "size", request.sensor_size);
        graph.nodes.push_back(std::move(sensor));
        graph.connections.push_back(
            {"platform", "body", "presence", "body"});
        graph.connections.push_back(
            {"presence", "active", "drive", "enabled"});
    } else {
        auto event = node(Kind::event, "activation-event");
        set(event, "event-id", request.event_id.value);
        set(event, "mode", std::string{"listen"});
        graph.nodes.push_back(std::move(event));
        graph.connections.push_back(
            {"activation-event", "active", "drive", "enabled"});
    }

    graph.parameters = {
        parameter("size", "Size", Type::vec2, request.size,
                  "platform", "size"),
        parameter("speed", "Speed", Type::scalar,
                  request.speed_degrees_per_second, "drive", "speed"),
        parameter("direction", "Direction", Type::integer,
                  request.direction, "drive", "direction"),
        parameter("acceleration", "Acceleration", Type::scalar,
                  request.acceleration_degrees_per_second_squared,
                  "drive", "acceleration"),
        parameter("max-torque", "Maximum torque", Type::scalar,
                  request.maximum_torque, "drive", "max-torque"),
        parameter("limit-enabled", "Limit enabled", Type::boolean,
                  request.limit_enabled, "hinge", "limit-enabled"),
        parameter("min-angle", "Minimum angle", Type::scalar,
                  request.minimum_angle_degrees, "hinge", "min-angle"),
        parameter("max-angle", "Maximum angle", Type::scalar,
                  request.maximum_angle_degrees, "hinge", "max-angle")};
    if (request.activation == RotatingPlatformActivation::sensor) {
        graph.parameters.push_back(parameter(
            "sensor-center", "Sensor center", Type::vec2,
            request.sensor_center, "presence", "center"));
        graph.parameters.push_back(parameter(
            "sensor-size", "Sensor size", Type::vec2,
            request.sensor_size, "presence", "size"));
    }

    const auto validation = project::validate_mechanic_graph(manifest, graph);
    result.errors = validation.errors;
    if (result.errors.empty()) {
        auto compiled = physics::compile_mechanic_graph(graph, map);
        result.errors = std::move(compiled.errors);
    }
    if (result.errors.empty()) result.graph = std::move(graph);
    return result;
}

MechanicPresetResult publish_rotating_platform_preset(
    const std::filesystem::path& root,
    const project::ProjectManifest& manifest, const project::MapDocument& map,
    const RotatingPlatformPresetRequest& request) {
    auto result = build_rotating_platform_preset(manifest, map, request);
    if (!result.ok()) return result;
    const auto path = project::mechanic_graph_document_path(
        manifest, result.graph->document.id);
    std::error_code filesystem_error;
    if (std::filesystem::exists(root / path, filesystem_error) || filesystem_error) {
        result.graph.reset();
        error(result, project::ErrorCode::asset_already_exists, "destination",
              "mechanic preset destination already exists");
        return result;
    }
    const auto published = project::publish_mechanic_graph(
        root, manifest, *result.graph);
    if (!published.ok()) {
        result.graph.reset();
        result.errors = published.errors;
    }
    return result;
}

} // namespace fabric::editor
