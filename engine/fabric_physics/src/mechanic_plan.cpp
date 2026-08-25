#include "fabric/physics/mechanic_plan.hpp"

#include <algorithm>
#include <ranges>
#include <string_view>

namespace fabric::physics {
namespace {

void error(MechanicPlanResult& result, const project::ErrorCode code,
           std::string field, std::string message) {
    result.errors.push_back({code, std::move(field), std::move(message)});
}

const project::MechanicNodeProperty* property(
    const project::MechanicNodeDefinition& node, const std::string_view id) {
    const auto found = std::ranges::find(node.properties, id,
                                         &project::MechanicNodeProperty::id);
    return found == node.properties.end() ? nullptr : &*found;
}

template <typename Value>
Value value(const project::MechanicNodeDefinition& node,
            const std::string_view id, Value fallback = {}) {
    const auto* found = property(node, id);
    if (found == nullptr) return fallback;
    const auto* typed = std::get_if<Value>(&found->value);
    return typed == nullptr ? fallback : *typed;
}

std::optional<std::string> incoming_source(
    const project::MechanicGraph& graph, const std::string_view node,
    const std::string_view port) {
    const auto found = std::ranges::find_if(
        graph.connections, [&](const auto& connection) {
            return connection.to_node == node && connection.to_port == port;
        });
    return found == graph.connections.end()
        ? std::nullopt
        : std::optional<std::string>{found->from_node};
}

bool require_source(MechanicPlanResult& result,
                    const project::MechanicGraph& graph,
                    const project::MechanicNodeDefinition& node,
                    const std::string_view port, std::string& output) {
    const auto source = incoming_source(graph, node.id, port);
    if (!source) {
        error(result, project::ErrorCode::missing_resource,
              "nodes." + node.id + ".ports." + std::string{port},
              "required mechanic input is not connected");
        return false;
    }
    output = *source;
    return true;
}

MechanicBodyType body_type(const std::string_view type) {
    if (type == "kinematic") return MechanicBodyType::kinematic_body;
    if (type == "dynamic") return MechanicBodyType::dynamic_body;
    return MechanicBodyType::static_body;
}

bool event_declared(const project::MapDocument& map,
                    const core::ResourceId& id) {
    return std::ranges::any_of(map.events, [&](const auto& event) {
        return event.id == id;
    });
}

} // namespace

MechanicPlanResult compile_mechanic_graph(
    const project::MechanicGraph& graph, const project::MapDocument& map) {
    MechanicPlanResult result;
    const auto validation = project::validate_mechanic_graph({}, graph);
    result.errors = validation.errors;
    if (!result.errors.empty()) return result;

    auto resolved = graph;
    for (const auto& parameter : resolved.parameters) {
        const auto node = std::ranges::find(
            resolved.nodes, parameter.target_node,
            &project::MechanicNodeDefinition::id);
        if (node == resolved.nodes.end()) continue;
        const auto target = std::ranges::find(
            node->properties, parameter.target_property,
            &project::MechanicNodeProperty::id);
        if (target != node->properties.end())
            target->value = parameter.default_value;
    }

    MechanicPlan plan;
    for (const auto& node : resolved.nodes) {
        const auto kind = project::mechanic_node_kind(node.type);
        if (!kind) continue;
        switch (*kind) {
        case project::MechanicNodeKind::body:
            plan.bodies.push_back({
                .node_id = node.id,
                .type = body_type(value<std::string>(node, "body-type")),
                .position = value<core::Vec2>(node, "position"),
                .size = value<core::Vec2>(node, "size"),
                .rotation_degrees = value<float>(node, "rotation"),
                .density = value<float>(node, "density"),
                .friction = value<float>(node, "friction"),
                .visual_entity = value<project::ResourceReference>(
                    node, "entity")});
            break;
        case project::MechanicNodeKind::pivot: {
            MechanicPivotDescription description{
                .node_id = node.id,
                .position = value<core::Vec2>(node, "position")};
            if (require_source(result, resolved, node, "body",
                               description.body_node_id))
                plan.pivots.push_back(std::move(description));
            break;
        }
        case project::MechanicNodeKind::joint: {
            MechanicJointDescription description{
                .node_id = node.id,
                .limit_enabled = value<bool>(node, "limit-enabled"),
                .minimum_angle_degrees = value<float>(node, "min-angle"),
                .maximum_angle_degrees = value<float>(node, "max-angle")};
            description.body_a_node_id = incoming_source(
                resolved, node.id, "body-a");
            description.body_b_node_id = incoming_source(
                resolved, node.id, "body-b");
            description.pivot_node_id = incoming_source(
                resolved, node.id, "pivot");
            if (!description.body_a_node_id)
                error(result, project::ErrorCode::missing_resource,
                      "nodes." + node.id + ".ports.body-a",
                      "joint requires body-a");
            if (!description.pivot_node_id)
                error(result, project::ErrorCode::missing_resource,
                      "nodes." + node.id + ".ports.pivot",
                      "joint requires a pivot");
            if (description.body_a_node_id && description.pivot_node_id)
                plan.joints.push_back(std::move(description));
            break;
        }
        case project::MechanicNodeKind::motor: {
            MechanicMotorDescription description{
                .node_id = node.id,
                .enabled_source_node_id = incoming_source(
                    resolved, node.id, "enabled"),
                .speed_degrees_per_second = value<float>(node, "speed"),
                .direction = value<std::int64_t>(node, "direction", 1),
                .acceleration_degrees_per_second_squared =
                    value<float>(node, "acceleration"),
                .maximum_torque = value<float>(node, "max-torque")};
            if (require_source(result, resolved, node, "joint",
                               description.joint_node_id))
                plan.motors.push_back(std::move(description));
            break;
        }
        case project::MechanicNodeKind::sensor:
            plan.sensors.push_back({
                .node_id = node.id,
                .body_node_id = incoming_source(resolved, node.id, "body"),
                .center = value<core::Vec2>(node, "center"),
                .size = value<core::Vec2>(node, "size")});
            break;
        case project::MechanicNodeKind::constraint: {
            MechanicConstraintDescription description{
                .node_id = node.id,
                .pivot_node_id = incoming_source(resolved, node.id, "pivot"),
                .minimum_angle_degrees = value<float>(node, "min-angle"),
                .maximum_angle_degrees = value<float>(node, "max-angle")};
            if (require_source(result, resolved, node, "body",
                               description.body_node_id))
                plan.constraints.push_back(std::move(description));
            break;
        }
        case project::MechanicNodeKind::event: {
            const auto mode = value<std::string>(node, "mode", "emit");
            MechanicEventDescription description{
                .node_id = node.id,
                .event_id = {.value = value<std::string>(node, "event-id")},
                .mode = mode == "listen" ? MechanicEventMode::listen
                                         : MechanicEventMode::emit,
                .trigger_source_node_id = incoming_source(
                    resolved, node.id, "trigger")};
            if (description.mode == MechanicEventMode::emit &&
                !description.trigger_source_node_id) {
                error(result, project::ErrorCode::missing_resource,
                      "nodes." + node.id + ".ports.trigger",
                      "emit event requires a trigger input");
                break;
            }
            if (!event_declared(map, description.event_id)) {
                error(result, project::ErrorCode::missing_resource,
                      "nodes." + node.id + ".properties.event-id",
                      "event is not declared by the map");
                break;
            }
            plan.events.push_back(std::move(description));
            break;
        }
        }
    }
    if (result.errors.empty()) result.plan = std::move(plan);
    return result;
}

} // namespace fabric::physics
