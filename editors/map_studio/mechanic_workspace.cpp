#include "mechanic_workspace.hpp"

#include "editor_widgets.hpp"
#include "resource_picker.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fabric::map_studio {
namespace {

using editor_ui::draw_disabled_reason;
using editor_ui::draw_id_picker;
using editor_ui::draw_resource_name_field;
using editor_ui::draw_technical_tooltip;
using editor_ui::focus_first_field_error;

bool draw_mechanic_node_picker(
    const char* label,
    const std::span<const fabric::project::MechanicNodeDefinition> nodes,
    std::string& selected_id) {
    const auto selected = std::ranges::find_if(
        nodes, [&](const auto& node) { return node.id == selected_id; });
    const std::string preview = selected != nodes.end()
        ? selected->id
        : selected_id.empty() ? std::string{"Choose a mechanic node..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##mechanic-node-search", "Search node ID or type...",
                                 &filter);
        bool found = false;
        for (const auto& node : nodes) {
            std::string haystack = node.id + " " + node.type;
            std::ranges::transform(haystack, haystack.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            std::string needle = filter;
            std::ranges::transform(needle, needle.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            if (!needle.empty() && haystack.find(needle) == std::string::npos)
                continue;
            found = true;
            const bool is_selected = node.id == selected_id;
            const std::string item_label = node.id + " (" + node.type + ")##mechanic-node-option-" +
                node.id;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                selected_id = node.id;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        if (!found) ImGui::TextDisabled("No matching mechanic node.");
        if (selected == nodes.end() && !selected_id.empty())
            ImGui::TextDisabled("Missing node reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_mechanic_port_picker(
    const char* label,
    const std::span<const fabric::project::MechanicNodeDefinition> nodes,
    const std::string_view node_id,
    std::string& selected_id,
    const fabric::project::MechanicPortDirection direction) {
    const auto node = std::ranges::find_if(
        nodes, [&](const auto& candidate) { return candidate.id == node_id; });
    const fabric::project::MechanicPortDefinition* selected = nullptr;
    if (node != nodes.end()) {
        const auto selected_it = std::ranges::find_if(node->ports, [&](const auto& port) {
            return port.id == selected_id && port.direction == direction;
        });
        if (selected_it != node->ports.end()) selected = &*selected_it;
    }
    const std::string preview = selected != nullptr
        ? selected->id + " (" + std::string{fabric::project::to_string(selected->type)} + ")"
        : selected_id.empty() ? std::string{"Choose a mechanic port..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##mechanic-port-search", "Search port ID, name or type...",
                                 &filter);
        bool found = false;
        if (node != nodes.end()) {
            for (const auto& port : node->ports) {
                if (port.direction != direction) continue;
                std::string haystack = port.id + " " + port.name + " " +
                    std::string{fabric::project::to_string(port.type)};
                std::ranges::transform(haystack, haystack.begin(), [](const unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
                std::string needle = filter;
                std::ranges::transform(needle, needle.begin(), [](const unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
                if (!needle.empty() && haystack.find(needle) == std::string::npos)
                    continue;
                found = true;
                const bool is_selected = port.id == selected_id;
                const std::string item_label = port.id + " (" + port.name + ", " +
                    std::string{fabric::project::to_string(port.type)} + ")##mechanic-port-option-" +
                    port.id;
                if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                    selected_id = port.id;
                    changed = true;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
        }
        if (!found) ImGui::TextDisabled("No matching mechanic port for this node.");
        if (node == nodes.end())
            ImGui::TextDisabled("Choose a mechanic node first.");
        else if (selected == nullptr && !selected_id.empty())
            ImGui::TextDisabled("Missing port reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}

std::optional<fabric::core::Vec2> mechanic_vec2_property(
    const fabric::project::MechanicNodeDefinition& node,
    const std::string_view id) {
    const auto property = std::ranges::find(
        node.properties, id, &fabric::project::MechanicNodeProperty::id);
    if (property == node.properties.end()) return std::nullopt;
    if (const auto* value = std::get_if<fabric::core::Vec2>(&property->value))
        return *value;
    return std::nullopt;
}

std::optional<float> mechanic_float_property(
    const fabric::project::MechanicNodeDefinition& node,
    const std::string_view id) {
    const auto property = std::ranges::find(
        node.properties, id, &fabric::project::MechanicNodeProperty::id);
    if (property == node.properties.end()) return std::nullopt;
    if (const auto* value = std::get_if<float>(&property->value)) return *value;
    return std::nullopt;
}

fabric::core::Vec2 rotate_mechanic_vector(
    const fabric::core::Vec2 value, const float degrees) {
    constexpr float radians_per_degree = 0.01745329251994329577F;
    const float radians = degrees * radians_per_degree;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

fabric::core::Vec2 add_mechanic_vectors(
    const fabric::core::Vec2 left, const fabric::core::Vec2 right) {
    return {left.x + right.x, left.y + right.y};
}

fabric::core::Vec2 subtract_mechanic_vectors(
    const fabric::core::Vec2 left, const fabric::core::Vec2 right) {
    return {left.x - right.x, left.y - right.y};
}

bool draw_mechanic_spatial_canvas(
    fabric::editor::MechanicSession& session,
    MechanicWorkspaceState& state, std::string& status,
    MechanicWorkspaceProbe* probe) {
    if (!session.graph()) return false;
    const auto& graph = *session.graph();
    ImGui::SeparatorText("Spatial canvas");
    ImGui::TextDisabled(
        "Drag shapes to move; selected rectangles expose size and body rotation.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    ImGui::SliderFloat("Zoom##mechanic-spatial", &state.spatial_zoom,
                       12.0F, 80.0F, "%.0f px/u");
    const ImVec2 size{std::max(1.0F, ImGui::GetContentRegionAvail().x), 280.0F};
    ImGui::InvisibleButton("Mechanic spatial canvas", size);
    if (probe && probe->enabled) probe->spatial_canvas_seen = true;
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    const ImVec2 center{(minimum.x + maximum.x) * 0.5F,
                        (minimum.y + maximum.y) * 0.5F};
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{center.x + point.x * state.spatial_zoom,
                      center.y - point.y * state.spatial_zoom};
    };
    const auto to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{
            (point.x - center.x) / state.spatial_zoom,
            (center.y - point.y) / state.spatial_zoom};
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum, maximum, IM_COL32(18, 22, 28, 255), 4.0F);
    draw->PushClipRect(minimum, maximum, true);
    draw->AddLine({minimum.x, center.y}, {maximum.x, center.y},
                  IM_COL32(100, 110, 125, 150));
    draw->AddLine({center.x, minimum.y}, {center.x, maximum.y},
                  IM_COL32(100, 110, 125, 150));

    struct Handle {
        std::string node;
        std::string mutation_node;
        std::string property;
        fabric::core::Vec2 position{};
        fabric::core::Vec2 size{};
        float rotation{};
        bool rectangle{};
        bool joint{};
        bool rotatable{};
    };
    std::vector<Handle> handles;
    for (const auto& node : graph.nodes) {
        if (node.type == "body" || node.type == "sensor") {
            const bool sensor = node.type == "sensor";
            handles.push_back({
                node.id, node.id, sensor ? "center" : "position",
                mechanic_vec2_property(node, sensor ? "center" : "position")
                    .value_or(fabric::core::Vec2{}),
                mechanic_vec2_property(node, "size")
                    .value_or(fabric::core::Vec2{1.0F, 1.0F}),
                sensor ? 0.0F
                       : mechanic_float_property(node, "rotation").value_or(0.0F),
                true, false, !sensor});
        } else if (node.type == "pivot") {
            handles.push_back({node.id, node.id, "position",
                mechanic_vec2_property(node, "position")
                    .value_or(fabric::core::Vec2{}), {}, 0.0F,
                false, false, false});
        } else if (node.type == "joint") {
            const auto connection = std::ranges::find_if(
                graph.connections, [&](const auto& candidate) {
                    return candidate.to_node == node.id &&
                        candidate.to_port == "pivot";
                });
            if (connection == graph.connections.end()) continue;
            const auto pivot = std::ranges::find(
                graph.nodes, connection->from_node,
                &fabric::project::MechanicNodeDefinition::id);
            if (pivot != graph.nodes.end()) {
                handles.push_back({node.id, pivot->id, "position",
                    mechanic_vec2_property(*pivot, "position")
                        .value_or(fabric::core::Vec2{}), {}, 0.0F,
                    false, true, false});
            }
        }
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    std::optional<std::size_t> hit;
    MechanicSpatialDragKind hit_kind = MechanicSpatialDragKind::move;
    for (std::size_t cursor = 0; cursor < handles.size(); ++cursor) {
        const auto& handle = handles[cursor];
        if (!handle.rectangle || state.selected_node != handle.node) continue;
        const auto resize_world = add_mechanic_vectors(
            handle.position, rotate_mechanic_vector(
                {handle.size.x * 0.5F, handle.size.y * 0.5F}, handle.rotation));
        const auto resize_screen = to_screen(resize_world);
        if (std::hypot(mouse.x - resize_screen.x,
                       mouse.y - resize_screen.y) <= 10.0F) {
            hit = cursor;
            hit_kind = MechanicSpatialDragKind::resize;
            break;
        }
        if (handle.rotatable) {
            const auto rotate_world = add_mechanic_vectors(
                handle.position, rotate_mechanic_vector(
                    {0.0F, handle.size.y * 0.5F + 24.0F / state.spatial_zoom},
                    handle.rotation));
            const auto rotate_screen = to_screen(rotate_world);
            if (std::hypot(mouse.x - rotate_screen.x,
                           mouse.y - rotate_screen.y) <= 10.0F) {
                hit = cursor;
                hit_kind = MechanicSpatialDragKind::rotate;
                break;
            }
        }
    }
    int hit_priority = 3;
    if (!hit) {
        for (std::size_t cursor = 0; cursor < handles.size(); ++cursor) {
            const auto& handle = handles[cursor];
            const auto point = to_screen(handle.position);
            const auto local_mouse = rotate_mechanic_vector(
                subtract_mechanic_vectors(to_world(mouse), handle.position),
                -handle.rotation);
            const float distance =
                std::hypot(mouse.x - point.x, mouse.y - point.y);
            const bool contains = handle.rectangle
                ? std::abs(local_mouse.x) <= handle.size.x * 0.5F &&
                    std::abs(local_mouse.y) <= handle.size.y * 0.5F
                : handle.joint ? distance >= 7.0F && distance <= 14.0F
                               : distance < 7.0F;
            const int priority = handle.joint ? 0 : handle.rectangle ? 2 : 1;
            if (contains && priority < hit_priority) {
                hit = cursor;
                hit_priority = priority;
            }
        }
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hit) {
        const auto& handle = handles[*hit];
        state.selected_node = handle.node;
        if (!handle.property.empty()) {
            state.spatial_drag_handle_node = handle.node;
            state.spatial_drag_node = handle.mutation_node;
            state.spatial_drag_kind = hit_kind;
            state.spatial_drag_property = hit_kind == MechanicSpatialDragKind::resize
                ? "size"
                : hit_kind == MechanicSpatialDragKind::rotate
                    ? "rotation" : handle.property;
            state.spatial_drag_start_value = handle.position;
            state.spatial_drag_start_size = handle.size;
            state.spatial_drag_start_rotation = handle.rotation;
            state.spatial_drag_start_mouse = mouse;
        }
    }

    for (const auto& handle : handles) {
        auto position = handle.position;
        auto handle_size = handle.size;
        auto rotation = handle.rotation;
        if ((state.spatial_drag_handle_node == handle.node ||
             state.spatial_drag_node == handle.node) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (state.spatial_drag_kind == MechanicSpatialDragKind::move) {
                position.x += (mouse.x - state.spatial_drag_start_mouse.x) /
                    state.spatial_zoom;
                position.y -= (mouse.y - state.spatial_drag_start_mouse.y) /
                    state.spatial_zoom;
            } else if (state.spatial_drag_kind == MechanicSpatialDragKind::resize) {
                const auto local = rotate_mechanic_vector(
                    subtract_mechanic_vectors(
                        to_world(mouse), state.spatial_drag_start_value),
                    -state.spatial_drag_start_rotation);
                handle_size = {std::max(0.1F, std::abs(local.x) * 2.0F),
                               std::max(0.1F, std::abs(local.y) * 2.0F)};
            } else if (state.spatial_drag_kind == MechanicSpatialDragKind::rotate) {
                const auto delta = subtract_mechanic_vectors(
                    to_world(mouse), state.spatial_drag_start_value);
                rotation = std::atan2(delta.y, delta.x) * 57.29577951308232F - 90.0F;
            }
        }
        const auto point = to_screen(position);
        if (probe && probe->enabled && !probe->spatial_handle_seen &&
            handle.node == "presence" && handle.property == "center") {
            probe->spatial_handle_screen = point;
            probe->spatial_handle_original = handle.position;
            probe->spatial_handle_node = handle.node;
            probe->spatial_handle_property = handle.property;
            probe->spatial_handle_seen = true;
        }
        if (probe && probe->enabled && handle.node == "platform") {
            probe->body_handle_screen = to_screen(add_mechanic_vectors(
                position, rotate_mechanic_vector(
                    {handle_size.x * 0.25F, 0.0F}, rotation)));
            probe->body_handle_seen = true;
        }
        if (probe && probe->enabled && handle.joint &&
            handle.node == "hinge") {
            probe->joint_handle_screen = {point.x + 10.0F, point.y};
            if (!probe->joint_handle_seen) {
                probe->joint_handle_original = handle.position;
                probe->joint_handle_node = handle.node;
                probe->joint_mutation_node = handle.mutation_node;
            }
            probe->joint_handle_seen = true;
        }
        const bool selected = state.selected_node == handle.node;
        const ImU32 color = selected ? IM_COL32(255, 190, 80, 255)
            : handle.joint ? IM_COL32(225, 110, 210, 255)
                           : IM_COL32(95, 190, 245, 230);
        if (handle.rectangle) {
            const fabric::core::Vec2 local_corners[4] = {
                {-handle_size.x * 0.5F, -handle_size.y * 0.5F},
                { handle_size.x * 0.5F, -handle_size.y * 0.5F},
                { handle_size.x * 0.5F,  handle_size.y * 0.5F},
                {-handle_size.x * 0.5F,  handle_size.y * 0.5F}};
            ImVec2 corners[4];
            for (std::size_t index = 0; index < 4U; ++index)
                corners[index] = to_screen(add_mechanic_vectors(
                    position,
                    rotate_mechanic_vector(local_corners[index], rotation)));
            draw->AddPolyline(corners, 4, color, ImDrawFlags_Closed,
                              selected ? 3.0F : 2.0F);
            if (selected) {
                const auto resize_screen = corners[2];
                draw->AddRectFilled(
                    {resize_screen.x - 5.0F, resize_screen.y - 5.0F},
                    {resize_screen.x + 5.0F, resize_screen.y + 5.0F}, color);
                if (probe && probe->enabled && handle.node == "platform") {
                    probe->resize_handle_screen = resize_screen;
                    if (!probe->resize_handle_seen)
                        probe->resize_handle_original = handle.size;
                    probe->resize_handle_seen = true;
                }
                if (handle.rotatable) {
                    const auto edge_screen = to_screen(add_mechanic_vectors(
                        position, rotate_mechanic_vector(
                            {0.0F, handle_size.y * 0.5F}, rotation)));
                    const auto rotate_screen = to_screen(add_mechanic_vectors(
                        position, rotate_mechanic_vector(
                            {0.0F, handle_size.y * 0.5F +
                                      24.0F / state.spatial_zoom},
                            rotation)));
                    draw->AddLine(edge_screen, rotate_screen, color, 1.5F);
                    draw->AddCircleFilled(rotate_screen, 6.0F, color, 16);
                    if (probe && probe->enabled && handle.node == "platform") {
                        probe->rotation_handle_screen = rotate_screen;
                        if (!probe->rotation_handle_seen)
                            probe->rotation_handle_original = handle.rotation;
                        probe->rotation_handle_seen = true;
                    }
                }
            }
        } else {
            draw->AddCircle(point, handle.joint ? 10.0F : 7.0F,
                            color, 20, selected ? 3.0F : 2.0F);
            draw->AddLine({point.x - 12.0F, point.y},
                          {point.x + 12.0F, point.y}, color, 1.5F);
            draw->AddLine({point.x, point.y - 12.0F},
                          {point.x, point.y + 12.0F}, color, 1.5F);
        }
        draw->AddText({point.x + 8.0F, point.y + 8.0F}, color,
                      handle.node.c_str());
    }
    draw->PopClipRect();

    if (!state.spatial_drag_node.empty() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        bool changed = false;
        if (state.spatial_drag_kind == MechanicSpatialDragKind::rotate) {
            const auto delta = subtract_mechanic_vectors(
                to_world(mouse), state.spatial_drag_start_value);
            const float value = std::atan2(delta.y, delta.x) *
                57.29577951308232F - 90.0F;
            changed = value != state.spatial_drag_start_rotation &&
                session.set_node_property({.value = state.spatial_drag_node},
                                          state.spatial_drag_property, value);
            if (probe && probe->enabled && changed)
                probe->rotation_handle_moved = true;
        } else {
            fabric::core::Vec2 value{};
            fabric::core::Vec2 original{};
            if (state.spatial_drag_kind == MechanicSpatialDragKind::resize) {
                const auto local = rotate_mechanic_vector(
                    subtract_mechanic_vectors(
                        to_world(mouse), state.spatial_drag_start_value),
                    -state.spatial_drag_start_rotation);
                value = {std::max(0.1F, std::abs(local.x) * 2.0F),
                         std::max(0.1F, std::abs(local.y) * 2.0F)};
                original = state.spatial_drag_start_size;
            } else {
                value = {
                    state.spatial_drag_start_value.x +
                        (mouse.x - state.spatial_drag_start_mouse.x) /
                            state.spatial_zoom,
                    state.spatial_drag_start_value.y -
                        (mouse.y - state.spatial_drag_start_mouse.y) /
                            state.spatial_zoom};
                original = state.spatial_drag_start_value;
            }
            changed = value != original && session.set_node_property(
                {.value = state.spatial_drag_node},
                state.spatial_drag_property, value);
            if (probe && probe->enabled && changed) {
                if (state.spatial_drag_kind == MechanicSpatialDragKind::resize)
                    probe->resize_handle_moved = true;
                else if (state.spatial_drag_handle_node ==
                         probe->joint_handle_node)
                    probe->joint_handle_moved = true;
                else
                    probe->spatial_handle_moved = true;
            }
        }
        status = changed ? "Mechanic transform changed"
                         : "Mechanic transform unchanged or rejected";
        state.spatial_drag_handle_node.clear();
        state.spatial_drag_node.clear();
        state.spatial_drag_property.clear();
        state.spatial_drag_kind = MechanicSpatialDragKind::none;
        return changed;
    }
    return false;
}

} // namespace

MechanicMapOverlayResult draw_mechanic_map_overlay(
    fabric::editor::MapSession& map_session,
    fabric::editor::MechanicSession& mechanic_session,
    MapMechanicOverlayState& state,
    const std::string& selected_instance_id,
    const ImVec2 canvas_center,
    const ImVec2 pan,
    const float zoom,
    const bool hovered,
    const bool interaction_blocked,
    std::string& status,
    MechanicMapOverlayProbe* probe) {
    MechanicMapOverlayResult result{.pointer_captured = state.active};
    if (!map_session.map() || selected_instance_id.empty()) return result;
    const auto& map = *map_session.map();
    if (mechanic_session.preview_instance_id())
        static_cast<void>(mechanic_session.sync_preview_instance(map));
    if (mechanic_session.simulation().playing() ||
        mechanic_session.simulation().step_count() != 0U) {
        mechanic_session.pause();
        if (!mechanic_session.reset_preview()) {
            status = "Mechanic authoring pose could not reset";
            return result;
        }
    }
    const auto& preview = mechanic_session.simulation();
    if (!preview.valid() || !mechanic_session.preview_instance_id() ||
        mechanic_session.preview_instance_id()->value != selected_instance_id)
        return result;
    if (probe && probe->enabled) probe->overlay_seen = true;
    const auto instance = std::ranges::find(
        map.instances, selected_instance_id,
        &fabric::project::MapInstance::id);
    if (instance == map.instances.end() || !instance->prefab) return result;
    const bool layer_locked = std::ranges::any_of(
        map.layers, [&](const auto& layer) {
            return layer.id == instance->layer_id && layer.locked;
        });
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{canvas_center.x + pan.x + point.x * zoom,
                      canvas_center.y + pan.y - point.y * zoom};
    };
    const auto to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{
            (point.x - canvas_center.x - pan.x) / zoom,
            -(point.y - canvas_center.y - pan.y) / zoom};
    };
    const auto inverse_transform_point = [&](fabric::core::Vec2 point) {
        point = subtract_mechanic_vectors(point, instance->transform.position);
        point = rotate_mechanic_vector(
            point, -instance->transform.rotation_degrees);
        point.x = point.x / instance->transform.scale.x +
            instance->transform.pivot.x;
        point.y = point.y / instance->transform.scale.y +
            instance->transform.pivot.y;
        return point;
    };
    const auto parameter_for = [&](const std::string_view node_id,
                                   const std::string_view property_id) {
        if (!mechanic_session.graph()) return std::string{};
        const auto parameter = std::ranges::find_if(
            mechanic_session.graph()->parameters, [&](const auto& candidate) {
                return candidate.target_node == node_id &&
                    candidate.target_property == property_id;
            });
        return parameter == mechanic_session.graph()->parameters.end()
            ? std::string{} : parameter->id;
    };
    struct Handle {
        std::string node_id;
        std::string move_parameter;
        std::string size_parameter;
        std::string rotation_parameter;
        fabric::core::Vec2 position{};
        fabric::core::Vec2 size{};
        float rotation{};
        bool sensor{};
    };
    std::vector<Handle> handles;
    for (const auto& body : preview.body_states())
        handles.push_back({
            .node_id = body.node_id,
            .move_parameter = parameter_for(body.node_id, "position"),
            .size_parameter = parameter_for(body.node_id, "size"),
            .rotation_parameter = parameter_for(body.node_id, "rotation"),
            .position = body.position,
            .size = body.size,
            .rotation = body.rotation_degrees});
    for (const auto& sensor : preview.sensor_states())
        handles.push_back({
            .node_id = sensor.node_id,
            .move_parameter = parameter_for(sensor.node_id, "center"),
            .size_parameter = parameter_for(sensor.node_id, "size"),
            .position = sensor.position,
            .size = sensor.size,
            .rotation = sensor.rotation_degrees,
            .sensor = true});
    const auto& io = ImGui::GetIO();
    if (state.active && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        state.active = false;
        state.parameter_id.clear();
        state.kind = MapMechanicDragKind::none;
        result.pointer_captured = false;
        status = "Mechanic parameter gesture cancelled";
        return result;
    }
    const auto current_values = [&](const Handle& handle) {
        auto position = handle.position;
        auto size = handle.size;
        auto rotation = handle.rotation;
        if (state.active && state.selected_node == handle.node_id) {
            const auto world_mouse = to_world(io.MousePos);
            if (state.kind == MapMechanicDragKind::move) {
                position = world_mouse;
            } else if (state.kind == MapMechanicDragKind::resize) {
                const auto local_delta = rotate_mechanic_vector(
                    subtract_mechanic_vectors(world_mouse, state.start_position),
                    -state.start_rotation);
                size = {std::max(0.1F, std::abs(local_delta.x) * 2.0F),
                        std::max(0.1F, std::abs(local_delta.y) * 2.0F)};
            } else if (state.kind == MapMechanicDragKind::rotate) {
                const auto delta = subtract_mechanic_vectors(
                    world_mouse, state.start_position);
                rotation = std::atan2(delta.y, delta.x) *
                    57.29577951308232F - 90.0F;
            }
        }
        return std::tuple{position, size, rotation};
    };
    if (hovered && !layer_locked && !interaction_blocked &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::optional<std::size_t> hit;
        auto hit_kind = MapMechanicDragKind::none;
        for (std::size_t index = 0; index < handles.size(); ++index) {
            const auto& handle = handles[index];
            if (state.selected_node != handle.node_id) continue;
            const auto corner = add_mechanic_vectors(
                handle.position, rotate_mechanic_vector(
                    {handle.size.x * 0.5F, handle.size.y * 0.5F},
                    handle.rotation));
            const auto corner_screen = to_screen(corner);
            if (!handle.size_parameter.empty() &&
                std::hypot(io.MousePos.x - corner_screen.x,
                           io.MousePos.y - corner_screen.y) <= 10.0F) {
                hit = index;
                hit_kind = MapMechanicDragKind::resize;
                break;
            }
            if (!handle.rotation_parameter.empty()) {
                const auto rotate_point = add_mechanic_vectors(
                    handle.position, rotate_mechanic_vector(
                        {0.0F, handle.size.y * 0.5F + 24.0F / zoom},
                        handle.rotation));
                const auto rotate_screen = to_screen(rotate_point);
                if (std::hypot(io.MousePos.x - rotate_screen.x,
                               io.MousePos.y - rotate_screen.y) <= 10.0F) {
                    hit = index;
                    hit_kind = MapMechanicDragKind::rotate;
                    break;
                }
            }
        }
        if (!hit) {
            for (std::size_t index = 0; index < handles.size(); ++index) {
                const auto& handle = handles[index];
                const auto local_mouse = rotate_mechanic_vector(
                    subtract_mechanic_vectors(to_world(io.MousePos),
                                              handle.position),
                    -handle.rotation);
                if (std::abs(local_mouse.x) <= handle.size.x * 0.5F &&
                    std::abs(local_mouse.y) <= handle.size.y * 0.5F) {
                    hit = index;
                    hit_kind = handle.move_parameter.empty()
                        ? MapMechanicDragKind::none
                        : MapMechanicDragKind::move;
                }
            }
        }
        if (hit) {
            const auto& handle = handles[*hit];
            state.selected_node = handle.node_id;
            result.pointer_captured = true;
            if (hit_kind == MapMechanicDragKind::none) {
                status = "Mechanic shape selected; highlighted handles edit all prefab instances";
            } else {
                state.active = true;
                state.kind = hit_kind;
                state.parameter_id = hit_kind == MapMechanicDragKind::move
                    ? handle.move_parameter
                    : hit_kind == MapMechanicDragKind::resize
                        ? handle.size_parameter : handle.rotation_parameter;
                state.start_position = handle.position;
                state.start_rotation = handle.rotation;
            }
        }
    }
    if (state.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const auto handle = std::ranges::find(
            handles, state.selected_node, &Handle::node_id);
        bool committed = false;
        bool preview_refreshed = false;
        if (handle != handles.end()) {
            const auto [position, size, rotation] = current_values(*handle);
            fabric::project::MechanicValue value;
            if (state.kind == MapMechanicDragKind::move)
                value = inverse_transform_point(position);
            else if (state.kind == MapMechanicDragKind::resize)
                value = fabric::core::Vec2{
                    size.x / instance->transform.scale.x,
                    size.y / instance->transform.scale.y};
            else
                value = rotation - instance->transform.rotation_degrees;
            committed = map_session.set_prefab_mechanic_override(
                instance->prefab->id, {state.parameter_id, std::move(value)});
            if (committed) {
                const auto refreshed_map = *map_session.map();
                preview_refreshed =
                    mechanic_session.sync_preview_instance(refreshed_map);
            }
        }
        if (probe && probe->enabled && committed && preview_refreshed)
            probe->parameter_handle_moved = true;
        status = committed && preview_refreshed
            ? "Prefab mechanic parameter changed for all its instances"
            : committed
                ? "Prefab mechanic changed, but its preview could not rebuild"
                : "Mechanic parameter unchanged or rejected";
        state.active = false;
        state.parameter_id.clear();
        state.kind = MapMechanicDragKind::none;
        result.map_changed = committed;
        return result;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const auto draw_box = [&](const fabric::core::Vec2 position,
                              const fabric::core::Vec2 size,
                              const float rotation,
                              const ImU32 fill,
                              const ImU32 outline) {
        const fabric::core::Vec2 corners[4] = {
            {-size.x * 0.5F, -size.y * 0.5F},
            { size.x * 0.5F, -size.y * 0.5F},
            { size.x * 0.5F,  size.y * 0.5F},
            {-size.x * 0.5F,  size.y * 0.5F}};
        ImVec2 points[4];
        for (std::size_t index = 0; index < 4U; ++index)
            points[index] = to_screen(add_mechanic_vectors(
                position, rotate_mechanic_vector(corners[index], rotation)));
        draw->AddQuadFilled(points[0], points[1], points[2], points[3], fill);
        draw->AddQuad(points[0], points[1], points[2], points[3], outline, 2.0F);
    };
    for (const auto& body : preview.body_states())
        draw_box(body.position, body.size, body.rotation_degrees,
                 IM_COL32(75, 165, 180, 42), IM_COL32(90, 220, 235, 235));
    for (const auto& sensor : preview.sensor_states())
        draw_box(sensor.position, sensor.size, sensor.rotation_degrees,
                 sensor.active ? IM_COL32(105, 235, 135, 58)
                               : IM_COL32(240, 190, 80, 35),
                 sensor.active ? IM_COL32(105, 235, 135, 245)
                               : IM_COL32(240, 190, 80, 220));
    if (const auto character = preview.preview_character_state())
        draw_box(character->position, character->size,
                 character->rotation_degrees,
                 IM_COL32(225, 105, 190, 72),
                 IM_COL32(255, 145, 220, 245));
    for (const auto& handle : handles) {
        const auto [position, size, rotation] = current_values(handle);
        if (probe && probe->enabled && !handle.sensor) {
            probe->parameter_body_screen = to_screen(add_mechanic_vectors(
                position, rotate_mechanic_vector(
                    {size.x * 0.25F, -size.y * 0.4F}, rotation)));
            probe->parameter_body_seen = true;
        }
        if (state.selected_node != handle.node_id) continue;
        draw_box(position, size, rotation, IM_COL32(255, 180, 70, 24),
                 IM_COL32(255, 190, 80, 255));
        if (!handle.size_parameter.empty()) {
            const auto point = to_screen(add_mechanic_vectors(
                position, rotate_mechanic_vector(
                    {size.x * 0.5F, size.y * 0.5F}, rotation)));
            draw->AddRectFilled({point.x - 6.0F, point.y - 6.0F},
                                {point.x + 6.0F, point.y + 6.0F},
                                IM_COL32(255, 190, 80, 255));
            if (probe && probe->enabled && !handle.sensor) {
                if (!probe->parameter_handle_seen)
                    probe->parameter_original_size = handle.size;
                probe->parameter_handle_seen = true;
                probe->parameter_handle_screen = point;
            }
        }
        if (!handle.rotation_parameter.empty()) {
            const auto edge = to_screen(add_mechanic_vectors(
                position, rotate_mechanic_vector(
                    {0.0F, size.y * 0.5F}, rotation)));
            const auto point = to_screen(add_mechanic_vectors(
                position, rotate_mechanic_vector(
                    {0.0F, size.y * 0.5F + 24.0F / zoom}, rotation)));
            draw->AddLine(edge, point, IM_COL32(255, 190, 80, 255), 1.5F);
            draw->AddCircleFilled(point, 6.0F,
                                  IM_COL32(255, 190, 80, 255));
        }
    }
    return result;
}

void draw_mechanic_value_editor(
    fabric::editor::MechanicSession& session,
    const fabric::project::MechanicNodeDefinition& node,
    const fabric::project::MechanicNodeProperty& property,
    std::string& status) {
    ImGui::PushID(property.id.c_str());
    auto value = property.value;
    bool changed = false;
    const std::string numeric_label = property.id +
        " (declared units)##numeric-" + property.id;
    std::visit([&](auto& item) {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, bool>) {
            changed = ImGui::Checkbox(property.id.c_str(), &item);
        } else if constexpr (std::is_same_v<Value, std::int64_t>) {
            auto edited = static_cast<int>(item);
            if (ImGui::InputInt(numeric_label.c_str(), &edited) &&
                ImGui::IsItemDeactivatedAfterEdit()) {
                item = edited;
                changed = true;
            }
            ImGui::SetItemTooltip("Integer value interpreted using the mechanic property schema.");
        } else if constexpr (std::is_same_v<Value, float>) {
            changed = ImGui::DragFloat(numeric_label.c_str(), &item, 0.1F) &&
                      ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("Real value interpreted using the mechanic property schema.");
        } else if constexpr (std::is_same_v<Value, std::string>) {
            changed = ImGui::InputText(property.id.c_str(), &item) &&
                      ImGui::IsItemDeactivatedAfterEdit();
        } else if constexpr (std::is_same_v<Value, fabric::core::Vec2>) {
            changed = ImGui::DragFloat2(numeric_label.c_str(), &item.x, 0.1F) &&
                      ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("Vector value interpreted using the mechanic property schema.");
        } else {
            changed = ImGui::InputText(property.id.c_str(), &item.id.value) &&
                      ImGui::IsItemDeactivatedAfterEdit();
        }
    }, value);
    if (changed) {
        status = session.set_node_property(
            {.value = node.id}, property.id, std::move(value))
            ? "Mechanic property changed"
            : "Mechanic property rejected";
    }
    ImGui::PopID();
}

void draw_mechanic_workspace(fabric::editor::MechanicSession& session,
                          const fabric::editor::MapSession& map_session,
                          MechanicWorkspaceState& state,
                          std::string& status,
                          fabric::editor::ProjectSession& resource_catalog,
                          MechanicWorkspaceProbe* probe) {
    ImGui::SeparatorText("Mechanics");
    if (!map_session.map()) {
        ImGui::TextDisabled("Open a map before editing mechanics.");
        return;
    }

    std::filesystem::path directory;
    if (map_session.manifest()) {
        directory = map_session.project_root() /
            map_session.manifest()->directories.assets / "mechanics";
        std::error_code error;
        if (std::filesystem::exists(directory, error)) {
            ImGui::TextDisabled("Project mechanics:");
            for (std::filesystem::directory_iterator iterator{directory, error}, end;
                 !error && iterator != end; iterator.increment(error)) {
                if (!iterator->is_regular_file(error)) continue;
                auto filename = iterator->path().filename().string();
                constexpr std::string_view suffix = ".mechanic.json";
                if (!filename.ends_with(suffix)) continue;
                filename.resize(filename.size() - suffix.size());
                ImGui::SameLine();
                if (ImGui::SmallButton(filename.c_str())) state.open_id = filename;
            }
        }
    }

    draw_resource_picker("Mechanics:", directory, ".mechanic.json",
                         state.open_id, &resource_catalog);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.open_id.empty());
    if (ImGui::Button("Open")) {
        status = session.open(map_session.project_root(), *map_session.map(),
                              {.value = state.open_id})
            ? "Mechanic opened" : "Mechanic could not be opened";
        state.selected_node.clear();
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.open_id.empty(),
                         "Choose an existing mechanic graph first.");
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputText("New id", &state.new_id);
    focus_first_field_error(session.errors(), "id", "mechanic-create");
    ImGui::SameLine();
    draw_resource_name_field("New name", state.new_name, 160.0F);
    focus_first_field_error(session.errors(), "name", "mechanic-create");
    ImGui::SameLine();
    ImGui::BeginDisabled(state.new_id.empty() || state.new_name.empty());
    if (ImGui::Button("Create")) {
        fabric::project::MechanicGraph graph;
        graph.document.id = {.value = state.new_id};
        graph.document.name = state.new_name;
        const bool created = session.create(
            map_session.project_root(), *map_session.map(), graph);
        status = created ? "Mechanic created" : "Mechanic creation rejected";
        if (created) {
            static_cast<void>(resource_catalog.refresh_resources());
            state.open_id = state.new_id;
            state.new_id.clear();
            state.new_name.clear();
            state.selected_node.clear();
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.new_id.empty() || state.new_name.empty(),
                         "Enter both a mechanic id and a mechanic name.");

    if (ImGui::CollapsingHeader("Create rotating platform preset")) {
    ImGui::SetNextItemWidth(150.0F);
    ImGui::InputText("Platform id", &state.platform.id.value);
    focus_first_field_error(session.errors(), "id", "platform-create");
    ImGui::SameLine();
    draw_resource_name_field("Platform name", state.platform.name, 170.0F);
    focus_first_field_error(session.errors(), "name", "platform-create");
    ImGui::Combo("Activation", &state.platform_activation,
                 "Presence sensor\0Map event\0");
    if (state.platform_activation == 1) {
        std::vector<std::string> event_ids;
        event_ids.reserve(map_session.map()->events.size());
        for (const auto& event : map_session.map()->events)
            event_ids.push_back(event.id.value);
        draw_id_picker("Activation event", event_ids, state.platform.event_id.value,
                       "Choose a map event...");
    }
    if (map_session.manifest()) {
        const auto directory = map_session.project_root() /
            map_session.manifest()->directories.entities;
        draw_resource_picker("Visual entity (optional):", directory, ".entity.json",
                             state.platform_visual_entity, &resource_catalog);
    }
    ImGui::DragFloat2("Platform position (world units)",
                      &state.platform.position.x, 0.1F);
    draw_technical_tooltip("Initial platform position in map world space.");
    ImGui::DragFloat2("Platform size (world units)", &state.platform.size.x,
                      0.1F, 0.01F, 256.0F);
    draw_technical_tooltip("Platform dimensions used by the rotating body.");
    ImGui::DragFloat("Speed (deg/s)",
                     &state.platform.speed_degrees_per_second, 1.0F, 0.0F, 3600.0F);
    draw_technical_tooltip("Angular velocity applied to the platform.");
    ImGui::Combo("Direction", &state.platform_direction,
                 "Counter-clockwise (+1)\0Clockwise (-1)\0");
    ImGui::DragFloat("Acceleration (deg/s2)",
                     &state.platform.acceleration_degrees_per_second_squared,
                     1.0F, 0.0F, 7200.0F);
    draw_technical_tooltip("Angular acceleration used to reach the target speed.");
    ImGui::DragFloat("Maximum torque (force)", &state.platform.maximum_torque,
                     1.0F, 0.0F, 100000.0F);
    draw_technical_tooltip("Maximum force available to drive the platform.");
    ImGui::Checkbox("Angular limits", &state.platform.limit_enabled);
    if (state.platform.limit_enabled) {
        ImGui::DragFloat("Minimum angle (degrees)", &state.platform.minimum_angle_degrees,
                         1.0F, -178.0F, 178.0F);
        draw_technical_tooltip("Lower angular limit when limits are enabled.");
        ImGui::DragFloat("Maximum angle (degrees)", &state.platform.maximum_angle_degrees,
                         1.0F, -178.0F, 178.0F);
        draw_technical_tooltip("Upper angular limit when limits are enabled.");
    }
    if (state.platform_activation == 0) {
        ImGui::DragFloat2("Sensor center (world units)",
                          &state.platform.sensor_center.x, 0.1F);
        draw_technical_tooltip("Center of the sensor activation area.");
        ImGui::DragFloat2("Sensor size (world units)", &state.platform.sensor_size.x,
                          0.1F, 0.01F, 256.0F);
        draw_technical_tooltip("Dimensions of the sensor activation area.");
    }
    if (ImGui::Button("Create rotating platform")) {
        state.platform.activation = state.platform_activation == 0
            ? fabric::editor::RotatingPlatformActivation::sensor
            : fabric::editor::RotatingPlatformActivation::event;
        state.platform.direction = state.platform_direction == 0 ? 1 : -1;
        state.platform.visual_entity = state.platform_visual_entity.empty()
            ? std::nullopt
            : std::optional<fabric::project::ResourceReference>{
                  {{.value = state.platform_visual_entity}, "entity"}};
        const auto built = fabric::editor::build_rotating_platform_preset(
            *map_session.manifest(), *map_session.map(), state.platform);
        if (!built.ok()) {
            status = built.errors.empty() ? "Platform preset rejected"
                                          : built.errors.front().message;
        } else if (session.create(map_session.project_root(), *map_session.map(),
                                  *built.graph)) {
            state.open_id = state.platform.id.value;
            state.selected_node = "platform";
            status = "Rotating platform created from Studio preset";
        } else {
            status = session.errors().empty()
                ? "Platform could not replace the current dirty graph"
                : session.errors().front().message;
        }
    }
    }

    if (!session.has_graph()) {
        for (const auto& error : session.errors())
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s: %s",
                               error.field.c_str(), error.message.c_str());
        return;
    }
    if (state.pending_canvas_connection) {
        status = session.connect({state.from_node, state.from_port,
                                  state.to_node, state.to_port})
            ? "Ports connected from canvas"
            : "Canvas connection rejected (types, direction or cycle)";
        state.pending_canvas_connection = false;
        if (status == "Ports connected from canvas")
            state.canvas_connection_source.clear();
    }
    const auto& graph = *session.graph();
    if (session.has_recovery()) {
        ImGui::TextColored({1.0F, 0.75F, 0.25F, 1.0F},
                           "A newer valid mechanic autosave is available.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Recover graph"))
            status = session.accept_recovery() ? "Mechanic autosave recovered"
                                                : "Mechanic recovery failed";
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss graph recovery")) {
            session.decline_recovery();
            status = "Mechanic recovery dismissed";
        }
    }
    ImGui::SeparatorText(graph.document.name.c_str());
    ImGui::TextColored(session.dirty() ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
                                       : ImVec4{0.45F, 0.9F, 0.55F, 1.0F},
                       session.dirty() ? "dirty" : "saved");
    ImGui::SameLine();
    if (ImGui::Button("Save graph"))
        status = session.save() ? "Mechanic saved" : "Mechanic save failed";
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.can_undo());
    if (ImGui::Button("Undo graph")) static_cast<void>(session.undo());
    ImGui::EndDisabled();
    draw_disabled_reason(!session.can_undo(),
                         "No mechanic changes are available to undo.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.can_redo());
    if (ImGui::Button("Redo graph")) static_cast<void>(session.redo());
    ImGui::EndDisabled();
    draw_disabled_reason(!session.can_redo(),
                         "No undone mechanic changes are available to redo.");

    ImGui::Columns(2, "mechanic-columns", true);
    ImGui::SeparatorText("Graph");
    ImGui::TextDisabled(
        "Choose Connect from output, then click a compatible destination.");
    if (ImGui::BeginChild("Mechanic graph canvas", {0.0F, 260.0F},
                          ImGuiChildFlags_Borders)) {
        if ((probe != nullptr && probe->enabled))
            probe->canvas_seen = true;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        constexpr float card_width = 174.0F;
        constexpr float card_height = 78.0F;
        constexpr float cell_width = 202.0F;
        constexpr float cell_height = 126.0F;
        const int columns = std::max(1, static_cast<int>(
            ImGui::GetContentRegionAvail().x / cell_width));
        std::vector<ImVec2> positions;
        positions.reserve(graph.nodes.size());
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            positions.push_back({
                14.0F + static_cast<float>(static_cast<int>(index) % columns) *
                    cell_width,
                14.0F + static_cast<float>(static_cast<int>(index) / columns) *
                    cell_height});
        }
        auto* draw = ImGui::GetWindowDrawList();
        const ImU32 line_color = ImGui::GetColorU32(ImGuiCol_PlotLines);
        for (const auto& connection : graph.connections) {
            if ((probe != nullptr && probe->enabled) &&
                connection == probe->expected_connection)
                probe->link_seen = true;
            const auto source = std::ranges::find(
                graph.nodes, connection.from_node,
                &fabric::project::MechanicNodeDefinition::id);
            const auto target = std::ranges::find(
                graph.nodes, connection.to_node,
                &fabric::project::MechanicNodeDefinition::id);
            if (source == graph.nodes.end() || target == graph.nodes.end())
                continue;
            const auto source_index = static_cast<std::size_t>(
                std::distance(graph.nodes.begin(), source));
            const auto target_index = static_cast<std::size_t>(
                std::distance(graph.nodes.begin(), target));
            const bool same_column =
                positions[source_index].x == positions[target_index].x;
            if (same_column) {
                const bool downward = positions[target_index].y >
                    positions[source_index].y;
                const ImVec2 start{
                    origin.x + positions[source_index].x + card_width * 0.5F,
                    origin.y + positions[source_index].y +
                        (downward ? card_height : 0.0F)};
                const ImVec2 end{
                    origin.x + positions[target_index].x + card_width * 0.5F,
                    origin.y + positions[target_index].y +
                        (downward ? 0.0F : card_height)};
                const float bend = std::max(28.0F, std::abs(end.y - start.y) * 0.4F);
                const float direction = downward ? 1.0F : -1.0F;
                draw->AddBezierCubic(start,
                    {start.x, start.y + direction * bend},
                    {end.x, end.y - direction * bend}, end, line_color, 2.0F);
                draw->AddTriangleFilled(end,
                    {end.x - 5.0F, end.y - direction * 9.0F},
                    {end.x + 5.0F, end.y - direction * 9.0F}, line_color);
            } else {
                const ImVec2 start{
                    origin.x + positions[source_index].x + card_width,
                    origin.y + positions[source_index].y + card_height * 0.5F};
                const ImVec2 end{
                    origin.x + positions[target_index].x,
                    origin.y + positions[target_index].y + card_height * 0.5F};
                const float bend =
                    std::max(36.0F, std::abs(end.x - start.x) * 0.4F);
                draw->AddBezierCubic(start, {start.x + bend, start.y},
                                     {end.x - bend, end.y}, end,
                                     line_color, 2.0F);
                draw->AddTriangleFilled(end, {end.x - 8.0F, end.y - 5.0F},
                                        {end.x - 8.0F, end.y + 5.0F}, line_color);
            }
        }
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            const auto& node = graph.nodes[index];
            const auto output = std::ranges::find(
                node.ports, fabric::project::MechanicPortDirection::output,
                &fabric::project::MechanicPortDefinition::direction);
            const auto input = std::ranges::find(
                node.ports, fabric::project::MechanicPortDirection::input,
                &fabric::project::MechanicPortDefinition::direction);
            ImGui::SetCursorScreenPos(
                {origin.x + positions[index].x, origin.y + positions[index].y});
            ImGui::PushID(node.id.c_str());
            std::string label = node.type + "\n" + node.id;
            if (input != node.ports.end()) label += "\nin: " + input->id;
            if (output != node.ports.end()) label += "\nout: " + output->id;
            if (ImGui::Button(label.c_str(), {card_width, card_height})) {
                state.selected_node = node.id;
                if ((probe != nullptr && probe->enabled) &&
                    node.id == probe->expected_connection.to_node)
                    probe->target_clicked = true;
                if (!state.canvas_connection_source.empty() &&
                    state.canvas_connection_source != node.id) {
                    const auto source = std::ranges::find(
                        graph.nodes, state.canvas_connection_source,
                        &fabric::project::MechanicNodeDefinition::id);
                    if (source != graph.nodes.end()) {
                        const auto source_port = std::ranges::find(
                            source->ports,
                            fabric::project::MechanicPortDirection::output,
                            &fabric::project::MechanicPortDefinition::direction);
                        const auto target_port = std::ranges::find_if(
                            node.ports, [&](const auto& port) {
                                return source_port != source->ports.end() &&
                                    port.direction ==
                                        fabric::project::MechanicPortDirection::input &&
                                    port.type == source_port->type;
                            });
                        if (source_port != source->ports.end() &&
                            target_port != node.ports.end()) {
                            state.from_node = source->id;
                            state.from_port = source_port->id;
                            state.to_node = node.id;
                            state.to_port = target_port->id;
                            state.pending_canvas_connection = true;
                        } else status = "No compatible mechanic input on target.";
                    }
                }
            }
            if ((probe != nullptr && probe->enabled) &&
                node.id == probe->expected_connection.to_node) {
                probe->target_seen = true;
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->target_screen = {
                    (minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F};
            }
            ImGui::BeginDisabled(output == node.ports.end());
            if (state.canvas_connection_source == node.id) {
                if (ImGui::SmallButton("Cancel connection"))
                    state.canvas_connection_source.clear();
            } else if (ImGui::SmallButton("Connect from output")) {
                state.canvas_connection_source = node.id;
                state.selected_node = node.id;
                if ((probe != nullptr && probe->enabled) &&
                    node.id == probe->expected_connection.from_node)
                    probe->source_clicked = true;
            }
            ImGui::EndDisabled();
            if ((probe != nullptr && probe->enabled) &&
                node.id == probe->expected_connection.from_node) {
                probe->source_seen = true;
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->source_screen = {
                    (minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F};
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    for (const auto& node : graph.nodes) {
        const auto label = node.id + " [" + node.type + "]";
        if (ImGui::Selectable(label.c_str(), state.selected_node == node.id))
            state.selected_node = node.id;
    }
    ImGui::Combo("Node type", &state.new_node_kind,
                 "body\0pivot\0joint\0motor\0sensor\0constraint\0event\0");
    ImGui::InputText("Node id", &state.new_node_id);
    ImGui::BeginDisabled(state.new_node_id.empty());
    if (ImGui::Button("Add node")) {
        status = session.add_node(
            static_cast<fabric::project::MechanicNodeKind>(state.new_node_kind),
            state.new_node_id) ? "Mechanic node added" : "Mechanic node rejected";
        if (status == "Mechanic node added") {
            state.selected_node = state.new_node_id;
            state.new_node_id.clear();
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.new_node_id.empty(),
                         "Enter a unique node id before adding a node.");

    ImGui::SeparatorText("Connections");
    for (std::size_t index = 0; index < graph.connections.size(); ++index) {
        const auto& connection = graph.connections[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextWrapped("%s.%s -> %s.%s", connection.from_node.c_str(),
                           connection.from_port.c_str(), connection.to_node.c_str(),
                           connection.to_port.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Disconnect")) {
            status = session.disconnect(index) ? "Connection removed"
                                               : "Connection removal rejected";
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    static_cast<void>(draw_mechanic_node_picker(
        "From node", graph.nodes, state.from_node));
    static_cast<void>(draw_mechanic_port_picker(
        "From port", graph.nodes, state.from_node, state.from_port,
        fabric::project::MechanicPortDirection::output));
    static_cast<void>(draw_mechanic_node_picker(
        "To node", graph.nodes, state.to_node));
    static_cast<void>(draw_mechanic_port_picker(
        "To port", graph.nodes, state.to_node, state.to_port,
        fabric::project::MechanicPortDirection::input));
    if (ImGui::Button("Connect ports")) {
        status = session.connect({state.from_node, state.from_port,
                                  state.to_node, state.to_port})
            ? "Ports connected" : "Connection rejected (types, direction or cycle)";
    }

    ImGui::NextColumn();
    if (draw_mechanic_spatial_canvas(session, state, status, probe)) {
        ImGui::Columns(1);
        return;
    }
    ImGui::SeparatorText("Inspector");
    const auto selected = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [&](const auto& node) { return node.id == state.selected_node; });
    if (selected != graph.nodes.end()) {
        const auto selected_node = *selected;
        ImGui::Text("%s [%s]", selected_node.id.c_str(), selected_node.type.c_str());
        for (const auto& property : selected_node.properties)
            draw_mechanic_value_editor(session, selected_node, property, status);
        if (ImGui::Button("Remove selected node")) {
            status = session.remove_node({.value = selected_node.id})
                ? "Mechanic node removed" : "Mechanic node removal rejected";
            if (status == "Mechanic node removed") state.selected_node.clear();
        }
        ImGui::TextDisabled("Ports:");
        for (const auto& port : selected_node.ports)
            ImGui::BulletText("%s · %s · %s", port.id.c_str(),
                port.direction == fabric::project::MechanicPortDirection::input
                    ? "input" : "output",
                std::string{fabric::project::to_string(port.type)}.c_str());
    } else {
        ImGui::TextDisabled("Select a node to edit its properties.");
    }

    ImGui::SeparatorText("Simulation");
    const auto& simulation = session.simulation();
    ImGui::BeginDisabled(!simulation.valid());
    if (ImGui::Button(simulation.playing() ? "Pause" : "Play")) {
        if (simulation.playing()) session.pause(); else session.play();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(simulation.playing());
    if (ImGui::Button("Step 1/60"))
        status = session.step_once() ? "Simulation advanced one fixed step"
                                     : "Simulation step rejected";
    ImGui::EndDisabled();
    draw_disabled_reason(simulation.playing(),
                         "Pause the simulation before advancing a single step.");
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        status = session.reset_preview() ? "Simulation reset"
                                         : "Simulation reset failed";
    ImGui::EndDisabled();
    draw_disabled_reason(!simulation.valid(),
                         "Load a valid mechanic graph before starting the simulation.");
    ImGui::Text("Fixed steps: %zu", simulation.step_count());
    ImGui::SeparatorText("Preview character");
    ImGui::DragFloat2("Character position (world units)",
                      &state.preview_character.position.x,
                      0.1F);
    draw_technical_tooltip("Preview character position in map world space.");
    ImGui::DragFloat2("Character size (world units)", &state.preview_character.size.x,
                      0.05F, 0.05F, 16.0F);
    draw_technical_tooltip("Preview character collider dimensions.");
    ImGui::DragFloat("Character friction (coefficient)",
                     &state.preview_character.friction,
                     0.05F, 0.0F, 4.0F);
    draw_technical_tooltip("Friction coefficient used by the preview controller.");
    if (ImGui::Button("Place / reset character"))
        status = session.place_preview_character(state.preview_character)
            ? "Preview character placed" : "Preview character rejected";
    ImGui::SameLine();
    if (ImGui::Button("Move left"))
        static_cast<void>(session.set_preview_character_velocity(
            {-state.preview_character_speed, 0.0F}));
    ImGui::SameLine();
    if (ImGui::Button("Stop character"))
        static_cast<void>(session.set_preview_character_velocity({0.0F, 0.0F}));
    ImGui::SameLine();
    if (ImGui::Button("Move right"))
        static_cast<void>(session.set_preview_character_velocity(
            {state.preview_character_speed, 0.0F}));
    ImGui::DragFloat("Character speed (world units/s)", &state.preview_character_speed,
                     0.1F, 0.0F, 20.0F);
    draw_technical_tooltip("Horizontal velocity used by the preview movement buttons.");
    if (const auto character = simulation.preview_character_state())
        ImGui::BulletText("character  pos %.2f, %.2f  vel %.2f, %.2f",
                          character->position.x, character->position.y,
                          character->velocity.x, character->velocity.y);

    ImGui::SeparatorText("Mechanic debug overlay");
    for (const auto& signal : simulation.signal_states()) {
        bool manually_active = signal.manually_active;
        const auto label = signal.kind == fabric::physics::MechanicSignalKind::sensor
            ? "Inject sensor: " + signal.node_id
            : "Inject event: " + signal.event_id.value;
        if (ImGui::Checkbox(label.c_str(), &manually_active)) {
            const auto applied = signal.kind ==
                    fabric::physics::MechanicSignalKind::sensor
                ? session.set_preview_sensor_active(
                      {.value = signal.node_id}, manually_active)
                : session.set_preview_event_active(signal.event_id,
                                                   manually_active);
            status = applied ? "Preview activation signal changed"
                             : "Preview activation signal rejected";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("state: %s · physical overlaps: %zu",
                            signal.active ? "active" : "inactive",
                            signal.physical_overlap_count);
    }
    for (const auto& activation : simulation.activation_states())
        ImGui::BulletText("%s  %s  source %s", activation.node_id.c_str(),
                          activation.active ? "active" : "inactive",
                          activation.source_node_id
                              ? activation.source_node_id->c_str() : "always");
    for (const auto& event : simulation.debug_events())
        ImGui::TextDisabled("step %zu  %s  %s", event.step,
            event.node_id.c_str(),
            event.transition == fabric::physics::MechanicActivationTransition::begin
                ? "begin" : "end");
    for (const auto& body : simulation.body_states())
        ImGui::BulletText("%s  pos %.2f, %.2f  rot %.2f deg",
                          body.node_id.c_str(), body.position.x, body.position.y,
                          body.rotation_degrees);
    for (const auto& error : session.preview_errors())
        ImGui::TextColored({1.0F, 0.62F, 0.25F, 1.0F}, "%s: %s",
                           error.field.c_str(), error.message.c_str());
    ImGui::Columns(1);
}


} // namespace fabric::map_studio
