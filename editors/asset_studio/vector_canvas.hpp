#pragma once

#include "fabric/core/types.hpp"
#include "fabric/editor/canvas_interaction.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/project/vector_asset.hpp"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fabric::asset_studio {

struct CanvasUiState {
    enum class Tool {
        move,
        rotate,
        scale,
        pivot,
        pen,
    };

    enum class DragOperation {
        none,
        move,
        rotate,
        scale,
        pivot,
        path_point,
        path_selection,
        pen_segment,
        bezier_handle1,
        bezier_handle2,
    };

    float zoom{1.0F};
    ImVec2 pan{};
    std::size_t selected_node{};
    std::string selected_entity_id;
    std::vector<std::size_t> selected_entity_nodes;
    std::vector<fabric::core::Transform> entity_display_transforms;
    bool native_canvas{};
    Tool tool{Tool::move};
    fabric::editor::BezierHandleMode bezier_handle_mode{
        fabric::editor::BezierHandleMode::linked};
    bool dragging{};
    DragOperation drag_operation{DragOperation::none};
    ImVec2 drag_start_mouse{};
    fabric::core::Transform drag_start_transform;
    fabric::project::VectorNode drag_start_node;
    bool entity_gizmo_dragging{};
    ImVec2 entity_gizmo_start_mouse{};
    ImVec2 entity_gizmo_screen{};
    bool xpbd_overlay_visible{};
    fabric::core::Transform entity_gizmo_start_transform;
    std::vector<std::pair<std::size_t, fabric::core::Transform>>
        entity_gizmo_start_transforms;
    std::size_t path_command_index{};
    std::vector<std::size_t> selected_path_points;
    std::optional<fabric::project::VectorNode> pen_start_node;
    ImVec2 native_origin{};
    ImVec2 native_size{};
    fabric::core::Rect native_world_bounds;
    fabric::core::Rect entity_world_bounds{{-5.0F, -5.0F}, {10.0F, 10.0F}};
    std::string crop_resource_id;
    std::optional<fabric::editor::RasterCropDrag> crop_drag;
    ImVec2 crop_start_mouse{};
    fabric::project::RasterView crop_start_view;
};

void draw_native_vector_canvas(fabric::editor::ProjectSession& session,
                               CanvasUiState& canvas, ImVec2 available,
                               std::string& status);

[[nodiscard]] bool start_new_freeform_path(
    fabric::editor::ProjectSession& session, CanvasUiState& canvas,
    std::string& status);

} // namespace fabric::asset_studio
