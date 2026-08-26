#include "fabric/editor/canvas_interaction.hpp"

#include <cmath>
#include <numbers>

namespace fabric::editor {

core::Vec2 extend_canvas_handle(const core::Vec2 center, const core::Vec2 edge,
                                const float distance) noexcept {
    const float delta_x = edge.x - center.x;
    const float delta_y = edge.y - center.y;
    const float length = std::hypot(delta_x, delta_y);
    if (length <= 0.0001F) return edge;
    return {edge.x + delta_x * distance / length,
            edge.y + delta_y * distance / length};
}

bool point_hits_vector_node(const core::Vec2 world,
                            const project::VectorNode& node,
                            const float tolerance) noexcept {
    const auto& transform = node.transform;
    const float angle = -transform.rotation_degrees *
        std::numbers::pi_v<float> / 180.0F;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float delta_x = world.x - transform.position.x - transform.pivot.x;
    const float delta_y = world.y - transform.position.y - transform.pivot.y;
    const float scale_x = std::abs(transform.scale.x) > 0.0001F
        ? transform.scale.x : 0.0001F;
    const float scale_y = std::abs(transform.scale.y) > 0.0001F
        ? transform.scale.y : 0.0001F;
    const core::Vec2 local{
        (delta_x * cosine - delta_y * sine) / scale_x + transform.pivot.x,
        (delta_x * sine + delta_y * cosine) / scale_y + transform.pivot.y};
    const auto& bounds = node.shape.bounds;
    return local.x >= bounds.origin.x - tolerance &&
        local.x <= bounds.origin.x + bounds.size.x + tolerance &&
        local.y >= bounds.origin.y - tolerance &&
        local.y <= bounds.origin.y + bounds.size.y + tolerance;
}

std::optional<std::size_t> topmost_vector_node_at(
    const std::span<const project::VectorNode> nodes, const core::Vec2 world,
    const float tolerance) noexcept {
    for (std::size_t index = nodes.size(); index > 0U; --index) {
        const auto& candidate = nodes[index - 1U];
        if (candidate.visible &&
            point_hits_vector_node(world, candidate, tolerance)) {
            return index - 1U;
        }
    }
    return std::nullopt;
}

} // namespace fabric::editor
