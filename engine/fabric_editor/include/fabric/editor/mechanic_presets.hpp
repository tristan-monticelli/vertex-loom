#pragma once

#include "fabric/project/mechanic_graph.hpp"
#include "fabric/project/map.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::editor {

enum class RotatingPlatformActivation { sensor, event };

struct RotatingPlatformPresetRequest {
    core::ResourceId id{.value = "rotating-platform"};
    std::string name{"Rotating Platform"};
    RotatingPlatformActivation activation{RotatingPlatformActivation::sensor};
    core::ResourceId event_id{.value = "platform-activate"};
    std::optional<project::ResourceReference> visual_entity;
    core::Vec2 position{};
    core::Vec2 size{4.0F, 0.5F};
    core::Vec2 sensor_center{0.0F, 1.0F};
    core::Vec2 sensor_size{5.0F, 2.0F};
    float speed_degrees_per_second{90.0F};
    std::int64_t direction{1};
    float acceleration_degrees_per_second_squared{180.0F};
    float maximum_torque{100.0F};
    bool limit_enabled{};
    float minimum_angle_degrees{-45.0F};
    float maximum_angle_degrees{45.0F};
};

struct MechanicPresetResult {
    std::optional<project::MechanicGraph> graph;
    std::vector<project::Error> errors;
    [[nodiscard]] bool ok() const noexcept {
        return graph.has_value() && errors.empty();
    }
};

[[nodiscard]] MechanicPresetResult build_rotating_platform_preset(
    const project::ProjectManifest&, const project::MapDocument&,
    const RotatingPlatformPresetRequest&);
[[nodiscard]] MechanicPresetResult publish_rotating_platform_preset(
    const std::filesystem::path&, const project::ProjectManifest&,
    const project::MapDocument&, const RotatingPlatformPresetRequest&);

} // namespace fabric::editor
