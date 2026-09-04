#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/textured_path.hpp"

#include <cstddef>

namespace fabric::project {

struct PathFollowerState {
    float progress{};
    float speed{};
    bool loop{true};
    bool orient_to_path{};
    float rotation_offset_degrees{};
    friend bool operator==(const PathFollowerState&, const PathFollowerState&) = default;
};

struct PathFollowerSample {
    core::Vec2 position{};
    core::Vec2 tangent{1.0F, 0.0F};
    float progress{};
    friend bool operator==(const PathFollowerSample&, const PathFollowerSample&) = default;
};

[[nodiscard]] PathFollowerSample sample_textured_path(
    const TexturedPath&, float normalized_progress,
    std::size_t curve_subdivisions = 32U);

[[nodiscard]] float advance_path_follower(
    const TexturedPath&, float normalized_progress, float speed,
    float delta_seconds, bool loop);

} // namespace fabric::project
