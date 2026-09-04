#include "preview_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabric::asset_studio {

void draw_packet_preview_canvas(
    CanvasUiState& canvas, const ImVec2 available, const std::string_view label,
    fabric::editor::ProjectSession* editable_session,
    EntityTransformCommit transform_commit) {
    ImGui::InvisibleButton("Entity canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 origin = ImGui::GetItemRectMin();
    canvas.xpbd_overlay_visible = false;
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
    if (canvas.grid_visible) {
        draw_list->AddLine(to_screen({0.0F, bounds.origin.y}),
                           to_screen({0.0F, bounds.origin.y + bounds.size.y}),
                           IM_COL32(100, 110, 125, 100));
        draw_list->AddLine(to_screen({bounds.origin.x, 0.0F}),
                           to_screen({bounds.origin.x + bounds.size.x, 0.0F}),
                           IM_COL32(100, 110, 125, 100));
    }
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
        const auto& entity = *editable_session->selected_entity();
        const auto display_transform = [&](const std::size_t index)
            -> const fabric::core::Transform& {
            return canvas.entity_display_transforms.size() == entity.nodes.size()
                ? canvas.entity_display_transforms[index]
                : entity.nodes[index].transform;
        };
        if (canvas.selected_entity_id != entity.document.id.value) {
            canvas.selected_entity_id = entity.document.id.value;
            canvas.selected_node = 0U;
            canvas.selected_entity_nodes = {0U};
        }
        std::erase_if(canvas.selected_entity_nodes, [&](const auto index) {
            return index >= entity.nodes.size();
        });
        if (std::ranges::find(canvas.selected_entity_nodes,
                              canvas.selected_node) ==
            canvas.selected_entity_nodes.end())
            canvas.selected_entity_nodes.push_back(canvas.selected_node);

        std::optional<std::size_t> clicked_node;
        float clicked_distance = 15.0F;
        const bool canvas_hovered = ImGui::IsMouseHoveringRect(
            origin, {origin.x + available.x, origin.y + available.y});
        if (canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto primary = to_screen(
                display_transform(canvas.selected_node).position);
            if (std::hypot(ImGui::GetIO().MousePos.x - primary.x,
                           ImGui::GetIO().MousePos.y - primary.y) <= 14.0F) {
                clicked_node = canvas.selected_node;
                clicked_distance = -1.0F;
            }
        }
        for (std::size_t index = 0; index < entity.nodes.size(); ++index) {
            const auto gizmo = to_screen(display_transform(index).position);
            const bool selected = std::ranges::find(
                canvas.selected_entity_nodes, index) !=
                canvas.selected_entity_nodes.end();
            const auto color = selected
                ? IM_COL32(100, 210, 255, 255)
                : IM_COL32(145, 155, 170, 210);
            draw_list->AddLine({gizmo.x - 10.0F, gizmo.y},
                               {gizmo.x + 10.0F, gizmo.y}, color,
                               selected ? 2.0F : 1.0F);
            draw_list->AddLine({gizmo.x, gizmo.y - 10.0F},
                               {gizmo.x, gizmo.y + 10.0F}, color,
                               selected ? 2.0F : 1.0F);
            draw_list->AddCircleFilled(gizmo, selected ? 5.0F : 4.0F, color);
            if (index == canvas.selected_node)
                canvas.entity_gizmo_screen = gizmo;
            const float distance = std::hypot(
                ImGui::GetIO().MousePos.x - gizmo.x,
                ImGui::GetIO().MousePos.y - gizmo.y);
            if (canvas_hovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                distance <= 14.0F &&
                clicked_distance >= 0.0F &&
                (distance < clicked_distance ||
                 (distance == clicked_distance &&
                  index == canvas.selected_node))) {
                clicked_node = index;
                clicked_distance = distance;
            }
        }
        if (clicked_node) {
            const bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
            const auto found = std::ranges::find(canvas.selected_entity_nodes,
                                                 *clicked_node);
            if (additive && found != canvas.selected_entity_nodes.end() &&
                canvas.selected_entity_nodes.size() > 1U) {
                canvas.selected_entity_nodes.erase(found);
                canvas.selected_node = canvas.selected_entity_nodes.back();
            } else {
                if (!additive && found == canvas.selected_entity_nodes.end())
                    canvas.selected_entity_nodes.clear();
                if (std::ranges::find(canvas.selected_entity_nodes,
                                      *clicked_node) ==
                    canvas.selected_entity_nodes.end())
                    canvas.selected_entity_nodes.push_back(*clicked_node);
                canvas.selected_node = *clicked_node;
            }
            const auto& node = entity.nodes[canvas.selected_node];
            if (!node.locked) {
                canvas.entity_gizmo_dragging = true;
                canvas.entity_gizmo_start_mouse = ImGui::GetIO().MousePos;
                canvas.entity_gizmo_start_transform =
                    display_transform(canvas.selected_node);
                canvas.entity_gizmo_start_transforms.clear();
                for (const auto index : canvas.selected_entity_nodes)
                    if (!entity.nodes[index].locked)
                        canvas.entity_gizmo_start_transforms.push_back(
                            {index, display_transform(index)});
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            canvas.entity_gizmo_dragging = false;
        if (canvas.entity_gizmo_dragging &&
            !canvas.entity_gizmo_start_transforms.empty()) {
            const ImVec2 delta{
                ImGui::GetIO().MousePos.x - canvas.entity_gizmo_start_mouse.x,
                ImGui::GetIO().MousePos.y - canvas.entity_gizmo_start_mouse.y};
            const auto scale = std::max(0.01F, pixels_per_unit);
            std::vector<std::pair<std::size_t, fabric::core::Transform>>
                changed_transforms;
            changed_transforms.reserve(
                canvas.entity_gizmo_start_transforms.size());
            for (const auto& [index, transform] :
                 canvas.entity_gizmo_start_transforms) {
                auto changed = transform;
                changed.position.x += delta.x / scale;
                changed.position.y -= delta.y / scale;
                changed_transforms.emplace_back(index, changed);
            }
            if (transform_commit &&
                transform_commit(std::move(changed_transforms)))
                ImGui::SetTooltip("Move node · %.2f, %.2f",
                                  delta.x / scale, -delta.y / scale);
        }
    }
}

} // namespace fabric::asset_studio
