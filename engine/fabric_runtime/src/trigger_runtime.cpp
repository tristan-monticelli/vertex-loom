#include "fabric/runtime/trigger_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace fabric::runtime {

namespace {

float squared_distance(const core::Vec2 a, const core::Vec2 b) noexcept {
    const auto x = a.x - b.x;
    const auto y = a.y - b.y;
    return x * x + y * y;
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

bool point_in_rect(const core::Vec2 point, const core::Rect& rect) noexcept {
    return point.x >= rect.origin.x &&
        point.x <= rect.origin.x + rect.size.x &&
        point.y >= rect.origin.y &&
        point.y <= rect.origin.y + rect.size.y;
}

float cross(const core::Vec2 a, const core::Vec2 b,
            const core::Vec2 c) noexcept {
    return (b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x);
}

bool segments_intersect(const core::Vec2 a, const core::Vec2 b,
                        const core::Vec2 c, const core::Vec2 d) noexcept {
    constexpr float epsilon = 1.0e-6F;
    const auto ab_c = cross(a, b, c);
    const auto ab_d = cross(a, b, d);
    const auto cd_a = cross(c, d, a);
    const auto cd_b = cross(c, d, b);
    if (((ab_c > epsilon && ab_d < -epsilon) ||
         (ab_c < -epsilon && ab_d > epsilon)) &&
        ((cd_a > epsilon && cd_b < -epsilon) ||
         (cd_a < -epsilon && cd_b > epsilon)))
        return true;
    const auto on_segment = [&](const core::Vec2 p, const core::Vec2 q,
                                const core::Vec2 value, const float area) {
        return std::abs(area) <= epsilon &&
            value.x >= std::min(p.x, q.x) - epsilon &&
            value.x <= std::max(p.x, q.x) + epsilon &&
            value.y >= std::min(p.y, q.y) - epsilon &&
            value.y <= std::max(p.y, q.y) + epsilon;
    };
    return on_segment(a, b, c, ab_c) || on_segment(a, b, d, ab_d) ||
        on_segment(c, d, a, cd_a) || on_segment(c, d, b, cd_b);
}

std::vector<project::MapProperty> payload_for(
    const project::MapDocument& map,
    const project::TriggerDefinition& trigger) {
    const auto event = std::find_if(
        map.events.begin(), map.events.end(), [&](const auto& candidate) {
            return candidate.id == trigger.event_id;
        });
    auto payload = event == map.events.end()
        ? std::vector<project::MapProperty>{} : event->payload;
    for (const auto& property : trigger.properties) {
        const auto existing = std::find_if(
            payload.begin(), payload.end(), [&](const auto& candidate) {
                return candidate.id == property.id;
            });
        if (existing == payload.end()) payload.push_back(property);
        else *existing = property;
    }
    return payload;
}

} // namespace

TriggerRuntime::TriggerRuntime(const project::MapDocument& map) : map_(&map), active_(map.triggers.size()) {}

void TriggerRuntime::reset() noexcept {
    for (auto& actors : active_) actors.clear();
}

bool TriggerRuntime::intersects(const project::CollisionShape& shape,
                                const core::Rect& bounds) const noexcept {
    const auto minimum = bounds.origin;
    const auto maximum = core::Vec2{bounds.origin.x + bounds.size.x,
                                    bounds.origin.y + bounds.size.y};
    switch (shape.kind) {
    case project::CollisionShapeKind::circle: {
        const auto closest = core::Vec2{
            std::clamp(shape.center.x, minimum.x, maximum.x),
            std::clamp(shape.center.y, minimum.y, maximum.y)};
        return squared_distance(closest, shape.center) <=
            shape.radius * shape.radius;
    }
    case project::CollisionShapeKind::capsule: {
        const auto half_length = std::max(0.0F, shape.length) * 0.5F;
        const auto segment_min = shape.center.x - half_length;
        const auto segment_max = shape.center.x + half_length;
        const auto dx = segment_max < minimum.x ? minimum.x - segment_max
            : segment_min > maximum.x ? segment_min - maximum.x : 0.0F;
        const auto dy = shape.center.y < minimum.y
            ? minimum.y - shape.center.y
            : shape.center.y > maximum.y
                ? shape.center.y - maximum.y : 0.0F;
        return dx * dx + dy * dy <= shape.radius * shape.radius;
    }
    case project::CollisionShapeKind::polygon: {
        const std::array corners{
            minimum, core::Vec2{maximum.x, minimum.y}, maximum,
            core::Vec2{minimum.x, maximum.y}};
        if (std::ranges::any_of(corners, [&](const auto point) {
                return point_in_polygon(shape.points, point);
            }) ||
            std::ranges::any_of(shape.points, [&](const auto point) {
                return point_in_rect(point, bounds);
            }))
            return true;
        for (std::size_t index = 0; index < shape.points.size(); ++index) {
            const auto start = shape.points[index];
            const auto end = shape.points[(index + 1U) % shape.points.size()];
            for (std::size_t edge = 0; edge < corners.size(); ++edge)
                if (segments_intersect(start, end, corners[edge],
                                       corners[(edge + 1U) % corners.size()]))
                    return true;
        }
        return false;
    }
    case project::CollisionShapeKind::chain:
        return false;
    }
    return false;
}

std::vector<GameplayEvent> TriggerRuntime::update(const core::Vec2 position) {
    return update({{"character", {position, {0.0F, 0.0F}}}});
}

std::vector<GameplayEvent> TriggerRuntime::update(
    const std::vector<TriggerActor>& actors) {
    std::vector<GameplayEvent> events;
    if (map_ == nullptr) return events;
    std::map<std::string, core::Rect> actors_by_id;
    for (const auto& actor : actors)
        if (!actor.id.empty() &&
            std::isfinite(actor.bounds.origin.x) &&
            std::isfinite(actor.bounds.origin.y) &&
            std::isfinite(actor.bounds.size.x) &&
            std::isfinite(actor.bounds.size.y) &&
            actor.bounds.size.x >= 0.0F && actor.bounds.size.y >= 0.0F)
            actors_by_id[actor.id] = actor.bounds;
    for (std::size_t index = 0; index < map_->triggers.size(); ++index) {
        const auto& trigger = map_->triggers[index];
        std::set<std::string> current;
        if (trigger.collision_index < map_->collisions.size()) {
            const auto& shape = map_->collisions[trigger.collision_index];
            for (const auto& [actor_id, bounds] : actors_by_id)
                if (intersects(shape, bounds)) current.insert(actor_id);
        }
        const auto payload = payload_for(*map_, trigger);
        for (const auto& actor_id : current)
            if (!active_[index].contains(actor_id))
                events.push_back({trigger.event_id, trigger.id, actor_id,
                                  payload, GameplayEventKind::entered});
        for (const auto& actor_id : active_[index])
            if (!current.contains(actor_id))
                events.push_back({trigger.event_id, trigger.id, actor_id,
                                  payload, GameplayEventKind::exited});
        active_[index] = std::move(current);
    }
    return events;
}

std::size_t TriggerRuntime::active_count() const noexcept {
    std::size_t count{};
    for (const auto& actors : active_) count += actors.size();
    return count;
}

} // namespace fabric::runtime
