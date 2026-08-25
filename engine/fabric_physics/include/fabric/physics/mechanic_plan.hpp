#pragma once

#include "fabric/project/map.hpp"
#include "fabric/project/mechanic_graph.hpp"

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace fabric::physics {

enum class MechanicBodyType { static_body, kinematic_body, dynamic_body };

struct MechanicBodyDescription {
    std::string node_id;
    MechanicBodyType type{MechanicBodyType::static_body};
    core::Vec2 position;
    core::Vec2 size{1.0F, 1.0F};
    float rotation_degrees{};
    float density{};
    float friction{};
    std::optional<project::ResourceReference> visual_entity;
    friend bool operator==(const MechanicBodyDescription&,
                           const MechanicBodyDescription&) = default;
};

struct MechanicPivotDescription {
    std::string node_id;
    std::string body_node_id;
    core::Vec2 position;
    friend bool operator==(const MechanicPivotDescription&,
                           const MechanicPivotDescription&) = default;
};

struct MechanicJointDescription {
    std::string node_id;
    std::optional<std::string> body_a_node_id;
    std::optional<std::string> body_b_node_id;
    std::optional<std::string> pivot_node_id;
    bool limit_enabled{};
    float minimum_angle_degrees{};
    float maximum_angle_degrees{};
    friend bool operator==(const MechanicJointDescription&,
                           const MechanicJointDescription&) = default;
};

struct MechanicMotorDescription {
    std::string node_id;
    std::string joint_node_id;
    std::optional<std::string> enabled_source_node_id;
    float speed_degrees_per_second{};
    std::int64_t direction{1};
    float acceleration_degrees_per_second_squared{};
    float maximum_torque{};
    friend bool operator==(const MechanicMotorDescription&,
                           const MechanicMotorDescription&) = default;
};

enum class MechanicEventMode { emit, listen };

struct MechanicSensorDescription {
    std::string node_id;
    std::optional<std::string> body_node_id;
    core::Vec2 center;
    core::Vec2 size{1.0F, 1.0F};
    friend bool operator==(const MechanicSensorDescription&,
                           const MechanicSensorDescription&) = default;
};

struct MechanicConstraintDescription {
    std::string node_id;
    std::string body_node_id;
    std::optional<std::string> pivot_node_id;
    float minimum_angle_degrees{};
    float maximum_angle_degrees{};
    friend bool operator==(const MechanicConstraintDescription&,
                           const MechanicConstraintDescription&) = default;
};

struct MechanicEventDescription {
    std::string node_id;
    core::ResourceId event_id;
    MechanicEventMode mode{MechanicEventMode::emit};
    std::optional<std::string> trigger_source_node_id;
    friend bool operator==(const MechanicEventDescription&,
                           const MechanicEventDescription&) = default;
};

struct MechanicPlan {
    std::vector<MechanicBodyDescription> bodies;
    std::vector<MechanicPivotDescription> pivots;
    std::vector<MechanicJointDescription> joints;
    std::vector<MechanicMotorDescription> motors;
    std::vector<MechanicSensorDescription> sensors;
    std::vector<MechanicConstraintDescription> constraints;
    std::vector<MechanicEventDescription> events;
    friend bool operator==(const MechanicPlan&, const MechanicPlan&) = default;
};

struct MechanicPlanResult {
    std::optional<MechanicPlan> plan;
    std::vector<project::Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return plan.has_value() && errors.empty();
    }
};

[[nodiscard]] MechanicPlanResult compile_mechanic_graph(
    const project::MechanicGraph&, const project::MapDocument&);

} // namespace fabric::physics
