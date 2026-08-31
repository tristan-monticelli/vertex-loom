#include "vector_canvas.hpp"

#include "fabric/editor/project_session.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>
#include <utility>

namespace fabric::asset_studio {

namespace {

ImU32 color_to_u32(const fabric::core::Color& color) {
    const auto channel = [](const float value) {
        return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
    };
    return IM_COL32(channel(color.red), channel(color.green),
                    channel(color.blue), channel(color.alpha));
}

} // namespace

void draw_native_vector_canvas(fabric::editor::ProjectSession& session,
                               CanvasUiState& canvas, const ImVec2 available) {
    if (!session.created_vector()) return;
    const auto& asset = *session.created_vector();
    if (!asset.native || asset.native->size.x <= 0.0F ||
        asset.native->size.y <= 0.0F) {
        ImGui::TextDisabled("Native artwork has no drawable canvas.");
        return;
    }
    ImGui::InvisibleButton("Native canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 origin = ImGui::GetItemRectMin();
    canvas.native_canvas = true;
    canvas.native_origin = origin;
    canvas.native_size = available;
    const ImVec2 center{origin.x + available.x * 0.5F,
                        origin.y + available.y * 0.5F};
    const float fit = std::min((available.x - 80.0F) / asset.native->size.x,
                               (available.y - 80.0F) / asset.native->size.y);
    const bool hovered = ImGui::IsItemHovered();
    auto& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.0F) {
        const float old_scale = fit * canvas.zoom;
        const ImVec2 mouse = io.MousePos;
        const ImVec2 world_under_cursor{
            (mouse.x - center.x - canvas.pan.x) / old_scale,
            -(mouse.y - center.y - canvas.pan.y) / old_scale};
        canvas.zoom = std::clamp(
            canvas.zoom * (io.MouseWheel > 0.0F ? 1.15F : 1.0F / 1.15F),
            0.1F, 20.0F);
        const float new_scale = fit * canvas.zoom;
        canvas.pan.x = mouse.x - center.x - world_under_cursor.x * new_scale;
        canvas.pan.y = mouse.y - center.y + world_under_cursor.y * new_scale;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        canvas.pan.x += delta.x;
        canvas.pan.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    const float pixels_per_unit = fit * canvas.zoom;
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{center.x + canvas.pan.x + point.x * pixels_per_unit,
                      center.y + canvas.pan.y - point.y * pixels_per_unit};
    };
    const auto to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{
            (point.x - center.x - canvas.pan.x) / pixels_per_unit,
            -(point.y - center.y - canvas.pan.y) / pixels_per_unit};
    };
    const auto transform_point = [](const fabric::project::VectorNode& node,
                                    const fabric::core::Vec2 point) {
        const float x = (point.x - node.transform.pivot.x) *
            node.transform.scale.x;
        const float y = (point.y - node.transform.pivot.y) *
            node.transform.scale.y;
        const float angle = node.transform.rotation_degrees *
            std::numbers::pi_v<float> / 180.0F;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        return fabric::core::Vec2{
            node.transform.position.x + node.transform.pivot.x +
                x * cosine - y * sine,
            node.transform.position.y + node.transform.pivot.y +
                x * sine + y * cosine};
    };
    const auto world_to_local = [](const fabric::project::VectorNode& node,
                                   const fabric::core::Vec2 world) {
        const float angle = -node.transform.rotation_degrees *
            std::numbers::pi_v<float> / 180.0F;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x = world.x - node.transform.position.x - node.transform.pivot.x;
        const float y = world.y - node.transform.position.y - node.transform.pivot.y;
        return fabric::core::Vec2{
            (x * cosine + y * sine) /
                std::max(std::abs(node.transform.scale.x), 0.0001F) +
                node.transform.pivot.x,
            (-x * sine + y * cosine) /
                std::max(std::abs(node.transform.scale.y), 0.0001F) +
                node.transform.pivot.y};
    };
    auto* draw_list = ImGui::GetWindowDrawList();
    const float target_grid_pixels = 48.0F;
    const float raw_grid_step = target_grid_pixels / pixels_per_unit;
    const float grid_power = std::pow(10.0F,
                                      std::floor(std::log10(raw_grid_step)));
    const float normalized_grid = raw_grid_step / grid_power;
    const float grid_step = (normalized_grid <= 1.0F ? 1.0F
                              : normalized_grid <= 2.0F ? 2.0F
                              : normalized_grid <= 5.0F ? 5.0F
                                                       : 10.0F) * grid_power;
    const float world_half_width = available.x / (2.0F * pixels_per_unit);
    const float world_half_height = available.y / (2.0F * pixels_per_unit);
    const float world_left = -world_half_width - canvas.pan.x / pixels_per_unit;
    const float world_right = world_half_width - canvas.pan.x / pixels_per_unit;
    const float world_bottom = -world_half_height + canvas.pan.y / pixels_per_unit;
    const float world_top = world_half_height + canvas.pan.y / pixels_per_unit;
    canvas.native_world_bounds = {
        .origin = {world_left, world_bottom},
        .size = {world_right - world_left, world_top - world_bottom},
    };
    const int first_vertical = static_cast<int>(std::floor(world_left / grid_step));
    const int last_vertical = static_cast<int>(std::ceil(world_right / grid_step));
    const int first_horizontal = static_cast<int>(std::floor(world_bottom / grid_step));
    const int last_horizontal = static_cast<int>(std::ceil(world_top / grid_step));
    const fabric::project::VectorNode* selected_node =
        !asset.native->nodes.empty() &&
                canvas.selected_node < asset.native->nodes.size()
            ? &asset.native->nodes[canvas.selected_node]
            : nullptr;
    ImVec2 rotate_handle{};
    ImVec2 rotate_anchor{};
    ImVec2 scale_handle{};
    ImVec2 pivot_handle{};
    ImVec2 transform_center{};
    if (selected_node != nullptr) {
        const auto& bounds = selected_node->shape.bounds;
        const fabric::core::Vec2 local_center{
            bounds.origin.x + bounds.size.x * 0.5F,
            bounds.origin.y + bounds.size.y * 0.5F};
        const auto world_center = transform_point(*selected_node, local_center);
        const auto world_top = transform_point(
            *selected_node,
            {local_center.x, bounds.origin.y + bounds.size.y});
        const auto world_bottom_right = transform_point(
            *selected_node,
            {bounds.origin.x + bounds.size.x,
             bounds.origin.y});
        transform_center = to_screen(world_center);
        const ImVec2 top = to_screen(world_top);
        rotate_anchor = top;
        const auto extended = fabric::editor::extend_canvas_handle(
            {transform_center.x, transform_center.y}, {top.x, top.y}, 30.0F);
        rotate_handle = {extended.x, extended.y};
        scale_handle = to_screen(world_bottom_right);
        pivot_handle = to_screen(
            transform_point(*selected_node, selected_node->transform.pivot));
    }
    bool path_command_edited = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = io.MousePos;
        const auto distance = [](const ImVec2 left, const ImVec2 right) {
            return std::hypot(left.x - right.x, left.y - right.y);
        };
        CanvasUiState::DragOperation operation =
            CanvasUiState::DragOperation::none;
        if (selected_node != nullptr && !selected_node->locked) {
            const auto hit_existing_path_target = [&]() {
                if (selected_node->shape.kind !=
                    fabric::project::VectorShapeKind::path)
                    return false;
                float closest = 10.0F;
                std::optional<std::size_t> hit_anchor;
                for (std::size_t index = 0;
                     index < selected_node->shape.path.size(); ++index) {
                    const auto& command = selected_node->shape.path[index];
                    if (command.kind == fabric::project::VectorPathCommandKind::move ||
                        command.kind == fabric::project::VectorPathCommandKind::line ||
                        command.kind == fabric::project::VectorPathCommandKind::cubic) {
                        const auto candidate = to_screen(
                            transform_point(*selected_node, command.point));
                        const float hit = distance(mouse, candidate);
                        if (hit <= closest) {
                            closest = hit;
                            hit_anchor = index;
                        }
                    }
                    if (command.kind == fabric::project::VectorPathCommandKind::cubic) {
                        for (const auto [handle, candidate_operation] : {
                                 std::pair{command.control1,
                                           CanvasUiState::DragOperation::bezier_handle1},
                                 std::pair{command.control2,
                                           CanvasUiState::DragOperation::bezier_handle2}}) {
                            const auto candidate = to_screen(
                                transform_point(*selected_node, handle));
                            const float hit = distance(mouse, candidate);
                            if (hit <= closest) {
                                closest = hit;
                                operation = candidate_operation;
                                canvas.path_command_index = index;
                                canvas.selected_path_points = {index};
                            }
                        }
                    }
                }
                if (operation == CanvasUiState::DragOperation::none && hit_anchor) {
                    const auto selected = std::ranges::find(
                        canvas.selected_path_points, *hit_anchor);
                    if (io.KeyShift) {
                        if (selected == canvas.selected_path_points.end())
                            canvas.selected_path_points.push_back(*hit_anchor);
                        else
                            canvas.selected_path_points.erase(selected);
                    } else {
                        if (selected == canvas.selected_path_points.end())
                            canvas.selected_path_points = {*hit_anchor};
                        operation = canvas.selected_path_points.size() > 1U
                            ? CanvasUiState::DragOperation::path_selection
                            : CanvasUiState::DragOperation::path_point;
                        canvas.path_command_index = *hit_anchor;
                    }
                    return true;
                }
                return operation != CanvasUiState::DragOperation::none;
            };
            if (canvas.tool == CanvasUiState::Tool::pen &&
                selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
                const bool editing_existing = hit_existing_path_target();
                auto changed = *selected_node;
                const auto local = world_to_local(*selected_node, to_world(mouse));
                std::size_t insert_index = changed.shape.path.size();
                float closest_segment = 14.0F;
                fabric::core::Vec2 previous{};
                bool has_previous = false;
                for (std::size_t index = 0; index < changed.shape.path.size(); ++index) {
                    const auto& command = changed.shape.path[index];
                    if (command.kind == fabric::project::VectorPathCommandKind::move) {
                        previous = command.point;
                        has_previous = true;
                        continue;
                    }
                    if ((command.kind == fabric::project::VectorPathCommandKind::line ||
                         command.kind == fabric::project::VectorPathCommandKind::cubic) &&
                        has_previous) {
                        const auto start = to_screen(transform_point(*selected_node, previous));
                        const auto end = to_screen(transform_point(*selected_node, command.point));
                        const ImVec2 segment{end.x - start.x, end.y - start.y};
                        const ImVec2 relative{mouse.x - start.x, mouse.y - start.y};
                        const float length_squared = segment.x * segment.x + segment.y * segment.y;
                        const float factor = length_squared > 0.0001F
                            ? std::clamp((relative.x * segment.x + relative.y * segment.y) /
                                             length_squared,
                                         0.0F, 1.0F)
                            : 0.0F;
                        const ImVec2 projection{start.x + segment.x * factor,
                                                start.y + segment.y * factor};
                        const float distance = std::hypot(
                            mouse.x - projection.x, mouse.y - projection.y);
                        if (distance < closest_segment) {
                            closest_segment = distance;
                            insert_index = index;
                        }
                        previous = command.point;
                    } else if (command.kind != fabric::project::VectorPathCommandKind::close) {
                        previous = command.point;
                    }
                }
                if (!editing_existing && insert_index == changed.shape.path.size() &&
                    !changed.shape.path.empty() &&
                    changed.shape.path.back().kind ==
                        fabric::project::VectorPathCommandKind::close)
                    insert_index -= 1U;
                if (editing_existing) {
                    path_command_edited = true;
                } else if (changed.shape.path.empty()) {
                    changed.shape.path.push_back({
                        .kind = fabric::project::VectorPathCommandKind::move,
                        .point = local});
                    static_cast<void>(session.set_selected_vector_node(
                        canvas.selected_node, std::move(changed)));
                } else {
                    const auto command = fabric::project::VectorShape::PathCommand{
                        .kind = fabric::project::VectorPathCommandKind::line,
                        .point = local};
                    const bool inserted = fabric::project::insert_path_command(
                        changed.shape, insert_index, command);
                    const auto authored_node = changed;
                    const bool applied = inserted &&
                        session.set_selected_vector_node(
                            canvas.selected_node, std::move(changed));
                    if (applied) {
                        canvas.selected_path_points = {insert_index};
                        canvas.path_command_index = insert_index;
                        canvas.dragging = true;
                        canvas.drag_operation =
                            CanvasUiState::DragOperation::pen_segment;
                        canvas.drag_start_mouse = mouse;
                        canvas.drag_start_transform = authored_node.transform;
                        canvas.drag_start_node = authored_node;
                        path_command_edited = true;
                    }
                }
            }
            if (canvas.tool == CanvasUiState::Tool::move &&
                selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
                static_cast<void>(hit_existing_path_target());
            }
            if (operation == CanvasUiState::DragOperation::none &&
                canvas.tool == CanvasUiState::Tool::rotate &&
                distance(mouse, rotate_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::rotate;
            } else if (operation == CanvasUiState::DragOperation::none &&
                       canvas.tool == CanvasUiState::Tool::scale &&
                       distance(mouse, scale_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::scale;
            } else if (operation == CanvasUiState::DragOperation::none &&
                       canvas.tool == CanvasUiState::Tool::pivot &&
                       distance(mouse, pivot_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::pivot;
            }
        }
        if (operation == CanvasUiState::DragOperation::none &&
            !path_command_edited) {
            const auto world = to_world(mouse);
            const auto hit_node = fabric::editor::topmost_vector_node_at(
                asset.native->nodes, world, 8.0F / pixels_per_unit);
            if (hit_node) {
                if (*hit_node == canvas.selected_node && selected_node != nullptr &&
                    !selected_node->locked &&
                    canvas.tool == CanvasUiState::Tool::move) {
                    operation = CanvasUiState::DragOperation::move;
                } else {
                    canvas.selected_path_points.clear();
                    canvas.selected_node = *hit_node;
                }
            }
        }
        if (operation != CanvasUiState::DragOperation::none &&
            selected_node != nullptr) {
            canvas.dragging = true;
            canvas.drag_operation = operation;
            canvas.drag_start_mouse = mouse;
            canvas.drag_start_transform = selected_node->transform;
            canvas.drag_start_node = *selected_node;
        }
    }
    const bool right_click = ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
        (io.MouseDown[ImGuiMouseButton_Right] &&
         io.MouseDownDuration[ImGuiMouseButton_Right] == 0.0F);
    if (hovered && right_click &&
        selected_node != nullptr && !selected_node->locked &&
        canvas.tool == CanvasUiState::Tool::pen &&
        selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
        const ImVec2 mouse = io.MousePos;
        std::optional<std::size_t> hit;
        float closest = 10.0F;
        for (std::size_t index = 0; index < selected_node->shape.path.size(); ++index) {
            const auto& command = selected_node->shape.path[index];
            if (command.kind == fabric::project::VectorPathCommandKind::move ||
                command.kind == fabric::project::VectorPathCommandKind::line ||
                command.kind == fabric::project::VectorPathCommandKind::cubic) {
                const auto candidate = to_screen(
                    transform_point(*selected_node, command.point));
                const float distance = std::hypot(
                    mouse.x - candidate.x, mouse.y - candidate.y);
                if (distance < closest) {
                    closest = distance;
                    hit = index;
                }
            }
        }
        if (hit) {
            auto changed = *selected_node;
            if (fabric::project::remove_path_command(changed.shape, *hit)) {
                static_cast<void>(session.set_selected_vector_node(
                    canvas.selected_node, std::move(changed)));
            }
            canvas.selected_path_points.clear();
        }
    }
    if (hovered && selected_node != nullptr && !selected_node->locked &&
        canvas.tool == CanvasUiState::Tool::pen &&
        selected_node->shape.kind == fabric::project::VectorShapeKind::path &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) ||
         ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        auto changed = *selected_node;
        auto selected = canvas.selected_path_points;
        if (fabric::editor::remove_selected_path_points(changed.shape, selected)) {
            static_cast<void>(session.set_selected_vector_node(
                canvas.selected_node, std::move(changed)));
            canvas.selected_path_points.clear();
            canvas.path_command_index = 0U;
        }
    }
    if (!canvas.dragging && hovered && selected_node != nullptr &&
        !selected_node->locked && canvas.tool == CanvasUiState::Tool::pen &&
        selected_node->shape.kind == fabric::project::VectorShapeKind::path &&
        canvas.selected_path_points.size() == 1U &&
        canvas.path_command_index < selected_node->shape.path.size() &&
        selected_node->shape.path[canvas.path_command_index].kind ==
            fabric::project::VectorPathCommandKind::line &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
         io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F)) {
        canvas.dragging = true;
        canvas.drag_operation = CanvasUiState::DragOperation::pen_segment;
        canvas.drag_start_mouse = io.MousePos;
        canvas.drag_start_transform = selected_node->transform;
        canvas.drag_start_node = *selected_node;
    }
    const bool pen_motion_frame = canvas.tool == CanvasUiState::Tool::pen &&
        (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !pen_motion_frame) {
        canvas.dragging = false;
        canvas.drag_operation = CanvasUiState::DragOperation::none;
    }
    if (canvas.dragging && !canvas.drag_start_node.locked &&
        (io.MousePos.x != canvas.drag_start_mouse.x ||
         io.MousePos.y != canvas.drag_start_mouse.y)) {
        const bool path_drag =
            canvas.drag_operation == CanvasUiState::DragOperation::path_selection ||
            canvas.drag_operation == CanvasUiState::DragOperation::path_point ||
            canvas.drag_operation == CanvasUiState::DragOperation::pen_segment ||
            canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle1 ||
            canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle2;
        auto changed = path_drag ? canvas.drag_start_node : *selected_node;
        const auto& start = canvas.drag_start_transform;
        const auto start_mouse = to_world(canvas.drag_start_mouse);
        const auto current_mouse = to_world(io.MousePos);
        const auto world_to_local = [&](const fabric::core::Vec2 world) {
            const auto& transform = canvas.drag_start_node.transform;
            const float angle = -transform.rotation_degrees *
                std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const float x = world.x - transform.position.x - transform.pivot.x;
            const float y = world.y - transform.position.y - transform.pivot.y;
            return fabric::core::Vec2{
                (x * cosine + y * sine) /
                    std::max(std::abs(transform.scale.x), 0.0001F) +
                    transform.pivot.x,
                (-x * sine + y * cosine) /
                    std::max(std::abs(transform.scale.y), 0.0001F) +
                    transform.pivot.y};
        };
        if (canvas.drag_operation == CanvasUiState::DragOperation::path_selection) {
            changed = canvas.drag_start_node;
            const fabric::core::Vec2 delta{
                current_mouse.x - start_mouse.x,
                current_mouse.y - start_mouse.y};
            static_cast<void>(fabric::project::transform_path_points(
                changed.shape, canvas.selected_path_points, delta, 0.0F,
                {1.0F, 1.0F}));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::pen_segment &&
                   canvas.path_command_index < changed.shape.path.size()) {
            static_cast<void>(fabric::editor::create_bezier_segment(
                changed.shape, canvas.path_command_index,
                world_to_local(current_mouse)));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::path_point &&
                   canvas.path_command_index < changed.shape.path.size()) {
            changed.shape.path[canvas.path_command_index].point =
                world_to_local(current_mouse);
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle1 &&
                   canvas.path_command_index < changed.shape.path.size()) {
            static_cast<void>(fabric::editor::update_bezier_handle(
                changed.shape, canvas.path_command_index, true,
                world_to_local(current_mouse), canvas.bezier_handle_mode));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle2 &&
                   canvas.path_command_index < changed.shape.path.size()) {
            static_cast<void>(fabric::editor::update_bezier_handle(
                changed.shape, canvas.path_command_index, false,
                world_to_local(current_mouse), canvas.bezier_handle_mode));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::move) {
            changed.transform.position = {
                start.position.x + current_mouse.x - start_mouse.x,
                start.position.y + current_mouse.y - start_mouse.y};
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::rotate) {
            const auto start_vector = fabric::core::Vec2{
                start_mouse.x - (start.position.x + start.pivot.x),
                start_mouse.y - (start.position.y + start.pivot.y)};
            const auto current_vector = fabric::core::Vec2{
                current_mouse.x - (start.position.x + start.pivot.x),
                current_mouse.y - (start.position.y + start.pivot.y)};
            const float start_angle = std::atan2(start_vector.y, start_vector.x);
            const float current_angle =
                std::atan2(current_vector.y, current_vector.x);
            changed.transform.rotation_degrees =
                start.rotation_degrees +
                (current_angle - start_angle) * 180.0F /
                    std::numbers::pi_v<float>;
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::scale) {
            const auto local_from_world = [&](const fabric::core::Vec2 world) {
                const float angle = -start.rotation_degrees *
                    std::numbers::pi_v<float> / 180.0F;
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                const float x = world.x - start.position.x - start.pivot.x;
                const float y = world.y - start.position.y - start.pivot.y;
                return fabric::core::Vec2{
                    (x * cosine + y * sine) /
                        std::max(std::abs(start.scale.x), 0.0001F),
                    (-x * sine + y * cosine) /
                        std::max(std::abs(start.scale.y), 0.0001F)};
            };
            const auto start_local = local_from_world(start_mouse);
            const auto current_local = local_from_world(current_mouse);
            const float ratio_x = std::abs(start_local.x) > 0.0001F
                ? current_local.x / start_local.x
                : 1.0F;
            const float ratio_y = std::abs(start_local.y) > 0.0001F
                ? current_local.y / start_local.y
                : 1.0F;
            changed.transform.scale = {
                std::copysign(std::max(0.01F, std::abs(start.scale.x * ratio_x)),
                              start.scale.x),
                std::copysign(std::max(0.01F, std::abs(start.scale.y * ratio_y)),
                              start.scale.y)};
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::pivot) {
            const fabric::core::Vec2 next_pivot{
                current_mouse.x - start.position.x,
                current_mouse.y - start.position.y};
            const float angle = start.rotation_degrees *
                std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const auto apply_linear = [&](const fabric::core::Vec2 value) {
                return fabric::core::Vec2{
                    cosine * start.scale.x * value.x -
                        sine * start.scale.y * value.y,
                    sine * start.scale.x * value.x +
                        cosine * start.scale.y * value.y};
            };
            const auto pivot_delta = fabric::core::Vec2{
                start.pivot.x - next_pivot.x,
                start.pivot.y - next_pivot.y};
            const auto transformed_delta = apply_linear(pivot_delta);
            changed.transform.position = {
                start.position.x + pivot_delta.x - transformed_delta.x,
                start.position.y + pivot_delta.y - transformed_delta.y};
            changed.transform.pivot = next_pivot;
        }
        static_cast<void>(session.set_selected_vector_node(
            canvas.selected_node, std::move(changed)));
    }
    draw_list->PushClipRect(origin, {origin.x + available.x,
                                     origin.y + available.y}, true);
    for (int index = first_vertical; index <= last_vertical; ++index) {
        const auto line_start = to_screen({static_cast<float>(index) * grid_step,
                                           world_bottom});
        const auto line_end = to_screen({static_cast<float>(index) * grid_step,
                                         world_top});
        draw_list->AddLine(line_start, line_end,
                           index == 0 ? IM_COL32(135, 155, 165, 150)
                                      : IM_COL32(90, 105, 115, 70));
    }
    for (int index = first_horizontal; index <= last_horizontal; ++index) {
        const auto line_start = to_screen({world_left,
                                           static_cast<float>(index) * grid_step});
        const auto line_end = to_screen({world_right,
                                         static_cast<float>(index) * grid_step});
        draw_list->AddLine(line_start, line_end,
                           index == 0 ? IM_COL32(135, 155, 165, 150)
                                      : IM_COL32(90, 105, 115, 70));
    }
    draw_list->AddText({origin.x + 10.0F, origin.y + 10.0F},
                       IM_COL32(185, 200, 205, 220),
                       ("Grid: " + std::to_string(grid_step) + " world units").c_str());
    for (std::size_t node_index = 0;
         node_index < asset.native->nodes.size(); ++node_index) {
        const auto& node = asset.native->nodes[node_index];
        if (!node.visible) {
            continue;
        }
        const auto node_transform_point = [&](const fabric::core::Vec2 point) {
            return transform_point(node, point);
        };
        const auto& bounds = node.shape.bounds;
        std::vector<ImVec2> points;
        if (node.shape.kind == fabric::project::VectorShapeKind::ellipse) {
            constexpr int segments = 64;
            points.reserve(segments);
            const fabric::core::Vec2 ellipse_center{
                bounds.origin.x + bounds.size.x * 0.5F,
                bounds.origin.y + bounds.size.y * 0.5F};
            for (int segment = 0; segment < segments; ++segment) {
                const float angle = 2.0F * std::numbers::pi_v<float> *
                    static_cast<float>(segment) / static_cast<float>(segments);
                points.push_back(to_screen(node_transform_point({
                    ellipse_center.x + std::cos(angle) * bounds.size.x * 0.5F,
                    ellipse_center.y + std::sin(angle) * bounds.size.y * 0.5F})));
            }
        } else if (node.shape.kind == fabric::project::VectorShapeKind::line &&
                   node.shape.points.size() == 2U) {
            points = {to_screen(node_transform_point(node.shape.points[0])),
                      to_screen(node_transform_point(node.shape.points[1]))};
        } else if (node.shape.kind == fabric::project::VectorShapeKind::path) {
            fabric::core::Vec2 current{};
            fabric::core::Vec2 first{};
            bool has_current = false;
            for (const auto& command : node.shape.path) {
                if (command.kind == fabric::project::VectorPathCommandKind::move) {
                    current = command.point;
                    first = current;
                    has_current = true;
                    points.push_back(to_screen(node_transform_point(current)));
                } else if (command.kind == fabric::project::VectorPathCommandKind::line &&
                           has_current) {
                    current = command.point;
                    points.push_back(to_screen(node_transform_point(current)));
                } else if (command.kind == fabric::project::VectorPathCommandKind::cubic &&
                           has_current) {
                    const auto start = current;
                    for (int segment = 1; segment <= 12; ++segment) {
                        const float t = static_cast<float>(segment) / 12.0F;
                        const float inverse = 1.0F - t;
                        current = {
                            inverse * inverse * inverse * start.x +
                                3.0F * inverse * inverse * t * command.control1.x +
                                3.0F * inverse * t * t * command.control2.x +
                                t * t * t * command.point.x,
                            inverse * inverse * inverse * start.y +
                                3.0F * inverse * inverse * t * command.control1.y +
                                3.0F * inverse * t * t * command.control2.y +
                                t * t * t * command.point.y};
                        points.push_back(to_screen(node_transform_point(current)));
                    }
                } else if (command.kind == fabric::project::VectorPathCommandKind::close &&
                           has_current) {
                    current = first;
                    points.push_back(to_screen(node_transform_point(current)));
                }
            }
        } else {
            points = {
                to_screen(node_transform_point(bounds.origin)),
                to_screen(node_transform_point({bounds.origin.x + bounds.size.x,
                                           bounds.origin.y})),
                to_screen(node_transform_point({bounds.origin.x + bounds.size.x,
                                           bounds.origin.y + bounds.size.y})),
                to_screen(node_transform_point({bounds.origin.x,
                                           bounds.origin.y + bounds.size.y})),
            };
        }
        fabric::core::Color fill{0.35F, 0.55F, 0.58F, 1.0F};
        if (node.fill.kind == fabric::project::VectorFillKind::solid &&
            node.fill.color) {
            fill = *node.fill.color;
        } else if (node.fill.kind == fabric::project::VectorFillKind::none) {
            fill.alpha = 0.0F;
        } else if (node.fill.kind == fabric::project::VectorFillKind::image) {
            fill = {0.89F, 0.68F, 0.34F, 0.8F};
        }
        if (fill.alpha > 0.0F &&
            node.fill.kind != fabric::project::VectorFillKind::image &&
            node.shape.kind != fabric::project::VectorShapeKind::line) {
            draw_list->AddConvexPolyFilled(points.data(),
                                           static_cast<int>(points.size()),
                                           color_to_u32(fill));
        }
        const bool selected = node_index == canvas.selected_node;
        const auto stroke_color = selected
            ? IM_COL32(236, 180, 75, 255)
            : (node.stroke.has_value()
                   ? color_to_u32(node.stroke->color)
                   : IM_COL32(225, 230, 235, 255));
        const float stroke_width = node.stroke.has_value()
            ? std::max(1.0F, node.stroke->width * pixels_per_unit)
            : (selected ? 2.5F : 1.5F);
        const bool closed_path =
            node.shape.kind == fabric::project::VectorShapeKind::path &&
            !node.shape.path.empty() &&
            node.shape.path.back().kind ==
                fabric::project::VectorPathCommandKind::close;
        const auto stroke_flags =
            node.shape.kind == fabric::project::VectorShapeKind::line ||
                    (node.shape.kind == fabric::project::VectorShapeKind::path &&
                     !closed_path)
                ? ImDrawFlags_None
                : ImDrawFlags_Closed;
        if (node.stroke && node.stroke->image) {
            // The native editor preview does not own an ImGui texture atlas.
            // Keep image strokes visible with a deterministic woven fallback
            // instead of dropping the stroke entirely.
            draw_list->AddPolyline(points.data(), static_cast<int>(points.size()),
                                   stroke_color, stroke_flags, stroke_width);
            const ImU32 highlight = IM_COL32(255, 255, 255, 190);
            for (std::size_t segment = 1; segment < points.size(); ++segment) {
                const auto start = points[segment - 1];
                const auto end = points[segment];
                const float length = std::hypot(end.x - start.x, end.y - start.y);
                if (length < 1.0F) continue;
                const int dash_count = std::max(1, static_cast<int>(length / 18.0F));
                for (int dash = 0; dash < dash_count; ++dash) {
                    if ((dash + static_cast<int>(segment)) % 2 != 0) continue;
                    const float begin = static_cast<float>(dash) /
                        static_cast<float>(dash_count);
                    const float finish = std::min(
                        1.0F, begin + 0.42F / static_cast<float>(dash_count));
                    draw_list->AddLine(
                        {start.x + (end.x - start.x) * begin,
                         start.y + (end.y - start.y) * begin},
                        {start.x + (end.x - start.x) * finish,
                         start.y + (end.y - start.y) * finish},
                        highlight, std::max(1.0F, stroke_width * 0.28F));
                }
            }
        } else {
            draw_list->AddPolyline(points.data(), static_cast<int>(points.size()),
                                   stroke_color, stroke_flags, stroke_width);
        }
    }
    if (selected_node != nullptr && !selected_node->locked) {
        if (canvas.tool == CanvasUiState::Tool::rotate) {
            draw_list->AddLine(rotate_anchor, rotate_handle,
                               IM_COL32(236, 180, 75, 220), 1.5F);
            draw_list->AddCircleFilled(rotate_handle, 6.0F,
                                       IM_COL32(236, 180, 75, 255));
        } else if (canvas.tool == CanvasUiState::Tool::scale) {
            draw_list->AddRectFilled(
                {scale_handle.x - 6.0F, scale_handle.y - 6.0F},
                {scale_handle.x + 6.0F, scale_handle.y + 6.0F},
                IM_COL32(98, 180, 240, 255));
        } else if (canvas.tool == CanvasUiState::Tool::pivot) {
            draw_list->AddLine({pivot_handle.x - 7.0F, pivot_handle.y},
                               {pivot_handle.x + 7.0F, pivot_handle.y},
                               IM_COL32(180, 110, 235, 255), 2.0F);
            draw_list->AddLine({pivot_handle.x, pivot_handle.y - 7.0F},
                               {pivot_handle.x, pivot_handle.y + 7.0F},
                               IM_COL32(180, 110, 235, 255), 2.0F);
        } else if (selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
            std::optional<std::size_t> hovered_path_point;
            float hovered_path_distance = 10.0F;
            for (std::size_t index = 0;
                 index < selected_node->shape.path.size(); ++index) {
                const auto& command = selected_node->shape.path[index];
                if (command.kind != fabric::project::VectorPathCommandKind::move &&
                    command.kind != fabric::project::VectorPathCommandKind::line &&
                    command.kind != fabric::project::VectorPathCommandKind::cubic)
                    continue;
                const auto point = to_screen(
                    transform_point(*selected_node, command.point));
                const float distance = std::hypot(
                    io.MousePos.x - point.x, io.MousePos.y - point.y);
                if (hovered && distance <= hovered_path_distance) {
                    hovered_path_distance = distance;
                    hovered_path_point = index;
                }
            }
            for (std::size_t index = 0; index < selected_node->shape.path.size(); ++index) {
                const auto& command = selected_node->shape.path[index];
                if (command.kind == fabric::project::VectorPathCommandKind::move ||
                    command.kind == fabric::project::VectorPathCommandKind::line ||
                    command.kind == fabric::project::VectorPathCommandKind::cubic) {
                    const auto point_selected = std::ranges::find(
                        canvas.selected_path_points, index) !=
                        canvas.selected_path_points.end();
                    const auto point = to_screen(
                        transform_point(*selected_node, command.point));
                    const auto point_color = point_selected
                        ? IM_COL32(100, 210, 255, 255)
                        : IM_COL32(236, 180, 75, 255);
                    draw_list->AddCircleFilled(point, 5.0F, point_color);
                    if (hovered_path_point && *hovered_path_point == index)
                        draw_list->AddCircle(point, 9.0F,
                                             IM_COL32(255, 255, 255, 255), 16,
                                             2.0F);
                }
                if (command.kind == fabric::project::VectorPathCommandKind::cubic) {
                    const auto anchor = to_screen(
                        transform_point(*selected_node, command.point));
                    const auto handle1 = to_screen(
                        transform_point(*selected_node, command.control1));
                    const auto handle2 = to_screen(
                        transform_point(*selected_node, command.control2));
                    draw_list->AddLine(anchor, handle1,
                                       IM_COL32(180, 110, 235, 210), 1.0F);
                    draw_list->AddLine(anchor, handle2,
                                       IM_COL32(180, 110, 235, 210), 1.0F);
                    draw_list->AddCircleFilled(handle1, 4.0F,
                                               IM_COL32(180, 110, 235, 255));
                    draw_list->AddCircleFilled(handle2, 4.0F,
                                               IM_COL32(180, 110, 235, 255));
                }
            }
            draw_list->AddText(
                {origin.x + 14.0F, origin.y + available.y - 34.0F},
                IM_COL32(238, 238, 238, 230),
                "Corners: amber | selected: cyan | handles: purple");
        }
    }
    draw_list->PopClipRect();
    if (hovered) {
        ImGui::SetTooltip("Click a shape to select it. Move drags the selected shape; on a path, drag anchors or Bézier handles. Rotate, Scale and Pivot drag only their active handle. Middle drag: pan | Wheel: zoom %.0f%%",
                          canvas.zoom * 100.0F);
    }
}

} // namespace fabric::asset_studio
