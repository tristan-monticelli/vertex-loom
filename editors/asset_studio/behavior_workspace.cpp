#include "behavior_workspace.hpp"

#include "editor_widgets.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fabric::asset_studio {
namespace {

using editor_ui::contains_ascii_insensitive;
using editor_ui::draw_disabled_reason;

bool draw_behavior_node_picker(
    const char* label,
    const std::span<const fabric::project::BehaviorNodeDefinition> nodes,
    std::string& selected_id) {
    const auto selected = std::ranges::find_if(
        nodes, [&](const auto& node) { return node.id == selected_id; });
    const std::string preview = selected != nodes.end()
        ? selected->id
        : selected_id.empty() ? std::string{"Choose a graph node..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(280.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##behavior-node-search", "Search node ID or type...",
                                 &filter);
        bool found = false;
        for (const auto& node : nodes) {
            if (!contains_ascii_insensitive(node.id, filter) &&
                !contains_ascii_insensitive(node.type, filter))
                continue;
            found = true;
            const bool is_selected = node.id == selected_id;
            const std::string item_label = node.id + " (" + node.type + ")##behavior-node-option-" +
                node.id;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                selected_id = node.id;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        if (!found) ImGui::TextDisabled("No matching graph node.");
        if (selected == nodes.end() && !selected_id.empty())
            ImGui::TextDisabled("Missing node reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_behavior_port_picker(
    const char* label,
    const std::span<const fabric::project::BehaviorNodeDefinition> nodes,
    const std::string_view node_id,
    std::string& selected_id,
    const fabric::project::BehaviorPortDirection direction) {
    const auto node = std::ranges::find_if(
        nodes, [&](const auto& candidate) { return candidate.id == node_id; });
    const fabric::project::BehaviorPortDefinition* selected = nullptr;
    if (node != nodes.end()) {
        const auto selected_it = std::ranges::find_if(node->ports, [&](const auto& port) {
            return port.id == selected_id && port.direction == direction;
        });
        if (selected_it != node->ports.end()) selected = &*selected_it;
    }
    const std::string preview = selected != nullptr
        ? selected->id + " (" + std::string{fabric::project::to_string(selected->type)} + ")"
        : selected_id.empty() ? std::string{"Choose a graph port..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##behavior-port-search", "Search port ID or type...",
                                 &filter);
        bool found = false;
        if (node != nodes.end()) {
            for (const auto& port : node->ports) {
                if (port.direction != direction) continue;
                const auto type = std::string{fabric::project::to_string(port.type)};
                auto haystack = port.id + " " + type;
                auto needle = filter;
                std::ranges::transform(haystack, haystack.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                std::ranges::transform(needle, needle.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                if (!needle.empty() && haystack.find(needle) == std::string::npos) continue;
                found = true;
                const bool is_selected = port.id == selected_id;
                const auto item_label = port.id + " (" + type + ")##behavior-port-option-" + port.id;
                if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                    selected_id = port.id;
                    changed = true;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
        }
        if (!found) ImGui::TextDisabled("No matching graph port for this node.");
        if (node == nodes.end()) ImGui::TextDisabled("Choose a graph node first.");
        else if (selected == nullptr && !selected_id.empty())
            ImGui::TextDisabled("Missing port reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}


} // namespace

void draw_behavior_workspace(
    fabric::editor::ProjectSession& project_session,
    fabric::editor::BehaviorSession& behavior_session,
    BehaviorWorkspaceState& state,
    std::string& status,
    const BehaviorResourceReferenceDrawer draw_resource_reference,
    BehaviorWorkspaceProbe* probe) {
    const auto* selected = project_session.selected_resource();
    if (!selected || selected->kind != fabric::editor::StudioResourceKind::behavior)
        return;
    if (!behavior_session.has_graph() ||
        behavior_session.graph()->document.id != selected->id) {
        if (!behavior_session.open(project_session.project_root(), selected->id)) {
            status = "Behavior could not be opened.";
            return;
        }
    }

    ImGui::SeparatorText("Behavior Graph");
    const auto& graph = *behavior_session.graph();
    ImGui::Text("%s", graph.document.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", graph.document.id.value.c_str());
    if (ImGui::Button("Save behavior"))
        status = behavior_session.save() ? "Behavior saved." : "Behavior save failed.";
    ImGui::SameLine();
    ImGui::BeginDisabled(!behavior_session.can_undo());
    if (ImGui::Button("Undo##behavior")) static_cast<void>(behavior_session.undo());
    ImGui::EndDisabled();
    draw_disabled_reason(!behavior_session.can_undo(), "No behavior edit to undo.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!behavior_session.can_redo());
    if (ImGui::Button("Redo##behavior")) static_cast<void>(behavior_session.redo());
    ImGui::EndDisabled();
    draw_disabled_reason(!behavior_session.can_redo(), "No behavior edit to redo.");

    static constexpr const char* node_types[] = {
        "action_source", "ai_source", "event_source", "trigger_source",
        "timer_source", "property_source", "condition", "branch", "sequence",
        "delay", "cooldown", "state", "transition", "set_property",
        "emit_event", "play_animation", "move", "activate_mechanic",
        "transform_entity"};
    static constexpr const char* source_labels[] = {
        "Action", "AI", "Map event", "Trigger", "Timer", "Property"};
    if (state.debug_document_id != graph.document.id.value) {
        state.debug_document_id = graph.document.id.value;
        state.selected_node_id.clear();
        state.canvas_connection_source.clear();
        state.from_node.clear();
        state.from_port = "out";
        state.to_node.clear();
        state.to_port = "in";
        state.breakpoints.clear();
        state.traced_nodes.clear();
        state.debug_paused = false;
        state.trace_cursor = 0U;
    }
    if ((probe != nullptr && probe->enabled) && state.node_search.empty())
        state.node_search = "emit_event";
    ImGui::SetNextItemWidth(220.0F);
    ImGui::InputTextWithHint("##behavior-node-search",
                             "Search node type...", &state.node_search);
    ImGui::SameLine();
    bool any_matching_type = false;
    for (const auto* type : node_types) {
        std::string display_type = type;
        std::ranges::replace(display_type, '_', ' ');
        if (!display_type.empty())
            display_type.front() = static_cast<char>(
                std::toupper(static_cast<unsigned char>(display_type.front())));
        if (!contains_ascii_insensitive(type, state.node_search) &&
            !contains_ascii_insensitive(display_type, state.node_search))
            continue;
        any_matching_type = true;
        ImGui::PushID(type);
        const std::string label = "Add " + display_type;
        if (ImGui::Button(label.c_str())) {
            std::string id = type;
            std::ranges::replace(id, '_', '-');
            const std::string base = id;
            std::size_t suffix = 2U;
            while (std::ranges::any_of(graph.nodes, [&](const auto& node) {
                return node.id == id;
            })) id = base + "-" + std::to_string(suffix++);
            if (behavior_session.add_node(type, id)) {
                state.selected_node_id = id;
                status = "Behavior node added from palette.";
                if ((probe != nullptr && probe->enabled) &&
                    std::string_view{type} == "emit_event")
                    probe->add_clicked = true;
            } else {
                status = "Node rejected; inspect Behavior diagnostics.";
            }
        }
        if ((probe != nullptr && probe->enabled) &&
            std::string_view{type} == "emit_event") {
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            probe->add_screen = {
                (minimum.x + maximum.x) * 0.5F,
                (minimum.y + maximum.y) * 0.5F};
            probe->add_seen = true;
        }
        ImGui::PopID();
        break;
    }
    if (!any_matching_type)
        ImGui::TextDisabled("No matching behavior node type.");
    ImGui::SeparatorText("Debug current signal");
    if ((probe != nullptr && probe->enabled)) {
        state.signal_source = 1;
        state.semantic_id = "attack";
    }
    ImGui::SetNextItemWidth(130.0F);
    ImGui::Combo("Source##behavior-debug", &state.signal_source, source_labels, 6);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputText("Signal##behavior-debug", &state.semantic_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.debug_paused);
    if (ImGui::Button("Evaluate fixed step")) {
        const auto actions = behavior_session.preview(
            {static_cast<fabric::runtime::BehaviorSignalSource>(state.signal_source),
             state.semantic_id, {}}, 1.0F / 60.0F);
        state.traced_nodes.clear();
        state.selected_node_id.clear();
        for (const auto& entry : behavior_session.trace()) {
            if (std::ranges::find(state.traced_nodes, entry.node_id) ==
                state.traced_nodes.end()) {
                state.traced_nodes.push_back(entry.node_id);
            }
        }
        const auto breakpoint = std::ranges::find_first_of(
            state.traced_nodes, state.breakpoints);
        state.debug_paused = breakpoint != state.traced_nodes.end();
        state.trace_cursor = 0U;
        status = state.debug_paused
            ? "Behavior paused on breakpoint " + *breakpoint + "."
            : "Behavior preview produced " + std::to_string(actions.size()) +
                  " action(s).";
        if ((probe != nullptr && probe->enabled))
            probe->evaluate_clicked = true;
    }
    ImGui::EndDisabled();
    if ((probe != nullptr && probe->enabled)) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->evaluate_screen = {
            (minimum.x + maximum.x) * 0.5F,
            (minimum.y + maximum.y) * 0.5F};
        probe->evaluate_seen = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.debug_paused || behavior_session.trace().empty());
    if ((probe != nullptr && probe->enabled) && state.debug_paused)
        probe->paused_seen = true;
    if (ImGui::Button("Step trace")) {
        const auto& trace = behavior_session.trace();
        const auto node = std::ranges::find(
            graph.nodes, trace[state.trace_cursor].node_id,
            &fabric::project::BehaviorNodeDefinition::id);
        if (node != graph.nodes.end())
            state.selected_node_id = node->id;
        status = "Trace: " + trace[state.trace_cursor].message;
        state.trace_cursor = std::min(state.trace_cursor + 1U, trace.size() - 1U);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.debug_paused);
    if (ImGui::Button("Continue")) {
        state.debug_paused = false;
        status = "Behavior debugger continued.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset debug")) {
        behavior_session.reset_preview();
        state.traced_nodes.clear();
        state.debug_paused = false;
        state.trace_cursor = 0U;
    }
    ImGui::SeparatorText("Behavior canvas");
    ImGui::TextDisabled(
        "Choose Connect from output, then click a compatible destination.");
    if (ImGui::BeginChild("Behavior graph canvas", {0.0F, 250.0F},
                          ImGuiChildFlags_Borders)) {
        if ((probe != nullptr && probe->enabled))
            probe->canvas_seen = true;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        constexpr float card_width = 190.0F;
        constexpr float card_height = 78.0F;
        constexpr float cell_width = 220.0F;
        constexpr float cell_height = 126.0F;
        const int columns = std::max(1, static_cast<int>(
            ImGui::GetContentRegionAvail().x / cell_width));
        std::vector<ImVec2> positions;
        positions.reserve(graph.nodes.size());
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            const auto column = static_cast<int>(index) % columns;
            const auto row = static_cast<int>(index) / columns;
            positions.push_back({16.0F + static_cast<float>(column) * cell_width,
                                 16.0F + static_cast<float>(row) * cell_height});
        }
        auto* draw = ImGui::GetWindowDrawList();
        const ImU32 connection_color = ImGui::GetColorU32(ImGuiCol_PlotLines);
        for (const auto& connection : graph.connections) {
            const auto source = std::ranges::find(
                graph.nodes, connection.from_node,
                &fabric::project::BehaviorNodeDefinition::id);
            const auto destination = std::ranges::find(
                graph.nodes, connection.to_node,
                &fabric::project::BehaviorNodeDefinition::id);
            if (source == graph.nodes.end() || destination == graph.nodes.end())
                continue;
            const auto source_index = static_cast<std::size_t>(
                std::distance(graph.nodes.begin(), source));
            const auto destination_index = static_cast<std::size_t>(
                std::distance(graph.nodes.begin(), destination));
            const ImVec2 start{origin.x + positions[source_index].x + card_width,
                               origin.y + positions[source_index].y +
                                   card_height * 0.5F};
            const ImVec2 end{origin.x + positions[destination_index].x,
                             origin.y + positions[destination_index].y +
                                 card_height * 0.5F};
            const float bend = std::max(40.0F, std::abs(end.x - start.x) * 0.4F);
            draw->AddBezierCubic(start, {start.x + bend, start.y},
                                 {end.x - bend, end.y}, end,
                                 connection_color, 2.0F);
            draw->AddTriangleFilled(end, {end.x - 9.0F, end.y - 5.0F},
                                    {end.x - 9.0F, end.y + 5.0F},
                                    connection_color);
            if ((probe != nullptr && probe->enabled))
                probe->link_seen = true;
        }
        struct PendingBehaviorConnection {
            std::string from_node;
            std::string from_port;
            std::string to_node;
            std::string to_port;
        };
        std::optional<PendingBehaviorConnection> pending_connection;
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            const auto& node = graph.nodes[index];
            const auto output = std::ranges::find(
                node.ports, fabric::project::BehaviorPortDirection::output,
                &fabric::project::BehaviorPortDefinition::direction);
            const auto input = std::ranges::find(
                node.ports, fabric::project::BehaviorPortDirection::input,
                &fabric::project::BehaviorPortDefinition::direction);
            ImGui::SetCursorScreenPos(
                {origin.x + positions[index].x, origin.y + positions[index].y});
            ImGui::PushID(node.id.c_str());
            std::string label = node.type + "\n" + node.id;
            if (input != node.ports.end())
                label += "\nin: " + input->id;
            if (output != node.ports.end())
                label += "\nout: " + output->id;
            const bool traced =
                std::ranges::find(state.traced_nodes, node.id) != state.traced_nodes.end();
            const bool breakpoint =
                std::ranges::find(state.breakpoints, node.id) != state.breakpoints.end();
            if (traced) {
                ImGui::PushStyleColor(ImGuiCol_Button, {0.18F, 0.52F, 0.28F, 1.0F});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      {0.24F, 0.66F, 0.36F, 1.0F});
            }
            if (ImGui::Button(label.c_str(), {card_width, card_height})) {
                state.selected_node_id = node.id;
                if ((probe != nullptr && probe->enabled) && index == 1U)
                    probe->target_clicked = true;
                if (!state.canvas_connection_source.empty() &&
                    state.canvas_connection_source != node.id) {
                    const auto source = std::ranges::find(
                        graph.nodes, state.canvas_connection_source,
                        &fabric::project::BehaviorNodeDefinition::id);
                    if (source != graph.nodes.end()) {
                        const auto source_port = std::ranges::find(
                            source->ports,
                            fabric::project::BehaviorPortDirection::output,
                            &fabric::project::BehaviorPortDefinition::direction);
                        const auto input = std::ranges::find_if(
                            node.ports, [&](const auto& port) {
                                return source_port != source->ports.end() &&
                                    port.direction ==
                                        fabric::project::BehaviorPortDirection::input &&
                                    port.type == source_port->type;
                            });
                        if (source_port != source->ports.end() &&
                            input != node.ports.end())
                            pending_connection = {source->id, source_port->id,
                                                  node.id, input->id};
                        else
                            status = "No compatible input on destination node.";
                    }
                }
            }
            if (traced) {
                ImGui::PopStyleColor(2);
                if ((probe != nullptr && probe->enabled))
                    probe->trace_highlight_seen = true;
            }
            if ((probe != nullptr && probe->enabled) && index == 1U) {
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
                state.selected_node_id = node.id;
                if ((probe != nullptr && probe->enabled) && index == 0U)
                    probe->connect_clicked = true;
            }
            ImGui::EndDisabled();
            if ((probe != nullptr && probe->enabled) && index == 0U) {
                probe->connect_seen = true;
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->connect_screen = {
                    (minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F};
            }
            ImGui::SameLine();
            const std::string breakpoint_label = breakpoint
                ? "Breakpoint on" : "Add breakpoint";
            if (ImGui::SmallButton(breakpoint_label.c_str())) {
                if (breakpoint)
                    std::erase(state.breakpoints, node.id);
                else
                    state.breakpoints.push_back(node.id);
                if ((probe != nullptr && probe->enabled) && index == 0U)
                    probe->breakpoint_clicked = true;
            }
            if ((probe != nullptr && probe->enabled) && index == 0U) {
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->breakpoint_screen = {
                    (minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F};
                probe->breakpoint_seen = true;
            }
            ImGui::PopID();
        }
        if (pending_connection) {
            std::string id = pending_connection->from_node + "-to-" +
                pending_connection->to_node;
            const std::string base = id;
            std::size_t suffix = 2U;
            while (std::ranges::any_of(graph.connections,
                                        [&](const auto& candidate) {
                return candidate.id == id;
            })) id = base + "-" + std::to_string(suffix++);
            if (behavior_session.connect({id, pending_connection->from_node,
                    pending_connection->from_port, pending_connection->to_node,
                    pending_connection->to_port})) {
                state.canvas_connection_source.clear();
                status = "Behavior nodes connected.";
            } else {
                status = "Connection rejected; inspect Behavior diagnostics.";
            }
        }
    }
    ImGui::EndChild();
    ImGui::SeparatorText("Nodes");
    if (ImGui::CollapsingHeader("Advanced node creation")) {
        ImGui::SetNextItemWidth(190.0F);
        ImGui::Combo("Type", &state.node_type, node_types,
                     static_cast<int>(std::size(node_types)));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170.0F);
        ImGui::InputText("Id", &state.new_node_id);
        ImGui::SameLine();
        if (ImGui::Button("Add node with custom id")) {
            if (behavior_session.add_node(node_types[state.node_type], state.new_node_id)) {
                state.selected_node_id = state.new_node_id;
                status = "Behavior node added.";
            } else {
                status = "Node rejected; inspect Behavior diagnostics.";
            }
        }
    }
    if (ImGui::BeginChild("Behavior node list", {260.0F, 230.0F}, true)) {
        for (std::size_t index = 0; index < behavior_session.graph()->nodes.size(); ++index) {
            const auto& node = behavior_session.graph()->nodes[index];
            const std::string label = node.id + "  [" + node.type + "]##behavior-node";
            if (ImGui::Selectable(label.c_str(), state.selected_node_id == node.id))
                state.selected_node_id = node.id;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Behavior node inspector", {0.0F, 230.0F}, true)) {
        const auto selected_node = std::ranges::find(
            behavior_session.graph()->nodes, state.selected_node_id,
            &fabric::project::BehaviorNodeDefinition::id);
        if (selected_node != behavior_session.graph()->nodes.end()) {
            const auto node = *selected_node;
            ImGui::Text("%s", node.type.c_str());
            for (const auto& property : node.properties) {
                ImGui::PushID(property.id.c_str());
                auto value = property.value;
                bool changed = false;
                const std::string numeric_label = property.id +
                    " (declared units)##behavior-numeric-" + property.id;
                if (auto* typed = std::get_if<bool>(&value)) changed = ImGui::Checkbox(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<std::int64_t>(&value)) {
                    int visible = static_cast<int>(*typed);
                    changed = ImGui::InputInt(numeric_label.c_str(), &visible);
                    *typed = visible;
                    ImGui::SetItemTooltip("Integer value interpreted using the behavior property schema.");
                } else if (auto* typed = std::get_if<float>(&value)) {
                    changed = ImGui::InputFloat(numeric_label.c_str(), typed);
                    ImGui::SetItemTooltip("Real value interpreted using the behavior property schema.");
                }
                else if (auto* typed = std::get_if<std::string>(&value)) changed = ImGui::InputText(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<fabric::core::Vec2>(&value)) {
                    float values[2]{typed->x, typed->y}; changed = ImGui::InputFloat2(numeric_label.c_str(), values);
                    *typed = {values[0], values[1]};
                    ImGui::SetItemTooltip("Vector value interpreted using the behavior property schema.");
                } else if (auto* typed = std::get_if<fabric::project::ResourceReference>(&value)) {
                    changed = draw_resource_reference(
                        property.id.c_str(), project_session.resources(), *typed);
                    ImGui::TextDisabled("expected: %s", typed->expected_type.c_str());
                }
                if (changed) static_cast<void>(behavior_session.set_node_property(
                    {.value = node.id}, property.id, std::move(value)));
                ImGui::PopID();
            }
            if (ImGui::Button("Duplicate node")) {
                const auto copy_id = node.id + "-copy";
                if (behavior_session.duplicate_node({.value = node.id}, copy_id))
                    state.selected_node_id = copy_id;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.55F, 0.16F, 0.16F, 1.0F});
            if (ImGui::Button("Delete node")) {
                if (behavior_session.remove_node({.value = node.id}))
                    state.selected_node_id.clear();
            }
            ImGui::PopStyleColor();
        } else ImGui::TextDisabled("Select a node to edit all typed properties.");
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Connections");
    ImGui::InputText("Connection id", &state.connection_id);
    static_cast<void>(draw_behavior_node_picker(
        "From node", behavior_session.graph()->nodes, state.from_node)); ImGui::SameLine();
    static_cast<void>(draw_behavior_port_picker(
        "From port", behavior_session.graph()->nodes, state.from_node, state.from_port,
        fabric::project::BehaviorPortDirection::output));
    static_cast<void>(draw_behavior_node_picker(
        "To node", behavior_session.graph()->nodes, state.to_node)); ImGui::SameLine();
    static_cast<void>(draw_behavior_port_picker(
        "To port", behavior_session.graph()->nodes, state.to_node, state.to_port,
        fabric::project::BehaviorPortDirection::input));
    if (ImGui::Button("Connect"))
        static_cast<void>(behavior_session.connect({state.connection_id, state.from_node, state.from_port, state.to_node, state.to_port}));
    std::optional<std::string> remove_connection;
    for (const auto& connection : behavior_session.graph()->connections) {
        ImGui::BulletText("%s: %s.%s -> %s.%s", connection.id.c_str(),
                          connection.from_node.c_str(), connection.from_port.c_str(),
                          connection.to_node.c_str(), connection.to_port.c_str());
        ImGui::SameLine(); ImGui::PushID(connection.id.c_str());
        if (ImGui::SmallButton("Remove")) remove_connection = connection.id;
        ImGui::PopID();
    }
    if (remove_connection)
        static_cast<void>(behavior_session.disconnect({.value = *remove_connection}));

    ImGui::SeparatorText("Trace");
    if (ImGui::BeginChild("Behavior trace", {0.0F, 100.0F}, true))
        for (const auto& entry : behavior_session.trace())
            ImGui::Text("%s — %s", entry.node_id.c_str(), entry.message.c_str());
    ImGui::EndChild();
    for (const auto& error : behavior_session.errors())
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s: %s",
                           error.field.c_str(), error.message.c_str());
}


} // namespace fabric::asset_studio
