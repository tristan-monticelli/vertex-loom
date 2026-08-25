#include "fabric/runtime/trigger_runtime.hpp"

#include <algorithm>
#include <cmath>

namespace fabric::runtime {

namespace {

float squared_distance(const core::Vec2 a, const core::Vec2 b) noexcept {
    const auto x = a.x - b.x;
    const auto y = a.y - b.y;
    return x * x + y * y;
}

float squared_distance_to_segment(const core::Vec2 point, const core::Vec2 start,
                                  const core::Vec2 end) noexcept {
    const auto delta = core::Vec2{end.x - start.x, end.y - start.y};
    const auto length_squared = delta.x * delta.x + delta.y * delta.y;
    if (length_squared <= 1.0e-8F) return squared_distance(point, start);
    const auto projection = ((point.x - start.x) * delta.x +
                             (point.y - start.y) * delta.y) / length_squared;
    const auto t = std::clamp(projection, 0.0F, 1.0F);
    return squared_distance(point, {start.x + delta.x * t, start.y + delta.y * t});
}

bool point_in_polygon(const std::vector<core::Vec2>& points,
                      const core::Vec2 position) noexcept {
    if (points.size() < 3U) return false;
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1U; i < points.size(); j = i++) {
        const auto& left = points[i];
        const auto& right = points[j];
        const auto crosses = ((left.y > position.y) != (right.y > position.y)) &&
            (position.x < (right.x - left.x) * (position.y - left.y) /
                              (right.y - left.y) + left.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

} // namespace

TriggerRuntime::TriggerRuntime(const project::MapDocument& map) : map_(&map), active_(map.triggers.size()) {}

void TriggerRuntime::reset() noexcept {
    std::fill(active_.begin(), active_.end(), false);
}

bool TriggerRuntime::contains(const project::CollisionShape& shape,
                              const core::Vec2 position) const noexcept {
    switch (shape.kind) {
    case project::CollisionShapeKind::circle:
        return squared_distance(position, shape.center) <= shape.radius * shape.radius;
    case project::CollisionShapeKind::capsule: {
        const auto half_length = std::max(0.0F, shape.length) * 0.5F;
        const auto start = core::Vec2{shape.center.x - half_length, shape.center.y};
        const auto end = core::Vec2{shape.center.x + half_length, shape.center.y};
        return squared_distance_to_segment(position, start, end) <= shape.radius * shape.radius;
    }
    case project::CollisionShapeKind::polygon:
        return point_in_polygon(shape.points, position);
    case project::CollisionShapeKind::chain:
        return false;
    }
    return false;
}

std::vector<GameplayEvent> TriggerRuntime::update(const core::Vec2 position) {
    std::vector<GameplayEvent> events;
    if (map_ == nullptr) return events;
    for (std::size_t index = 0; index < map_->triggers.size(); ++index) {
        const auto& trigger = map_->triggers[index];
        const auto inside = trigger.collision_index < map_->collisions.size() &&
            contains(map_->collisions[trigger.collision_index], position);
        if (inside && !active_[index]) {
            const auto event = std::find_if(map_->events.begin(), map_->events.end(),
                [&](const auto& candidate) { return candidate.id == trigger.event_id; });
            events.push_back({trigger.event_id, trigger.id,
                              event == map_->events.end() ? std::vector<project::MapProperty>{}
                                                           : event->payload});
        }
        active_[index] = inside;
    }
    return events;
}

std::size_t TriggerRuntime::active_count() const noexcept {
    return static_cast<std::size_t>(std::count(active_.begin(), active_.end(), true));
}

} // namespace fabric::runtime
