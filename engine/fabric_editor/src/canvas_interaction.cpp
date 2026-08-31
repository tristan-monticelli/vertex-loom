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

bool update_bezier_handle(project::VectorShape& shape, const std::size_t index,
                          const bool first, const core::Vec2 next,
                          const BezierHandleMode mode) noexcept {
    using Kind = project::VectorPathCommandKind;
    if (shape.kind != project::VectorShapeKind::path ||
        index >= shape.path.size() || shape.path[index].kind != Kind::cubic)
        return false;
    auto& command = shape.path[index];
    const auto anchor = command.point;
    const auto opposite = first ? command.control2 : command.control1;
    const float dx = next.x - anchor.x;
    const float dy = next.y - anchor.y;
    if (first) command.control1 = next;
    else command.control2 = next;
    if (mode == BezierHandleMode::free) return true;
    if (mode == BezierHandleMode::symmetric) {
        const auto mirrored = core::Vec2{anchor.x - dx, anchor.y - dy};
        if (first) command.control2 = mirrored;
        else command.control1 = mirrored;
        return true;
    }
    const float length = std::hypot(dx, dy);
    const float opposite_length = std::hypot(
        opposite.x - anchor.x, opposite.y - anchor.y);
    if (length > 0.0001F) {
        const float factor = opposite_length / length;
        const auto aligned = core::Vec2{
            anchor.x - dx * factor, anchor.y - dy * factor};
        if (first) command.control2 = aligned;
        else command.control1 = aligned;
    }
    return true;
}

bool create_bezier_segment(project::VectorShape& shape, const std::size_t index,
                           const core::Vec2 end_point) noexcept {
    using Kind = project::VectorPathCommandKind;
    if (shape.kind != project::VectorShapeKind::path || index >= shape.path.size() ||
        shape.path[index].kind != Kind::line) return false;
    core::Vec2 start_point{};
    for (std::size_t cursor = index; cursor-- > 0U;) {
        const auto& candidate = shape.path[cursor];
        if (candidate.kind == Kind::move || candidate.kind == Kind::line ||
            candidate.kind == Kind::cubic) {
            start_point = candidate.point;
            break;
        }
    }
    const core::Vec2 delta{end_point.x - start_point.x,
                           end_point.y - start_point.y};
    const core::Vec2 normal{-delta.y * 0.2F, delta.x * 0.2F};
    auto& command = shape.path[index];
    command.kind = Kind::cubic;
    command.point = end_point;
    command.control1 = {start_point.x + delta.x / 3.0F + normal.x,
                        start_point.y + delta.y / 3.0F + normal.y};
    command.control2 = {end_point.x - delta.x / 3.0F + normal.x,
                        end_point.y - delta.y / 3.0F + normal.y};
    return true;
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
