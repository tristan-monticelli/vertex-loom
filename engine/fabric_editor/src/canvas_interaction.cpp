#include "fabric/editor/canvas_interaction.hpp"

#include <algorithm>
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

project::RasterView drag_raster_crop(
    const project::RasterView& start, const RasterCropDrag operation,
    const core::Vec2 delta_pixels, const std::uint32_t source_width,
    const std::uint32_t source_height) noexcept {
    auto result = start;
    const float maximum_x = static_cast<float>(source_width);
    const float maximum_y = static_cast<float>(source_height);
    float left = start.crop.origin.x;
    float top = start.crop.origin.y;
    float right = left + start.crop.size.x;
    float bottom = top + start.crop.size.y;
    if (operation == RasterCropDrag::move) {
        left = std::clamp(left + delta_pixels.x, 0.0F,
                          maximum_x - start.crop.size.x);
        top = std::clamp(top + delta_pixels.y, 0.0F,
                         maximum_y - start.crop.size.y);
        right = left + start.crop.size.x;
        bottom = top + start.crop.size.y;
    } else {
        if (operation == RasterCropDrag::top_left ||
            operation == RasterCropDrag::bottom_left) {
            left = std::clamp(left + delta_pixels.x, 0.0F, right - 1.0F);
        }
        if (operation == RasterCropDrag::top_right ||
            operation == RasterCropDrag::bottom_right) {
            right = std::clamp(right + delta_pixels.x, left + 1.0F, maximum_x);
        }
        if (operation == RasterCropDrag::top_left ||
            operation == RasterCropDrag::top_right) {
            top = std::clamp(top + delta_pixels.y, 0.0F, bottom - 1.0F);
        }
        if (operation == RasterCropDrag::bottom_left ||
            operation == RasterCropDrag::bottom_right) {
            bottom = std::clamp(bottom + delta_pixels.y, top + 1.0F, maximum_y);
        }
    }
    result.crop = {{left, top}, {right - left, bottom - top}};
    return result;
}

} // namespace fabric::editor
