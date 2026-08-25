#include "fabric/editor/map_session.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>

#include <filesystem>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <optional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

void draw_errors(const fabric::editor::MapSession& session) {
    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s: %s", error.field.c_str(), error.message.c_str());
        ImGui::PopStyleColor();
    }
}

std::optional<fabric::project::MapPropertyValue> parse_override_value(
    const int kind, const std::string_view text) {
    try {
        switch (kind) {
        case 0:
            if (text == "true") return true;
            if (text == "false") return false;
            return std::nullopt;
        case 1: {
            std::size_t consumed = 0;
            const auto value = std::stoll(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return static_cast<std::int64_t>(value);
        }
        case 2: {
            std::size_t consumed = 0;
            const auto value = std::stof(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return value;
        }
        case 3:
            return std::string{text};
        case 4: {
            const auto separator = text.find(',');
            if (separator == std::string_view::npos) return std::nullopt;
            std::size_t consumed_x = 0;
            std::size_t consumed_y = 0;
            const auto x = std::stof(std::string{text.substr(0, separator)}, &consumed_x);
            const auto y = std::stof(std::string{text.substr(separator + 1)}, &consumed_y);
            if (consumed_x != separator || consumed_y != text.size() - separator - 1U)
                return std::nullopt;
            return fabric::core::Vec2{x, y};
        }
        case 5:
            if (text.empty()) return std::nullopt;
            return fabric::project::ResourceReference{{.value = std::string{text}}, "resource"};
        default:
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string property_value_text(const fabric::project::MapPropertyValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<Value, std::int64_t>) return std::to_string(item);
        else if constexpr (std::is_same_v<Value, float>) return std::to_string(item);
        else if constexpr (std::is_same_v<Value, std::string>) return item;
        else if constexpr (std::is_same_v<Value, fabric::core::Vec2>)
            return std::to_string(item.x) + "," + std::to_string(item.y);
        else return item.id.value;
    }, value);
}

std::string collision_shape_text(const fabric::project::CollisionShape& shape) {
    std::string result;
    switch (shape.kind) {
    case fabric::project::CollisionShapeKind::circle: result = "circle"; break;
    case fabric::project::CollisionShapeKind::capsule: result = "capsule"; break;
    case fabric::project::CollisionShapeKind::polygon: result = "polygon"; break;
    case fabric::project::CollisionShapeKind::chain: result = "chain"; break;
    }
    result += " @ " + std::to_string(shape.center.x) + "," +
              std::to_string(shape.center.y);
    if (shape.kind == fabric::project::CollisionShapeKind::circle ||
        shape.kind == fabric::project::CollisionShapeKind::capsule)
        result += " r=" + std::to_string(shape.radius);
    if (shape.kind == fabric::project::CollisionShapeKind::capsule)
        result += " l=" + std::to_string(shape.length);
    result += shape.sensor ? " [sensor]" : " [solid]";
    return result;
}

bool layer_visible(const fabric::project::MapDocument& map, const std::string& layer_id) {
    const auto layer = std::find_if(map.layers.begin(), map.layers.end(),
                                    [&](const auto& candidate) {
                                        return candidate.id == layer_id;
                                    });
    return layer == map.layers.end() || layer->visible;
}

struct TransformEditorState {
    std::string instance_id;
    fabric::core::Transform value{};
};

enum class CanvasGizmoMode { translate, rotate, scale };

struct CanvasGizmoState {
    bool active{};
    CanvasGizmoMode mode{CanvasGizmoMode::translate};
    std::string instance_id;
    ImVec2 start_mouse{};
    fabric::core::Transform start_transform{};
    fabric::core::Transform preview_transform{};
    std::vector<std::string> group_ids;
    fabric::core::Vec2 preview_delta{};
};

struct CollisionPointGizmoState {
    bool active{};
    int collision_index{-1};
    std::size_t point_index{};
    fabric::core::Vec2 preview_point{};
};

struct SelectionBoxState {
    bool active{};
    bool append{};
    ImVec2 start_mouse{};
    ImVec2 current_mouse{};
};

void draw_transform_editor(fabric::editor::MapSession& session,
                           const std::vector<std::string>& selected_instances,
                           TransformEditorState& state,
                           std::string& status) {
    if (selected_instances.size() != 1U || !session.map()) return;
    const auto& map = *session.map();
    const auto found = std::find_if(map.instances.begin(), map.instances.end(),
                                    [&](const auto& instance) {
                                        return instance.id == selected_instances.front();
                                    });
    if (found == map.instances.end()) return;
    const auto instance_id = found->id;
    if (state.instance_id != instance_id) {
        state.instance_id = instance_id;
        state.value = found->transform;
    }
    ImGui::SeparatorText("Transform gizmo");
    ImGui::TextDisabled("Selected: %s", instance_id.c_str());
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Position", &state.value.position.x, 0.1F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Position transformed" : "Transform rejected (layer locked or invalid)";
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat("Rotation (degrees)", &state.value.rotation_degrees, 1.0F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Rotation transformed" : "Transform rejected (layer locked or invalid)";
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Scale", &state.value.scale.x, 0.01F, -32.0F, 32.0F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Scale transformed" : "Transform rejected (layer locked or invalid)";
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Pivot", &state.value.pivot.x, 0.01F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Pivot transformed" : "Transform rejected (layer locked or invalid)";
    }
}

void draw_map_canvas(fabric::editor::MapSession& session,
                     std::vector<std::string>& selected_instances,
                     ImVec2& pan,
                     float& zoom,
                     CanvasGizmoState& gizmo,
                     int selected_collision_index,
                     CollisionPointGizmoState& point_gizmo,
                     int selected_trigger_index,
                     const std::string& active_layer_id,
                     SelectionBoxState& selection_box,
                     bool& placement_mode,
                     std::string& placement_id,
                     std::string& placement_resource_id,
                     int& placement_kind,
                     fabric::editor::MapSnapSettings& snapping,
                     std::string& status) {
    if (!session.map()) return;
    const auto& map = *session.map();
    ImGui::SeparatorText("Canvas");
    ImGui::TextDisabled("Active layer: %s", active_layer_id.c_str());
    ImGui::Checkbox("Snap translation", &snapping.enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0F);
    ImGui::DragFloat("Grid size", &snapping.grid_size, 0.1F, 0.01F, 1024.0F);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0F);
    ImGui::DragFloat2("Origin", &snapping.origin.x, 0.1F);
    const ImVec2 canvas_size{ImGui::GetContentRegionAvail().x, 380.0F};
    auto frame_instances = [&](const bool selected_only) {
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        bool found = false;
        for (const auto& instance : map.instances) {
            if (!layer_visible(map, instance.layer_id)) continue;
            if (selected_only && std::find(selected_instances.begin(), selected_instances.end(),
                                           instance.id) == selected_instances.end()) continue;
            min_x = std::min(min_x, instance.transform.position.x);
            min_y = std::min(min_y, instance.transform.position.y);
            max_x = std::max(max_x, instance.transform.position.x);
            max_y = std::max(max_y, instance.transform.position.y);
            found = true;
        }
        if (!found) return false;
        const auto width = std::max(max_x - min_x, 1.0F);
        const auto height = std::max(max_y - min_y, 1.0F);
        zoom = std::clamp(std::min((canvas_size.x - 48.0F) / width,
                                   (canvas_size.y - 48.0F) / height), 0.1F, 32.0F);
        const auto center_x = (min_x + max_x) * 0.5F;
        const auto center_y = (min_y + max_y) * 0.5F;
        pan = {-center_x * zoom, center_y * zoom};
        return true;
    };
    ImGui::BeginDisabled(selected_instances.empty());
    if (ImGui::Button("Frame selection"))
        status = frame_instances(true) ? "Selection framed" : "No visible selected instance";
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Frame all"))
        status = frame_instances(false) ? "Map framed" : "No visible instance to frame";
    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map-canvas", canvas_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 canvas_center{canvas_pos.x + canvas_size.x * 0.5F,
                               canvas_pos.y + canvas_size.y * 0.5F};
    auto world_to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{canvas_center.x + pan.x + point.x * zoom,
                      canvas_center.y + pan.y - point.y * zoom};
    };
    auto screen_to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{(point.x - canvas_center.x - pan.x) / zoom,
                                  -(point.y - canvas_center.y - pan.y) / zoom};
    };
    auto transform_for = [&](const fabric::project::MapInstance& instance) {
        if (!gizmo.active) return instance.transform;
        if (gizmo.mode == CanvasGizmoMode::translate &&
            std::find(gizmo.group_ids.begin(), gizmo.group_ids.end(), instance.id) !=
                gizmo.group_ids.end()) {
            auto result = instance.transform;
            result.position.x += gizmo.preview_delta.x;
            result.position.y += gizmo.preview_delta.y;
            return result;
        }
        return gizmo.instance_id == instance.id ? gizmo.preview_transform : instance.transform;
    };
    auto collision_point_for = [&](const std::size_t collision_index,
                                   const std::size_t point_index,
                                   const fabric::core::Vec2 point) {
        return point_gizmo.active &&
                       point_gizmo.collision_index == static_cast<int>(collision_index) &&
                       point_gizmo.point_index == point_index
                   ? point_gizmo.preview_point : point;
    };

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_pos,
                        {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                        IM_COL32(22, 25, 31, 255));
    const float pixels_per_grid = zoom;
    float grid_step = 1.0F;
    while (grid_step * pixels_per_grid < 14.0F) grid_step *= 2.0F;
    while (grid_step * pixels_per_grid > 70.0F) grid_step *= 0.5F;
    const auto top_left = screen_to_world(canvas_pos);
    const auto bottom_right = screen_to_world(
        {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y});
    const auto first_x = static_cast<int>(std::floor(top_left.x / grid_step)) - 1;
    const auto last_x = static_cast<int>(std::ceil(bottom_right.x / grid_step)) + 1;
    const auto first_y = static_cast<int>(std::floor(bottom_right.y / grid_step)) - 1;
    const auto last_y = static_cast<int>(std::ceil(top_left.y / grid_step)) + 1;
    for (int x = first_x; x <= last_x; ++x) {
        const auto line = world_to_screen({static_cast<float>(x) * grid_step, 0.0F});
        draw->AddLine({line.x, canvas_pos.y},
                      {line.x, canvas_pos.y + canvas_size.y},
                      x == 0 ? IM_COL32(105, 115, 130, 220) : IM_COL32(48, 54, 64, 180));
    }
    for (int y = first_y; y <= last_y; ++y) {
        const auto line = world_to_screen({0.0F, static_cast<float>(y) * grid_step});
        draw->AddLine({canvas_pos.x, line.y},
                      {canvas_pos.x + canvas_size.x, line.y},
                      y == 0 ? IM_COL32(105, 115, 130, 220) : IM_COL32(48, 54, 64, 180));
    }
    for (std::size_t collision_index = 0; collision_index < map.collisions.size();
         ++collision_index) {
        const auto& collision = map.collisions[collision_index];
        if (!layer_visible(map, collision.layer_id)) continue;
        const auto center = world_to_screen(collision.center);
        const auto color = collision.sensor ? IM_COL32(240, 190, 80, 210)
                                            : IM_COL32(220, 90, 90, 210);
        if (collision.kind == fabric::project::CollisionShapeKind::circle) {
            draw->AddCircle(center, collision.radius * zoom, color, 32, 2.0F);
        } else if (collision.kind == fabric::project::CollisionShapeKind::capsule) {
            const float half_length = collision.length * zoom * 0.5F;
            draw->AddLine({center.x - half_length, center.y},
                          {center.x + half_length, center.y}, color, 2.0F);
            draw->AddCircle({center.x - half_length, center.y}, collision.radius * zoom,
                            color, 24, 2.0F);
            draw->AddCircle({center.x + half_length, center.y}, collision.radius * zoom,
                            color, 24, 2.0F);
        } else if (collision.points.size() > 1U) {
            for (std::size_t point = 1; point < collision.points.size(); ++point)
                draw->AddLine(world_to_screen(collision_point_for(
                                  collision_index, point - 1, collision.points[point - 1])),
                              world_to_screen(collision_point_for(
                                  collision_index, point, collision.points[point])), color, 2.0F);
            if (collision.kind == fabric::project::CollisionShapeKind::polygon)
                draw->AddLine(world_to_screen(collision_point_for(
                                  collision_index, collision.points.size() - 1,
                                  collision.points.back())),
                              world_to_screen(collision_point_for(
                                  collision_index, 0, collision.points.front())), color, 2.0F);
        }
    }
    if (selected_collision_index >= 0 &&
        static_cast<std::size_t>(selected_collision_index) < map.collisions.size()) {
        const auto& collision = map.collisions[static_cast<std::size_t>(selected_collision_index)];
        if (collision.kind == fabric::project::CollisionShapeKind::polygon ||
            collision.kind == fabric::project::CollisionShapeKind::chain) {
            for (std::size_t point = 0; point < collision.points.size(); ++point) {
                const auto position = world_to_screen(collision_point_for(
                    static_cast<std::size_t>(selected_collision_index), point,
                    collision.points[point]));
                const auto active = point_gizmo.active &&
                    point_gizmo.collision_index == selected_collision_index &&
                    point_gizmo.point_index == point;
                draw->AddCircleFilled(position, active ? 7.0F : 5.0F,
                                      IM_COL32(250, 210, 90, 255));
            }
        }
    }
    for (std::size_t trigger_index = 0; trigger_index < map.triggers.size(); ++trigger_index) {
        const auto& trigger = map.triggers[trigger_index];
        if (trigger.collision_index >= map.collisions.size()) continue;
        const auto& collision = map.collisions[trigger.collision_index];
        if (!layer_visible(map, collision.layer_id)) continue;
        fabric::core::Vec2 anchor = collision.center;
        if (!collision.points.empty()) {
            anchor = {};
            for (const auto point : collision.points) {
                anchor.x += point.x;
                anchor.y += point.y;
            }
            const auto count = static_cast<float>(collision.points.size());
            anchor.x /= count;
            anchor.y /= count;
        }
        const auto label_position = world_to_screen(anchor);
        const auto color = static_cast<int>(trigger_index) == selected_trigger_index
            ? IM_COL32(255, 240, 100, 255) : IM_COL32(160, 220, 255, 230);
        draw->AddText({label_position.x + 8.0F, label_position.y + 8.0F}, color,
                      trigger.event_id.value.c_str());
    }
    for (const auto& instance : map.instances) {
        if (!layer_visible(map, instance.layer_id)) continue;
        const auto transform = transform_for(instance);
        const auto point = world_to_screen(transform.position);
        const bool selected = std::find(selected_instances.begin(), selected_instances.end(),
                                        instance.id) != selected_instances.end();
        draw->AddCircleFilled(point, selected ? 8.0F : 6.0F,
                              selected ? IM_COL32(90, 190, 255, 255)
                                       : IM_COL32(150, 205, 165, 255));
        draw->AddText({point.x + 9.0F, point.y - 7.0F}, IM_COL32(220, 225, 235, 230),
                      instance.id.c_str());
    }
    if (selected_instances.size() == 1U) {
        const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                           [&](const auto& instance) {
                                               return instance.id == selected_instances.front();
                                           });
        if (selected != map.instances.end() && layer_visible(map, selected->layer_id)) {
            const auto transform = transform_for(*selected);
            const auto center = world_to_screen(transform.position);
            const auto scale_handle = ImVec2{center.x + 36.0F, center.y};
            const auto rotate_handle = ImVec2{center.x, center.y - 36.0F};
            draw->AddLine({center.x - 18.0F, center.y}, {center.x + 48.0F, center.y},
                          IM_COL32(90, 190, 255, 180), 1.0F);
            draw->AddCircleFilled(scale_handle, 6.0F, IM_COL32(100, 220, 140, 240));
            draw->AddCircle(rotate_handle, 6.0F, IM_COL32(240, 180, 80, 240), 16, 2.0F);
            draw->AddCircle(center, 11.0F, IM_COL32(90, 190, 255, 240), 24, 2.0F);
        }
    } else if (selected_instances.size() > 1U) {
        ImVec2 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        ImVec2 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        bool found = false;
        for (const auto& instance : map.instances) {
            if (std::find(selected_instances.begin(), selected_instances.end(), instance.id) ==
                    selected_instances.end() || !layer_visible(map, instance.layer_id))
                continue;
            const auto point = world_to_screen(transform_for(instance).position);
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            found = true;
        }
        if (found) {
            minimum.x -= 12.0F;
            minimum.y -= 12.0F;
            maximum.x += 12.0F;
            maximum.y += 12.0F;
            draw->AddRect(minimum, maximum, IM_COL32(90, 190, 255, 230), 2.0F, 0, 2.0F);
        }
    }
    draw->AddRect(canvas_pos,
                  {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                  IM_COL32(130, 140, 155, 255));
    if (selection_box.active) {
        const ImVec2 minimum{std::min(selection_box.start_mouse.x,
                                      selection_box.current_mouse.x),
                             std::min(selection_box.start_mouse.y,
                                      selection_box.current_mouse.y)};
        const ImVec2 maximum{std::max(selection_box.start_mouse.x,
                                      selection_box.current_mouse.x),
                             std::max(selection_box.start_mouse.y,
                                      selection_box.current_mouse.y)};
        draw->AddRectFilled(minimum, maximum, IM_COL32(80, 160, 240, 35));
        draw->AddRect(minimum, maximum, IM_COL32(100, 190, 255, 220));
    }

    if (hovered) {
        const auto& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0F) {
            const auto before = screen_to_world(io.MousePos);
            zoom = std::clamp(zoom * std::pow(1.15F, io.MouseWheel), 0.1F, 32.0F);
            const auto after = world_to_screen(before);
            pan.x += io.MousePos.x - after.x;
            pan.y += io.MousePos.y - after.y;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan.x += io.MouseDelta.x;
            pan.y += io.MouseDelta.y;
        }
        if (!io.WantTextInput && !placement_mode && !gizmo.active &&
            !point_gizmo.active && !selection_box.active && !selected_instances.empty()) {
            std::vector<fabric::core::ResourceId> ids;
            for (const auto& id : selected_instances) ids.push_back({.value = id});
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                const auto removed = session.remove_instances(ids);
                status = removed ? "Selected instances deleted"
                                 : "Delete rejected (selection locked or invalid)";
                if (removed) selected_instances.clear();
                return;
            }
            fabric::core::Vec2 nudge{};
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) nudge.x = -1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) nudge.x = 1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) nudge.y = 1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) nudge.y = -1.0F;
            if (nudge.x != 0.0F || nudge.y != 0.0F) {
                const auto moved = session.translate_instances(ids, nudge, snapping);
                status = moved ? "Selected instances nudged"
                                : "Nudge rejected (selection locked or invalid)";
                return;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) &&
                selected_instances.size() == 1U) {
                const auto duplicated = session.duplicate_instance(
                    ids.front(), {1.0F, 1.0F}, snapping);
                status = duplicated ? "Selected instance duplicated"
                                    : "Duplication rejected (locked or invalid)";
                return;
            }
        }
        if (!placement_mode && !gizmo.active && !point_gizmo.active && io.KeyCtrl &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_instances.size() == 1U) {
            const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                               [&](const auto& instance) {
                                                   return instance.id == selected_instances.front();
                                               });
            if (selected != map.instances.end()) {
                const auto target = screen_to_world(io.MousePos);
                const fabric::core::Vec2 offset{
                    target.x - selected->transform.position.x,
                    target.y - selected->transform.position.y};
                const auto duplicated = session.duplicate_instance(
                    {.value = selected->id}, offset, snapping);
                status = duplicated ? "Instance duplicated at cursor"
                                    : "Duplication rejected (locked or invalid)";
                return;
            }
        }
        if (placement_mode && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            fabric::project::MapInstance instance;
            instance.id = placement_id;
            instance.layer_id = active_layer_id;
            instance.transform.position = screen_to_world(io.MousePos);
            if (placement_kind == 0)
                instance.entity = fabric::project::ResourceReference{
                    {.value = placement_resource_id}, "entity"};
            else
                instance.prefab = fabric::project::ResourceReference{
                    {.value = placement_resource_id}, "prefab"};
            const auto placed = session.place_instance(std::move(instance), snapping);
            status = placed ? "Instance placed" :
                             "Placement rejected (id, resource, layer or lock)";
            if (placed) {
                placement_mode = false;
                placement_id.clear();
                placement_resource_id.clear();
            }
            return;
        }
        if (!gizmo.active && !point_gizmo.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_collision_index >= 0) {
            const auto collision_index = static_cast<std::size_t>(selected_collision_index);
            if (collision_index < map.collisions.size()) {
                const auto& collision = map.collisions[collision_index];
                if (collision.kind == fabric::project::CollisionShapeKind::polygon ||
                    collision.kind == fabric::project::CollisionShapeKind::chain) {
                    for (std::size_t point = 0; point < collision.points.size(); ++point) {
                        const auto screen = world_to_screen(collision.points[point]);
                        const auto dx = io.MousePos.x - screen.x;
                        const auto dy = io.MousePos.y - screen.y;
                        if (std::sqrt(dx * dx + dy * dy) <= 10.0F) {
                            point_gizmo.active = true;
                            point_gizmo.collision_index = selected_collision_index;
                            point_gizmo.point_index = point;
                            point_gizmo.preview_point = collision.points[point];
                            break;
                        }
                    }
                }
            }
        }
        if (point_gizmo.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                point_gizmo.preview_point = screen_to_world(io.MousePos);
            } else {
                const auto index = static_cast<std::size_t>(point_gizmo.collision_index);
                if (index < map.collisions.size() &&
                    point_gizmo.point_index < map.collisions[index].points.size()) {
                    auto shape = map.collisions[index];
                    shape.points[point_gizmo.point_index] = point_gizmo.preview_point;
                    const auto committed = session.set_collision_shape(index, std::move(shape));
                    status = committed ? "Collision point committed"
                                       : "Collision point rejected (layer locked or invalid)";
                }
                point_gizmo.active = false;
                point_gizmo.collision_index = -1;
            }
        }
        if (!gizmo.active && !point_gizmo.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !selected_instances.empty()) {
            const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                               [&](const auto& instance) {
                                                   return instance.id == selected_instances.front();
                                               });
            if (selected_instances.size() == 1U && selected != map.instances.end()) {
                const auto transform = transform_for(*selected);
                const auto center = world_to_screen(transform.position);
                const auto scale_handle = ImVec2{center.x + 36.0F, center.y};
                const auto rotate_handle = ImVec2{center.x, center.y - 36.0F};
                const auto distance = [](const ImVec2 a, const ImVec2 b) {
                    const auto dx = a.x - b.x;
                    const auto dy = a.y - b.y;
                    return std::sqrt(dx * dx + dy * dy);
                };
                const auto handle_distance = distance(io.MousePos, scale_handle);
                const auto rotate_distance = distance(io.MousePos, rotate_handle);
                const auto center_distance = distance(io.MousePos, center);
                if (handle_distance <= 10.0F || rotate_distance <= 10.0F ||
                    center_distance <= 12.0F) {
                    gizmo.active = true;
                    gizmo.instance_id = selected->id;
                    gizmo.start_mouse = io.MousePos;
                    gizmo.start_transform = selected->transform;
                    gizmo.preview_transform = selected->transform;
                    gizmo.mode = handle_distance <= 10.0F
                        ? CanvasGizmoMode::scale
                        : (rotate_distance <= 10.0F ? CanvasGizmoMode::rotate
                                                   : CanvasGizmoMode::translate);
                }
            } else if (selected_instances.size() > 1U) {
                const auto hit = std::find_if(map.instances.begin(), map.instances.end(),
                                              [&](const auto& instance) {
                                                  if (std::find(selected_instances.begin(),
                                                                selected_instances.end(),
                                                                instance.id) == selected_instances.end())
                                                      return false;
                                                  const auto point = world_to_screen(
                                                      instance.transform.position);
                                                  const auto dx = io.MousePos.x - point.x;
                                                  const auto dy = io.MousePos.y - point.y;
                                                  return std::sqrt(dx * dx + dy * dy) <= 12.0F;
                                              });
                if (hit != map.instances.end()) {
                    gizmo.active = true;
                    gizmo.mode = CanvasGizmoMode::translate;
                    gizmo.instance_id = hit->id;
                    gizmo.start_mouse = io.MousePos;
                    gizmo.preview_delta = {};
                    gizmo.group_ids = selected_instances;
                }
            }
        }
        if (gizmo.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (gizmo.mode == CanvasGizmoMode::translate) {
                    const auto start = screen_to_world(gizmo.start_mouse);
                    const auto current = screen_to_world(io.MousePos);
                    gizmo.preview_delta = {current.x - start.x, current.y - start.y};
                    if (gizmo.group_ids.size() == 1U)
                        gizmo.preview_transform.position = {
                            gizmo.start_transform.position.x + gizmo.preview_delta.x,
                            gizmo.start_transform.position.y + gizmo.preview_delta.y};
                } else if (gizmo.mode == CanvasGizmoMode::rotate) {
                    const auto center = world_to_screen(gizmo.start_transform.position);
                    const auto start_angle = std::atan2(gizmo.start_mouse.y - center.y,
                                                        gizmo.start_mouse.x - center.x);
                    const auto current_angle = std::atan2(io.MousePos.y - center.y,
                                                          io.MousePos.x - center.x);
                    constexpr float radians_to_degrees = 57.29577951308232F;
                    gizmo.preview_transform.rotation_degrees =
                        gizmo.start_transform.rotation_degrees +
                        (current_angle - start_angle) * radians_to_degrees;
                } else {
                    const auto delta = (io.MousePos.x - gizmo.start_mouse.x) / 48.0F;
                    const auto factor = std::max(0.05F, 1.0F + delta);
                    gizmo.preview_transform.scale = {
                        gizmo.start_transform.scale.x * factor,
                        gizmo.start_transform.scale.y * factor};
                }
            } else {
                bool committed = false;
                if (gizmo.group_ids.size() > 1U) {
                    std::vector<fabric::core::ResourceId> ids;
                    for (const auto& id : gizmo.group_ids) ids.push_back({.value = id});
                    committed = session.translate_instances(ids, gizmo.preview_delta, snapping);
                } else {
                    committed = session.set_instance_transform(
                        {.value = gizmo.instance_id}, gizmo.preview_transform,
                        gizmo.mode == CanvasGizmoMode::translate
                            ? snapping : fabric::editor::MapSnapSettings{.enabled = false});
                }
                status = committed ? "Canvas transform committed"
                                   : "Canvas transform rejected (layer locked or invalid)";
                gizmo.active = false;
                gizmo.instance_id.clear();
                gizmo.group_ids.clear();
                gizmo.preview_delta = {};
            }
        }
        if (!gizmo.active && !point_gizmo.active && !placement_mode &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl) {
            selection_box.active = true;
            selection_box.append = io.KeyShift;
            selection_box.start_mouse = io.MousePos;
            selection_box.current_mouse = io.MousePos;
        }
        if (selection_box.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                selection_box.current_mouse = io.MousePos;
            } else {
                const auto start = screen_to_world(selection_box.start_mouse);
                const auto end = screen_to_world(selection_box.current_mouse);
                const auto min_x = std::min(start.x, end.x);
                const auto max_x = std::max(start.x, end.x);
                const auto min_y = std::min(start.y, end.y);
                const auto max_y = std::max(start.y, end.y);
                const auto width = std::abs(selection_box.current_mouse.x -
                                            selection_box.start_mouse.x);
                const auto height = std::abs(selection_box.current_mouse.y -
                                             selection_box.start_mouse.y);
                if (width >= 5.0F || height >= 5.0F) {
                    if (!selection_box.append) selected_instances.clear();
                    for (const auto& instance : map.instances) {
                        if (!layer_visible(map, instance.layer_id)) continue;
                        const auto& position = instance.transform.position;
                        if (position.x >= min_x && position.x <= max_x &&
                            position.y >= min_y && position.y <= max_y &&
                            std::find(selected_instances.begin(), selected_instances.end(),
                                      instance.id) == selected_instances.end())
                            selected_instances.push_back(instance.id);
                    }
                    status = "Rectangle selection changed";
                } else {
                    const auto world = screen_to_world(selection_box.start_mouse);
                    auto hit = map.instances.end();
                    float best_distance = 12.0F / zoom;
                    for (auto candidate = map.instances.begin(); candidate != map.instances.end();
                         ++candidate) {
                        if (!layer_visible(map, candidate->layer_id)) continue;
                        const auto dx = candidate->transform.position.x - world.x;
                        const auto dy = candidate->transform.position.y - world.y;
                        const auto distance = std::sqrt(dx * dx + dy * dy);
                        if (distance <= best_distance) {
                            best_distance = distance;
                            hit = candidate;
                        }
                    }
                    if (!selection_box.append) selected_instances.clear();
                    if (hit != map.instances.end()) {
                        const auto existing = std::find(selected_instances.begin(),
                                                        selected_instances.end(), hit->id);
                        if (selection_box.append && existing != selected_instances.end())
                            selected_instances.erase(existing);
                        else if (existing == selected_instances.end())
                            selected_instances.push_back(hit->id);
                        status = "Canvas selection changed";
                    }
                }
                selection_box.active = false;
            }
        }
        if (!gizmo.active && !selection_box.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto world = screen_to_world(io.MousePos);
            auto hit = map.instances.end();
            float best_distance = 12.0F / zoom;
            for (auto candidate = map.instances.begin(); candidate != map.instances.end();
                 ++candidate) {
                if (!layer_visible(map, candidate->layer_id)) continue;
                const auto dx = candidate->transform.position.x - world.x;
                const auto dy = candidate->transform.position.y - world.y;
                const auto distance = std::sqrt(dx * dx + dy * dy);
                if (distance <= best_distance) {
                    best_distance = distance;
                    hit = candidate;
                }
            }
            if (!io.KeyShift) selected_instances.clear();
            if (hit != map.instances.end()) {
                const auto selected = std::find(selected_instances.begin(),
                                                selected_instances.end(), hit->id);
                if (io.KeyShift && selected != selected_instances.end())
                    selected_instances.erase(selected);
                else if (selected == selected_instances.end())
                    selected_instances.push_back(hit->id);
                status = "Canvas selection changed";
            }
        }
    }
    ImGui::TextDisabled("Zoom %.2fx · pan %.1f, %.1f · clic: sélectionner · molette: zoom · bouton milieu: déplacer",
                       zoom, pan.x, pan.y);
}

int run(const std::filesystem::path& project_root,
        const fabric::core::ResourceId& map_id) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    auto* window = SDL_CreateWindow("Vertex Loom Map Studio",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1200, 760,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    auto context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
#if defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    fabric::editor::MapSession session;
    std::string status;
    if (!project_root.empty()) {
        if (!session.open(project_root, map_id)) status = "Map could not be opened";
        else status = "Map opened";
    }
    std::string event_id;
    std::string selected_event_id;
    int event_editor_index = -1;
    std::vector<fabric::project::MapProperty> event_payload_editor;
    std::string event_property_id;
    std::string event_property_value;
    int event_property_kind = 2;
    std::string trigger_id;
    std::string trigger_event_id;
    int trigger_collision_index = 0;
    int selected_collision_index = -1;
    int collision_editor_index = -1;
    fabric::project::CollisionShape collision_editor;
    int selected_trigger_index = -1;
    int trigger_editor_index = -1;
    int trigger_editor_collision_index = 0;
    fabric::project::TriggerDefinition trigger_editor;
    std::vector<std::string> selected_instances;
    std::string active_layer_id;
    bool placement_mode = false;
    std::string placement_id;
    std::string placement_resource_id;
    int placement_kind = 0;
    std::string selected_prefab;
    std::string override_id;
    std::string override_value;
    int override_kind = 2;
    std::string instance_property_id;
    std::string instance_property_value;
    int instance_property_kind = 2;
    ImVec2 canvas_pan{0.0F, 0.0F};
    float canvas_zoom = 1.0F;
    CanvasGizmoState canvas_gizmo;
    CollisionPointGizmoState collision_point_gizmo;
    SelectionBoxState selection_box;
    fabric::editor::MapSnapSettings canvas_snapping;
    TransformEditorState transform_editor;
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Map Studio", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse);
        if (!session.has_map()) {
            ImGui::TextWrapped("Open a map with: map_studio <project-directory> <map-id>");
            draw_errors(session);
        } else {
            const auto& map = *session.map();
            if (active_layer_id.empty() && !map.layers.empty())
                active_layer_id = map.layers.front().id;
            ImGui::Text("Map: %s (%s)", map.document.name.c_str(),
                        map.document.id.value.c_str());
            ImGui::SameLine();
            ImGui::TextColored(session.dirty() ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
                                               : ImVec4{0.45F, 0.9F, 0.55F, 1.0F},
                               session.dirty() ? "dirty" : "saved");
            if (ImGui::Button("Save")) status = session.save() ? "Map saved" : "Save failed";
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.can_undo());
            if (ImGui::Button("Undo")) static_cast<void>(session.undo());
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.can_redo());
            if (ImGui::Button("Redo")) static_cast<void>(session.redo());
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::Columns(2, "map-studio-columns", true);
            ImGui::Text("Layers (%zu)", map.layers.size());
            bool layer_changed = false;
            for (std::size_t layer_index = 0; layer_index < map.layers.size(); ++layer_index) {
                const auto layer = map.layers[layer_index];
                ImGui::PushID(layer.id.c_str());
                bool visible = layer.visible;
                if (ImGui::Checkbox("##visible", &visible) &&
                    session.set_layer_visibility({.value = layer.id}, visible)) {
                    status = "Layer visibility changed";
                    layer_changed = true;
                }
                ImGui::SameLine();
                bool locked = layer.locked;
                if (ImGui::Checkbox("##locked", &locked) &&
                    session.set_layer_locked({.value = layer.id}, locked)) {
                    status = "Layer lock changed";
                    layer_changed = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(layer.name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(active_layer_id == layer.id ? "Active" : "Use"))
                    active_layer_id = layer.id;
                ImGui::SameLine();
                float depth = layer.depth;
                ImGui::SetNextItemWidth(90.0F);
                if (ImGui::DragFloat("##depth", &depth, 0.1F) &&
                    ImGui::IsItemDeactivatedAfterEdit() &&
                    session.set_layer_depth({.value = layer.id}, depth)) {
                    status = "Layer depth changed";
                    layer_changed = true;
                }
                ImGui::PopID();
                if (layer_changed) break;
            }
            if (layer_changed) ImGui::TextDisabled("Layer edit recorded in undo history");
            ImGui::SeparatorText("Content");
            ImGui::Text("Instances: %zu", map.instances.size());
            for (const auto& instance : map.instances) {
                bool selected = std::find(selected_instances.begin(), selected_instances.end(),
                                          instance.id) != selected_instances.end();
                if (ImGui::Checkbox((instance.id + "##selected").c_str(), &selected)) {
                    if (selected) selected_instances.push_back(instance.id);
                    else selected_instances.erase(std::remove(selected_instances.begin(),
                                                               selected_instances.end(), instance.id),
                                                  selected_instances.end());
                }
            }
            ImGui::BeginDisabled(selected_instances.empty());
            if (ImGui::Button("Nudge selected +1")) {
                std::vector<fabric::core::ResourceId> ids;
                for (const auto& id : selected_instances) ids.push_back({.value = id});
                status = session.translate_instances(ids, {1.0F, 0.0F})
                    ? "Selected instances moved" : "Selection contains a locked instance";
            }
            if (selected_instances.size() == 1U) {
                ImGui::SameLine();
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                if (ImGui::Button("Duplicate selected")) {
                    status = session.duplicate_instance(selected_id)
                        ? "Instance duplicated" : "Instance duplication rejected";
                }
                ImGui::SameLine();
                if (ImGui::Button("Move selected to front")) {
                    status = session.reorder_instance(selected_id, 0U)
                        ? "Instance reordered" : "Instance reorder rejected";
                }
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(selected_instances.empty() || active_layer_id.empty());
            if (ImGui::Button("Move selected to active layer")) {
                std::vector<fabric::core::ResourceId> ids;
                for (const auto& id : selected_instances) ids.push_back({.value = id});
                status = session.set_instances_layer(ids, {.value = active_layer_id})
                    ? "Instances moved to active layer"
                    : "Layer move rejected (locked or invalid)";
            }
            ImGui::EndDisabled();
            ImGui::SeparatorText("Placement");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("New instance id", &placement_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Resource id", &placement_resource_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::Combo("Resource kind", &placement_kind, "entity\0prefab\0");
            ImGui::BeginDisabled(placement_id.empty() || placement_resource_id.empty() ||
                                 active_layer_id.empty());
            if (ImGui::Button(placement_mode ? "Cancel placement" : "Place in canvas"))
                placement_mode = !placement_mode;
            ImGui::EndDisabled();
            draw_map_canvas(session, selected_instances, canvas_pan, canvas_zoom, canvas_gizmo,
                            selected_collision_index, collision_point_gizmo,
                            selected_trigger_index, active_layer_id, selection_box,
                            placement_mode,
                            placement_id, placement_resource_id, placement_kind,
                            canvas_snapping, status);
            draw_transform_editor(session, selected_instances, transform_editor, status);
            ImGui::Text("Collisions: %zu", map.collisions.size());
            for (std::size_t collision_index = 0; collision_index < map.collisions.size();
                 ++collision_index) {
                const auto& collision = map.collisions[collision_index];
                ImGui::PushID(static_cast<int>(collision_index));
                const auto label = "[" + std::to_string(collision_index) + "] " +
                                   collision_shape_text(collision) + " / layer " +
                                   collision.layer_id;
                if (ImGui::Selectable(label.c_str(),
                                      selected_collision_index ==
                                          static_cast<int>(collision_index))) {
                    selected_collision_index = static_cast<int>(collision_index);
                }
                ImGui::PopID();
            }
            if (selected_collision_index >= 0 &&
                static_cast<std::size_t>(selected_collision_index) < map.collisions.size()) {
                if (collision_editor_index != selected_collision_index) {
                    collision_editor_index = selected_collision_index;
                    collision_editor = map.collisions[static_cast<std::size_t>(
                        selected_collision_index)];
                }
                ImGui::SeparatorText("Selected collision");
                ImGui::Text("Layer: %s", collision_editor.layer_id.c_str());
                ImGui::Text("Kind: %s", collision_shape_text(collision_editor).c_str());
                ImGui::Checkbox("Sensor", &collision_editor.sensor);
                ImGui::SetNextItemWidth(220.0F);
                ImGui::DragFloat2("Center", &collision_editor.center.x, 0.1F);
                if (collision_editor.kind == fabric::project::CollisionShapeKind::circle ||
                    collision_editor.kind == fabric::project::CollisionShapeKind::capsule) {
                    ImGui::SetNextItemWidth(220.0F);
                    ImGui::DragFloat("Radius", &collision_editor.radius, 0.1F, 0.0F,
                                     4096.0F);
                }
                if (collision_editor.kind == fabric::project::CollisionShapeKind::capsule) {
                    ImGui::SetNextItemWidth(220.0F);
                    ImGui::DragFloat("Length", &collision_editor.length, 0.1F, 0.0F,
                                     4096.0F);
                }
                if (collision_editor.kind == fabric::project::CollisionShapeKind::polygon ||
                    collision_editor.kind == fabric::project::CollisionShapeKind::chain) {
                    ImGui::Text("Points: %zu", collision_editor.points.size());
                    for (std::size_t point_index = 0;
                         point_index < collision_editor.points.size(); ++point_index) {
                        ImGui::PushID(static_cast<int>(point_index));
                        ImGui::SetNextItemWidth(220.0F);
                        ImGui::DragFloat2("Point", &collision_editor.points[point_index].x,
                                          0.1F);
                        ImGui::PopID();
                    }
                    const auto minimum_points = collision_editor.kind ==
                        fabric::project::CollisionShapeKind::polygon ? 3U : 2U;
                    ImGui::BeginDisabled(collision_editor.points.size() <= minimum_points);
                    if (ImGui::Button("Remove last point")) collision_editor.points.pop_back();
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Add point")) collision_editor.points.push_back({});
                }
                if (ImGui::Button("Apply collision")) {
                    const auto applied = session.set_collision_shape(
                        static_cast<std::size_t>(selected_collision_index), collision_editor);
                    status = applied ? "Collision updated" :
                                       "Collision update rejected (layer locked or invalid)";
                }
            }
            ImGui::Text("Triggers: %zu", map.triggers.size());
            ImGui::Text("Events: %zu", map.events.size());
            ImGui::NextColumn();
            ImGui::SeparatorText("Events");
            ImGui::SetNextItemWidth(250.0F);
            ImGui::InputText("New event id", &event_id);
            ImGui::SameLine();
            ImGui::BeginDisabled(event_id.empty());
            if (ImGui::Button("Declare")) {
                if (session.declare_event({{.value = event_id}, {}})) {
                    status = "Event declared";
                    event_id.clear();
                } else status = "Event declaration rejected";
            }
            ImGui::EndDisabled();
            for (const auto& event_definition : map.events) {
                const auto selected = selected_event_id == event_definition.id.value;
                if (ImGui::Selectable(event_definition.id.value.c_str(), selected))
                    selected_event_id = event_definition.id.value;
            }
            if (!selected_event_id.empty()) {
                const auto event_definition = std::find_if(
                    map.events.begin(), map.events.end(), [&](const auto& event) {
                        return event.id.value == selected_event_id;
                    });
                if (event_definition != map.events.end()) {
                    if (event_editor_index != static_cast<int>(
                            std::distance(map.events.begin(), event_definition))) {
                        event_editor_index = static_cast<int>(
                            std::distance(map.events.begin(), event_definition));
                        event_payload_editor = event_definition->payload;
                    }
                    ImGui::SeparatorText("Selected event payload");
                    for (std::size_t property_index = 0;
                         property_index < event_payload_editor.size(); ++property_index) {
                        ImGui::PushID(static_cast<int>(property_index));
                        ImGui::BulletText("%s = %s", event_payload_editor[property_index].id.c_str(),
                                          property_value_text(
                                              event_payload_editor[property_index].value).c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove"))
                            event_payload_editor.erase(event_payload_editor.begin() +
                                                       static_cast<std::ptrdiff_t>(property_index));
                        ImGui::PopID();
                        if (property_index >= event_payload_editor.size()) break;
                    }
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::InputText("Payload property id", &event_property_id);
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::Combo("Payload type", &event_property_kind,
                                 "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::InputText("Payload value", &event_property_value);
                    ImGui::BeginDisabled(event_property_id.empty() ||
                                         event_property_value.empty());
                    if (ImGui::Button("Apply payload property")) {
                        const auto value = parse_override_value(event_property_kind,
                                                                 event_property_value);
                        if (value) {
                            const auto existing = std::find_if(
                                event_payload_editor.begin(), event_payload_editor.end(),
                                [&](const auto& property) {
                                    return property.id == event_property_id;
                                });
                            if (existing != event_payload_editor.end()) existing->value = *value;
                            else event_payload_editor.push_back({event_property_id, *value});
                            const auto applied = session.set_event_payload(
                                {.value = selected_event_id}, event_payload_editor);
                            status = applied ? "Event payload updated" :
                                               "Event payload rejected";
                            if (applied) {
                                event_property_id.clear();
                                event_property_value.clear();
                            }
                        } else status = "Event payload value rejected";
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::SeparatorText("Triggers");
            for (std::size_t trigger_index = 0; trigger_index < map.triggers.size();
                 ++trigger_index) {
                const auto& trigger = map.triggers[trigger_index];
                ImGui::PushID(static_cast<int>(trigger_index));
                const auto label = trigger.id + " -> " + trigger.event_id.value +
                                   " (collision " + std::to_string(trigger.collision_index) + ")";
                if (ImGui::Selectable(label.c_str(), selected_trigger_index ==
                                      static_cast<int>(trigger_index)))
                    selected_trigger_index = static_cast<int>(trigger_index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    status = session.remove_trigger({.value = trigger.id})
                        ? "Trigger removed" : "Trigger removal rejected";
                }
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("New trigger id", &trigger_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Trigger event id", &trigger_event_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputInt("Collision index", &trigger_collision_index);
            ImGui::BeginDisabled(trigger_id.empty() || trigger_event_id.empty() ||
                                 trigger_collision_index < 0);
            if (ImGui::Button("Add trigger")) {
                const auto added = session.add_trigger(
                    {trigger_id, "triggers", static_cast<std::size_t>(trigger_collision_index),
                     {.value = trigger_event_id}, {}});
                status = added ? "Trigger added" : "Trigger rejected";
                if (added) {
                    trigger_id.clear();
                    trigger_event_id.clear();
                    trigger_collision_index = 0;
                }
            }
            ImGui::EndDisabled();
            if (selected_trigger_index >= 0 &&
                static_cast<std::size_t>(selected_trigger_index) < map.triggers.size()) {
                if (trigger_editor_index != selected_trigger_index) {
                    trigger_editor_index = selected_trigger_index;
                    trigger_editor = map.triggers[static_cast<std::size_t>(selected_trigger_index)];
                    trigger_editor_collision_index =
                        static_cast<int>(trigger_editor.collision_index);
                }
                ImGui::SeparatorText("Selected trigger");
                ImGui::Text("Id: %s", trigger_editor.id.c_str());
                ImGui::SetNextItemWidth(220.0F);
                ImGui::InputText("Event id", &trigger_editor.event_id.value);
                ImGui::SetNextItemWidth(220.0F);
                ImGui::InputInt("Collision index", &trigger_editor_collision_index);
                const auto event_definition = std::find_if(
                    map.events.begin(), map.events.end(), [&](const auto& event) {
                        return event.id == trigger_editor.event_id;
                    });
                if (event_definition != map.events.end()) {
                    ImGui::Text("Payload:");
                    for (const auto& property : event_definition->payload)
                        ImGui::BulletText("%s = %s", property.id.c_str(),
                                          property_value_text(property.value).c_str());
                }
                ImGui::BeginDisabled(trigger_editor_collision_index < 0 ||
                                     trigger_editor.event_id.value.empty());
                if (ImGui::Button("Apply trigger")) {
                    trigger_editor.collision_index = static_cast<std::size_t>(
                        trigger_editor_collision_index);
                    const auto applied = session.set_trigger(
                        static_cast<std::size_t>(selected_trigger_index), trigger_editor);
                    status = applied ? "Trigger updated" :
                                       "Trigger update rejected (event, collision or layer)";
                }
                ImGui::EndDisabled();
            }
            if (selected_instances.size() == 1U) {
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                ImGui::SeparatorText("Selected instance properties");
                for (const auto& property : session.effective_instance_properties(selected_id))
                    ImGui::BulletText("%s = %s", property.id.c_str(),
                                      property_value_text(property.value).c_str());
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Instance property id", &instance_property_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Instance type", &instance_property_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Instance value", &instance_property_value);
                ImGui::BeginDisabled(instance_property_id.empty() || instance_property_value.empty());
                if (ImGui::Button("Apply instance property")) {
                    const auto value = parse_override_value(instance_property_kind,
                                                             instance_property_value);
                    const auto applied = value && session.set_instance_property(
                        selected_id, {instance_property_id, *value});
                    status = applied ? "Instance property applied" : "Instance property rejected";
                    if (applied) {
                        instance_property_id.clear();
                        instance_property_value.clear();
                    }
                }
                ImGui::EndDisabled();
            }
            ImGui::SeparatorText("Prefab overrides");
            for (const auto& prefab : map.prefabs) {
                const auto selected = selected_prefab == prefab.id;
                if (ImGui::Selectable(prefab.id.c_str(), selected)) selected_prefab = prefab.id;
            }
            if (!selected_prefab.empty()) {
                ImGui::Text("Selected prefab: %s", selected_prefab.c_str());
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Property id", &override_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Type", &override_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Value", &override_value);
                ImGui::BeginDisabled(override_id.empty() || override_value.empty());
                if (ImGui::Button("Apply override")) {
                    const auto value = parse_override_value(override_kind, override_value);
                    const auto applied = value && session.set_prefab_override(
                        {.value = selected_prefab}, {override_id, *value});
                    status = applied ? "Prefab override applied" : "Prefab override rejected";
                    if (applied) {
                        override_id.clear();
                        override_value.clear();
                    }
                }
                ImGui::EndDisabled();
            }
            ImGui::Columns(1);
            if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
            draw_errors(session);
        }
        ImGui::End();
        ImGui::Render();
        glViewport(0, 0, static_cast<GLsizei>(ImGui::GetIO().DisplaySize.x),
                   static_cast<GLsizei>(ImGui::GetIO().DisplaySize.y));
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: map_studio [project-directory map-id]\n";
        return 64;
    }
    const std::filesystem::path project = argc == 3 ? argv[1] : std::filesystem::path{};
    const fabric::core::ResourceId map_id{argc == 3 ? argv[2] : ""};
    return run(project, map_id);
}
