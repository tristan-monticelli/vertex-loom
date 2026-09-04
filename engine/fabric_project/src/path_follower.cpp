#include "fabric/project/path_follower.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <ranges>
#include <vector>

namespace fabric::project {
namespace {

core::Vec2 cubic(const core::Vec2 a, const core::Vec2 b, const core::Vec2 c,
                const core::Vec2 d, const float t) {
    const float one_minus = 1.0F - t;
    return {one_minus * one_minus * one_minus * a.x +
                3.0F * one_minus * one_minus * t * b.x +
                3.0F * one_minus * t * t * c.x + t * t * t * d.x,
            one_minus * one_minus * one_minus * a.y +
                3.0F * one_minus * one_minus * t * b.y +
                3.0F * one_minus * t * t * c.y + t * t * t * d.y};
}

float length(const core::Vec2 value) noexcept {
    return std::hypot(value.x, value.y);
}

std::vector<core::Vec2> flatten(const TexturedPath& path,
                                const std::size_t subdivisions) {
    std::vector<core::Vec2> points;
    if (path.commands.empty()) return points;
    const auto steps = std::max<std::size_t>(1U, subdivisions);
    core::Vec2 current = path.commands.front().point;
    points.push_back(current);
    for (std::size_t index = 1U; index < path.commands.size(); ++index) {
        const auto& command = path.commands[index];
        if (command.kind == TexturedPathCommandKind::line) {
            points.push_back(command.point);
            current = command.point;
        } else if (command.kind == TexturedPathCommandKind::cubic) {
            for (std::size_t step = 1U; step <= steps; ++step)
                points.push_back(cubic(current, command.control1,
                                       command.control2, command.point,
                                       static_cast<float>(step) /
                                           static_cast<float>(steps)));
            current = command.point;
        } else if (command.kind == TexturedPathCommandKind::move) {
            points.push_back(command.point);
            current = command.point;
        }
    }
    if (path.closed && points.size() > 1U && points.front() != points.back())
        points.push_back(points.front());
    return points;
}

float path_length(const std::vector<core::Vec2>& points) noexcept {
    float total = 0.0F;
    for (std::size_t index = 1U; index < points.size(); ++index)
        total += length({points[index].x - points[index - 1U].x,
                         points[index].y - points[index - 1U].y});
    return total;
}

} // namespace

PathFollowerSample sample_textured_path(const TexturedPath& path,
                                        const float normalized_progress,
                                        const std::size_t curve_subdivisions) {
    const auto points = flatten(path, curve_subdivisions);
    if (points.empty()) return {};
    if (points.size() == 1U)
        return {.position = points.front(), .progress = 0.0F};
    std::vector<float> cumulative(points.size(), 0.0F);
    for (std::size_t index = 1U; index < points.size(); ++index)
        cumulative[index] = cumulative[index - 1U] + length({
            points[index].x - points[index - 1U].x,
            points[index].y - points[index - 1U].y});
    const float total = cumulative.back();
    if (!(total > 0.0F) || !std::isfinite(total))
        return {.position = points.front(), .progress = 0.0F};
    const float progress = std::clamp(normalized_progress, 0.0F, 1.0F);
    const float target = progress * total;
    const auto upper = std::ranges::upper_bound(cumulative, target);
    const std::size_t next = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::distance(cumulative.begin(), upper)),
        1U, points.size() - 1U);
    const float segment = cumulative[next] - cumulative[next - 1U];
    const float local = segment > 0.0F
        ? (target - cumulative[next - 1U]) / segment : 0.0F;
    const core::Vec2 delta{points[next].x - points[next - 1U].x,
                           points[next].y - points[next - 1U].y};
    const float tangent_length = length(delta);
    return {.position = {points[next - 1U].x + delta.x * local,
                         points[next - 1U].y + delta.y * local},
            .tangent = tangent_length > 0.0F
                ? core::Vec2{delta.x / tangent_length, delta.y / tangent_length}
                : core::Vec2{1.0F, 0.0F},
            .progress = progress};
}

float advance_path_follower(const TexturedPath& path,
                            const float normalized_progress, const float speed,
                            const float delta_seconds, const bool loop) {
    const auto points = flatten(path, 32U);
    const float total = path_length(points);
    if (!(total > 0.0F) || !std::isfinite(speed) ||
        !std::isfinite(delta_seconds))
        return std::clamp(normalized_progress, 0.0F, 1.0F);
    const float next = normalized_progress + speed * delta_seconds / total;
    if (loop) {
        float wrapped = std::fmod(next, 1.0F);
        if (wrapped < 0.0F) wrapped += 1.0F;
        return wrapped;
    }
    return std::clamp(next, 0.0F, 1.0F);
}

} // namespace fabric::project
