#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/texture_asset.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace fabric::editor {

[[nodiscard]] core::Vec2 extend_canvas_handle(
    core::Vec2 center, core::Vec2 edge, float distance) noexcept;

[[nodiscard]] bool point_hits_vector_node(
    core::Vec2 world, const project::VectorNode& node,
    float tolerance = 0.0F) noexcept;

[[nodiscard]] std::optional<std::size_t> topmost_vector_node_at(
    std::span<const project::VectorNode> nodes, core::Vec2 world,
    float tolerance = 0.0F) noexcept;

enum class BezierHandleMode { linked, symmetric, free };

[[nodiscard]] bool update_bezier_handle(
    project::VectorShape& shape, std::size_t index, bool first,
    core::Vec2 next, BezierHandleMode mode) noexcept;

enum class RasterCropDrag {
    move,
    top_left,
    top_right,
    bottom_left,
    bottom_right,
};

[[nodiscard]] project::RasterView drag_raster_crop(
    const project::RasterView& start, RasterCropDrag operation,
    core::Vec2 delta_pixels, std::uint32_t source_width,
    std::uint32_t source_height) noexcept;

} // namespace fabric::editor
