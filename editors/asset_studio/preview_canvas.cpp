#include "preview_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabric::asset_studio {

void draw_packet_preview_canvas(
    CanvasUiState& canvas, const ImVec2 available, const std::string_view label,
    fabric::editor::ProjectSession* editable_session) {
    ImGui::InvisibleButton("Entity canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 origin = ImGui::GetItemRectMin();
    canvas.native_canvas = true;
    canvas.native_origin = origin;
    canvas.native_size = available;
    const auto bounds = canvas.entity_world_bounds;
    const ImVec2 center{origin.x + available.x * 0.5F,
                        origin.y + available.y * 0.5F};
    const float fit = std::min(
        (available.x - 80.0F) / std::max(bounds.size.x, 1.0F),
        (available.y - 80.0F) / std::max(bounds.size.y, 1.0F));
    const float pixels_per_unit = std::max(0.01F, fit * canvas.zoom);
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
        canvas.zoom = std::clamp(
            canvas.zoom * (ImGui::GetIO().MouseWheel > 0.0F ? 1.15F : 1.0F / 1.15F),
            0.1F, 20.0F);
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        canvas.pan.x += delta.x;
        canvas.pan.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{center.x + canvas.pan.x + point.x * pixels_per_unit,
                      center.y + canvas.pan.y - point.y * pixels_per_unit};
    };
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(to_screen({0.0F, bounds.origin.y}),
                       to_screen({0.0F, bounds.origin.y + bounds.size.y}),
                       IM_COL32(100, 110, 125, 100));
    draw_list->AddLine(to_screen({bounds.origin.x, 0.0F}),
                       to_screen({bounds.origin.x + bounds.size.x, 0.0F}),
                       IM_COL32(100, 110, 125, 100));
    if (editable_session && editable_session->selected_entity() &&
        editable_session->selected_entity()->xpbd) {
        canvas.xpbd_overlay_visible = true;
        const auto& xpbd = *editable_session->selected_entity()->xpbd;
        const auto particle_position = [&](const std::size_t index)
            -> std::optional<ImVec2> {
            if (index >= xpbd.particles.size()) return std::nullopt;
            return to_screen(xpbd.particles[index].position);
        };
        for (const auto& constraint : xpbd.distance_constraints) {
            const auto first = particle_position(constraint.first);
            const auto second = particle_position(constraint.second);
            if (first && second)
                draw_list->AddLine(*first, *second,
                                   IM_COL32(64, 205, 230, 220), 2.0F);
        }
        for (const auto& constraint : xpbd.bending_constraints) {
            const auto first = particle_position(constraint.first);
            const auto middle = particle_position(constraint.middle);
            const auto third = particle_position(constraint.third);
            if (first && middle)
                draw_list->AddLine(*first, *middle,
                                   IM_COL32(190, 120, 240, 190), 2.0F);
            if (middle && third)
                draw_list->AddLine(*middle, *third,
                                   IM_COL32(190, 120, 240, 190), 2.0F);
        }
        for (const auto& constraint : xpbd.area_constraints) {
            const auto first = particle_position(constraint.first);
            const auto second = particle_position(constraint.second);
            const auto third = particle_position(constraint.third);
            if (first && second && third) {
                const ImVec2 points[]{*first, *second, *third};
                draw_list->AddPolyline(points, 3, IM_COL32(245, 180, 70, 190),
                                       ImDrawFlags_Closed, 2.0F);
            }
        }
        for (const auto& constraint : xpbd.pin_constraints) {
            const auto particle = particle_position(constraint.particle);
            const auto target = to_screen(constraint.target);
            if (particle)
                draw_list->AddLine(*particle, target,
                                   IM_COL32(245, 95, 135, 210), 2.0F);
            draw_list->AddRect({target.x - 4.0F, target.y - 4.0F},
                               {target.x + 4.0F, target.y + 4.0F},
                               IM_COL32(245, 95, 135, 255), 0.0F, 0, 2.0F);
        }
        for (const auto& constraint : xpbd.collision_constraints) {
            const auto particle = particle_position(constraint.particle);
            const float normal_length = std::hypot(constraint.normal.x,
                                                   constraint.normal.y);
            if (particle && normal_length > 0.0001F) {
                const auto position = xpbd.particles[constraint.particle].position;
                const fabric::core::Vec2 tip{
                    position.x + constraint.normal.x / normal_length * 0.5F,
                    position.y + constraint.normal.y / normal_length * 0.5F};
                draw_list->AddLine(*particle, to_screen(tip),
                                   IM_COL32(250, 90, 70, 230), 2.0F);
            }
        }
        for (std::size_t index = 0; index < xpbd.particles.size(); ++index) {
            const auto point = to_screen(xpbd.particles[index].position);
            const bool fixed = xpbd.particles[index].inverse_mass == 0.0F;
            draw_list->AddCircleFilled(
                point, fixed ? 6.0F : 5.0F,
                fixed ? IM_COL32(245, 95, 135, 255)
                      : IM_COL32(80, 220, 165, 255));
            draw_list->AddText({point.x + 7.0F, point.y - 8.0F},
                               IM_COL32(225, 230, 238, 230),
                               std::to_string(index).c_str());
        }
    }
    const float world_half_width = available.x / (2.0F * pixels_per_unit);
    const float world_half_height = available.y / (2.0F * pixels_per_unit);
    canvas.native_world_bounds = {
        .origin = {-world_half_width - canvas.pan.x / pixels_per_unit,
                   -world_half_height + canvas.pan.y / pixels_per_unit},
        .size = {2.0F * world_half_width, 2.0F * world_half_height}};
    ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 8.0F});
    ImGui::TextDisabled("%s · %.0f%%", std::string(label).c_str(),
                        canvas.zoom * 100.0F);
    if (editable_session && editable_session->selected_entity() &&
        canvas.selected_node < editable_session->selected_entity()->nodes.size()) {
        const auto& node = editable_session->selected_entity()->nodes[
            canvas.selected_node];
        const auto gizmo = to_screen(node.transform.position);
        canvas.entity_gizmo_screen = gizmo;
        draw_list->AddLine({gizmo.x - 12.0F, gizmo.y},
                           {gizmo.x + 12.0F, gizmo.y},
                           IM_COL32(100, 210, 255, 230), 2.0F);
        draw_list->AddLine({gizmo.x, gizmo.y - 12.0F},
                           {gizmo.x, gizmo.y + 12.0F},
                           IM_COL32(100, 210, 255, 230), 2.0F);
        draw_list->AddCircleFilled(gizmo, 5.0F,
                                   IM_COL32(100, 210, 255, 255));
        const bool canvas_hovered = ImGui::IsMouseHoveringRect(
            origin, {origin.x + available.x, origin.y + available.y});
        if (canvas_hovered && !node.locked &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            std::hypot(ImGui::GetIO().MousePos.x - gizmo.x,
                       ImGui::GetIO().MousePos.y - gizmo.y) <= 14.0F) {
            canvas.entity_gizmo_dragging = true;
            canvas.entity_gizmo_start_mouse = ImGui::GetIO().MousePos;
            canvas.entity_gizmo_start_transform = node.transform;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            canvas.entity_gizmo_dragging = false;
        if (canvas.entity_gizmo_dragging && !node.locked) {
            const ImVec2 delta{
                ImGui::GetIO().MousePos.x - canvas.entity_gizmo_start_mouse.x,
                ImGui::GetIO().MousePos.y - canvas.entity_gizmo_start_mouse.y};
            const auto scale = std::max(0.01F, pixels_per_unit);
            auto changed = node;
            changed.transform = canvas.entity_gizmo_start_transform;
            changed.transform.position.x += delta.x / scale;
            changed.transform.position.y -= delta.y / scale;
            if (editable_session->set_selected_entity_node(
                    canvas.selected_node, std::move(changed)))
                ImGui::SetTooltip("Move node · %.2f, %.2f",
                                  delta.x / scale, -delta.y / scale);
        }
    }
}

} // namespace fabric::asset_studio
