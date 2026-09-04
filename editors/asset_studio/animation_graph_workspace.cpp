#include "animation_graph_workspace.hpp"

#include "editor_widgets.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace fabric::asset_studio {
namespace {

using editor_ui::draw_disabled_reason;

const char* animation_condition_label(
    const fabric::project::AnimationConditionOperator operation) {
    using Operator = fabric::project::AnimationConditionOperator;
    switch (operation) {
    case Operator::equal: return "Equal";
    case Operator::not_equal: return "Not equal";
    case Operator::less: return "Less";
    case Operator::less_equal: return "Less or equal";
    case Operator::greater: return "Greater";
    case Operator::greater_equal: return "Greater or equal";
    }
    return "Equal";
}

} // namespace

void draw_animation_graph_workspace(
    fabric::editor::ProjectSession& session,
    AnimationGraphWorkspaceState& ui,
    std::string& status,
    const AnimationResourcePicker draw_resource_picker,
    AnimationGraphWorkspaceProbe* probe) {
    if (!ui.open) return;
    if ((probe != nullptr && probe->enabled)) probe->graph_seen = true;
    const auto* selected = session.selected_resource();
    if (!selected || selected->kind != fabric::editor::StudioResourceKind::entity ||
        !session.selected_entity()) {
        ui.open = false;
        return;
    }
    if (ui.document_id != selected->id.value) {
        ui.document_id = selected->id.value;
        ui.current_state.clear();
        ui.selected_state.clear();
        ui.connection_source.clear();
        ui.last_transition.clear();
        ui.new_state_clip_id.clear();
        ui.normalized_time = 0.0F;
        ui.parameters.clear();
    }
    ImGui::SeparatorText("Animation Graph");
    const auto commit_machine =
        [&](std::optional<fabric::project::AnimationStateMachine> machine,
            const char* success) {
            auto entity = *session.selected_entity();
            entity.animation_state_machine = std::move(machine);
            if (session.set_selected_entity_definition(std::move(entity)))
                status = success;
            else
                status = "Animation Graph change rejected; inspect diagnostics.";
        };
    const auto first_animation = std::ranges::find_if(
        session.resources(), [](const auto& resource) {
            return resource.kind == fabric::editor::StudioResourceKind::animation;
        });
    if (!session.selected_entity()->animation_state_machine) {
        ImGui::TextWrapped(
            "Create a deterministic animation graph from an existing clip. "
            "Runtime parameters remain instance-owned.");
        ImGui::BeginDisabled(first_animation == session.resources().end());
        if (ImGui::Button("Create Animation Graph")) {
            fabric::project::AnimationStateMachine machine{
                .initial_state = "state-1",
                .states = {{"state-1", {first_animation->id, "animation"}}}};
            commit_machine(std::move(machine), "Animation Graph created.");
            ui.current_state = "state-1";
        }
        ImGui::EndDisabled();
        draw_disabled_reason(first_animation == session.resources().end(),
                             "Create an Animation clip before creating a graph.");
        return;
    }

    const auto machine_snapshot = *session.selected_entity()->animation_state_machine;
    ImGui::Text("Entity: %s", session.selected_entity()->document.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu states · %zu transitions",
                        machine_snapshot.states.size(),
                        machine_snapshot.transitions.size());
    if (ui.new_state_clip_id.empty() &&
        first_animation != session.resources().end())
        ui.new_state_clip_id = first_animation->id.value;
    static_cast<void>(draw_resource_picker(
        "New state clip", session.resources(),
        fabric::editor::StudioResourceKind::animation,
        ui.new_state_clip_id, false, false));
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.new_state_clip_id.empty());
    if (ImGui::Button("Add state from clip")) {
        auto next = machine_snapshot;
        std::string id = ui.new_state_clip_id;
        const std::string base = id;
        std::size_t suffix = 2U;
        while (std::ranges::any_of(next.states, [&](const auto& state) {
            return state.id == id;
        })) id = base + "-" + std::to_string(suffix++);
        next.states.push_back({
            id, {{.value = ui.new_state_clip_id}, "animation"}});
        commit_machine(std::move(next), "Animation state added from clip.");
        ui.selected_state = id;
    }
    if ((probe != nullptr && probe->enabled)) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->add_screen = {
            (minimum.x + maximum.x) * 0.5F,
            (minimum.y + maximum.y) * 0.5F};
        probe->add_seen = true;
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText("State graph");
    ImGui::TextDisabled(
        "Choose Connect from here, then click the destination state.");
    const float graph_height = 250.0F;
    if (ImGui::BeginChild("Animation state graph canvas", {0.0F, graph_height},
                          ImGuiChildFlags_Borders)) {
        if ((probe != nullptr && probe->enabled))
            probe->canvas_seen = true;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float available_width = ImGui::GetContentRegionAvail().x;
        constexpr float card_width = 176.0F;
        constexpr float card_height = 58.0F;
        constexpr float cell_width = 210.0F;
        constexpr float cell_height = 108.0F;
        const int columns = std::max(
            1, static_cast<int>(available_width / cell_width));
        std::vector<ImVec2> positions;
        positions.reserve(machine_snapshot.states.size());
        for (std::size_t index = 0; index < machine_snapshot.states.size();
             ++index) {
            const auto column = static_cast<int>(index) % columns;
            const auto row = static_cast<int>(index) / columns;
            positions.push_back({18.0F + static_cast<float>(column) * cell_width,
                                 16.0F + static_cast<float>(row) * cell_height});
        }

        auto* draw = ImGui::GetWindowDrawList();
        const ImU32 transition_color = ImGui::GetColorU32(ImGuiCol_PlotLines);
        for (const auto& transition : machine_snapshot.transitions) {
            const auto source = std::ranges::find(
                machine_snapshot.states, transition.from_state,
                &fabric::project::AnimationState::id);
            const auto destination = std::ranges::find(
                machine_snapshot.states, transition.to_state,
                &fabric::project::AnimationState::id);
            if (source == machine_snapshot.states.end() ||
                destination == machine_snapshot.states.end())
                continue;
            const auto source_index = static_cast<std::size_t>(
                std::distance(machine_snapshot.states.begin(), source));
            const auto destination_index = static_cast<std::size_t>(
                std::distance(machine_snapshot.states.begin(), destination));
            const ImVec2 start{origin.x + positions[source_index].x + card_width,
                               origin.y + positions[source_index].y +
                                   card_height * 0.5F};
            const ImVec2 end{origin.x + positions[destination_index].x,
                             origin.y + positions[destination_index].y +
                                 card_height * 0.5F};
            const float bend = std::max(40.0F, std::abs(end.x - start.x) * 0.4F);
            draw->AddBezierCubic(start, {start.x + bend, start.y},
                                 {end.x - bend, end.y}, end,
                                 transition_color, 2.0F);
            draw->AddTriangleFilled(
                end, {end.x - 9.0F, end.y - 5.0F},
                {end.x - 9.0F, end.y + 5.0F}, transition_color);
            if ((probe != nullptr && probe->enabled))
                probe->link_seen = true;
        }

        std::optional<std::pair<std::string, std::string>> pending_connection;
        for (std::size_t index = 0; index < machine_snapshot.states.size();
             ++index) {
            const auto& state = machine_snapshot.states[index];
            ImGui::SetCursorScreenPos(
                {origin.x + positions[index].x, origin.y + positions[index].y});
            ImGui::PushID(state.id.c_str());
            const bool selected_state = ui.selected_state == state.id;
            if (selected_state)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_Header));
            std::string label = state.id;
            if (state.id == machine_snapshot.initial_state) label += "  [initial]";
            label += "\n" + state.clip.id.value;
            if (ImGui::Button(label.c_str(), {card_width, card_height})) {
                ui.selected_state = state.id;
                if (!ui.connection_source.empty() &&
                    ui.connection_source != state.id)
                    pending_connection =
                        std::pair{ui.connection_source, state.id};
            }
            if ((probe != nullptr && probe->enabled) && index == 1U) {
                probe->target_seen = true;
                probe->target_screen = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->target_screen.x =
                    (probe->target_screen.x + maximum.x) * 0.5F;
                probe->target_screen.y =
                    (probe->target_screen.y + maximum.y) * 0.5F;
            }
            if (selected_state) ImGui::PopStyleColor();
            if (ui.connection_source == state.id) {
                if (ImGui::SmallButton("Cancel connection"))
                    ui.connection_source.clear();
            } else if (ImGui::SmallButton("Connect from here")) {
                ui.connection_source = state.id;
                ui.selected_state = state.id;
            }
            if ((probe != nullptr && probe->enabled) && index == 0U) {
                probe->connect_seen = true;
                probe->connect_screen = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->connect_screen.x =
                    (probe->connect_screen.x + maximum.x) * 0.5F;
                probe->connect_screen.y =
                    (probe->connect_screen.y + maximum.y) * 0.5F;
            }
            ImGui::PopID();
        }
        if (pending_connection) {
            auto next = machine_snapshot;
            std::string id = pending_connection->first + "-to-" +
                pending_connection->second;
            const std::string base = id;
            std::size_t suffix = 2U;
            while (std::ranges::any_of(next.transitions,
                                        [&](const auto& candidate) {
                return candidate.id == id;
            })) id = base + "-" + std::to_string(suffix++);
            next.transitions.push_back({.id = id,
                .from_state = pending_connection->first,
                .to_state = pending_connection->second});
            commit_machine(std::move(next), "Animation states connected.");
            ui.connection_source.clear();
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginTabBar("Animation graph tabs")) {
        if (ImGui::BeginTabItem("States")) {
            auto machine = machine_snapshot;
            const auto initial = std::ranges::find_if(
                machine.states, [&](const auto& state) {
                    return state.id == machine.initial_state;
                });
            const char* initial_label = initial == machine.states.end()
                ? "Choose initial state..." : initial->id.c_str();
            if (ImGui::BeginCombo("Initial state", initial_label)) {
                for (const auto& state : machine.states) {
                    if (ImGui::Selectable(state.id.c_str(),
                                          state.id == machine.initial_state)) {
                        machine.initial_state = state.id;
                        commit_machine(machine, "Initial animation state changed.");
                        ui.current_state = state.id;
                    }
                }
                ImGui::EndCombo();
            }
            bool removed = false;
            for (std::size_t index = 0; index < machine.states.size(); ++index) {
                auto state = machine.states[index];
                const auto old_id = state.id;
                ImGui::PushID(static_cast<int>(index));
                ImGui::SeparatorText(("State " + std::to_string(index + 1U)).c_str());
                ImGui::InputText("Id", &state.id);
                std::string clip_id = state.clip.id.value;
                static_cast<void>(draw_resource_picker(
                    "Clip", session.resources(),
                    fabric::editor::StudioResourceKind::animation,
                    clip_id, false, true));
                state.clip = {{.value = clip_id}, "animation"};
                if (ImGui::Button("Save state")) {
                    auto next = machine;
                    next.states[index] = state;
                    if (old_id != state.id) {
                        if (next.initial_state == old_id) next.initial_state = state.id;
                        for (auto& transition : next.transitions) {
                            if (transition.from_state == old_id)
                                transition.from_state = state.id;
                            if (transition.to_state == old_id)
                                transition.to_state = state.id;
                        }
                    }
                    commit_machine(std::move(next), "Animation state saved.");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(machine.states.size() <= 1U);
                if (ImGui::Button("Remove state")) {
                    auto next = machine;
                    next.states.erase(next.states.begin() +
                                      static_cast<std::ptrdiff_t>(index));
                    std::erase_if(next.transitions, [&](const auto& transition) {
                        return transition.from_state == old_id ||
                            transition.to_state == old_id;
                    });
                    if (next.initial_state == old_id)
                        next.initial_state = next.states.front().id;
                    commit_machine(std::move(next), "Animation state removed.");
                    removed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                if (removed) break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Transitions")) {
            auto machine = machine_snapshot;
            bool removed = false;
            for (std::size_t index = 0; index < machine.transitions.size(); ++index) {
                auto transition = machine.transitions[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::SeparatorText(
                    ("Transition " + std::to_string(index + 1U)).c_str());
                ImGui::InputText("Id", &transition.id);
                const auto draw_state_picker = [&](const char* label,
                                                   std::string& state_id) {
                    if (ImGui::BeginCombo(label, state_id.c_str())) {
                        for (const auto& state : machine.states)
                            if (ImGui::Selectable(state.id.c_str(),
                                                  state.id == state_id))
                                state_id = state.id;
                        ImGui::EndCombo();
                    }
                };
                draw_state_picker("From", transition.from_state);
                draw_state_picker("To", transition.to_state);
                ImGui::InputInt("Priority", &transition.priority);
                bool has_exit_time = transition.exit_time.has_value();
                if (ImGui::Checkbox("Require exit time", &has_exit_time)) {
                    if (has_exit_time) transition.exit_time = 1.0F;
                    else transition.exit_time.reset();
                }
                if (transition.exit_time)
                    ImGui::SliderFloat("Normalized exit time",
                                       &*transition.exit_time, 0.0F, 1.0F);
                for (std::size_t condition_index = 0;
                     condition_index < transition.conditions.size();
                     ++condition_index) {
                    auto& condition = transition.conditions[condition_index];
                    ImGui::PushID(static_cast<int>(condition_index));
                    ImGui::InputText("Parameter", &condition.parameter_id);
                    const bool boolean = std::holds_alternative<bool>(condition.value);
                    if (ImGui::BeginCombo("Operator",
                                          animation_condition_label(condition.operation))) {
                        using Operator = fabric::project::AnimationConditionOperator;
                        for (const auto operation : {
                                 Operator::equal, Operator::not_equal,
                                 Operator::less, Operator::less_equal,
                                 Operator::greater, Operator::greater_equal}) {
                            const bool allowed = !boolean || operation == Operator::equal ||
                                operation == Operator::not_equal;
                            ImGui::BeginDisabled(!allowed);
                            if (ImGui::Selectable(animation_condition_label(operation),
                                                  condition.operation == operation))
                                condition.operation = operation;
                            ImGui::EndDisabled();
                        }
                        ImGui::EndCombo();
                    }
                    bool use_boolean = boolean;
                    if (ImGui::Checkbox("Boolean parameter", &use_boolean)) {
                        condition.value = use_boolean
                            ? fabric::project::AnimationParameterValue{false}
                            : fabric::project::AnimationParameterValue{0.0F};
                        if (use_boolean && condition.operation !=
                                fabric::project::AnimationConditionOperator::equal &&
                            condition.operation !=
                                fabric::project::AnimationConditionOperator::not_equal)
                            condition.operation =
                                fabric::project::AnimationConditionOperator::equal;
                    }
                    if (auto* value = std::get_if<bool>(&condition.value))
                        ImGui::Checkbox("Expected", value);
                    else
                        ImGui::InputFloat("Expected", &std::get<float>(condition.value));
                    if (ImGui::SmallButton("Remove condition")) {
                        transition.conditions.erase(
                            transition.conditions.begin() +
                            static_cast<std::ptrdiff_t>(condition_index));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add condition"))
                    transition.conditions.push_back({"parameter",
                        fabric::project::AnimationConditionOperator::equal, false});
                ImGui::SameLine();
                if (ImGui::Button("Save transition")) {
                    auto next = machine;
                    next.transitions[index] = std::move(transition);
                    commit_machine(std::move(next), "Animation transition saved.");
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove transition")) {
                    auto next = machine;
                    next.transitions.erase(
                        next.transitions.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    commit_machine(std::move(next), "Animation transition removed.");
                    removed = true;
                }
                ImGui::PopID();
                if (removed) break;
            }
            ImGui::BeginDisabled(machine.states.size() < 2U);
            if (ImGui::Button("Add transition")) {
                auto next = machine_snapshot;
                std::string id = "transition-" +
                    std::to_string(next.transitions.size() + 1U);
                while (std::ranges::any_of(next.transitions,
                                            [&](const auto& candidate) {
                    return candidate.id == id;
                })) id += "-copy";
                next.transitions.push_back({
                    .id = id,
                    .from_state = next.states[0].id,
                    .to_state = next.states[1].id});
                commit_machine(std::move(next), "Animation transition added.");
            }
            ImGui::EndDisabled();
            draw_disabled_reason(machine.states.size() < 2U,
                                 "Add at least two states before connecting them.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Preview")) {
            const auto& machine = machine_snapshot;
            if (!fabric::project::find_animation_state(machine, ui.current_state))
                ui.current_state = machine.initial_state;
            for (const auto& transition : machine.transitions) {
                for (const auto& condition : transition.conditions) {
                    const auto found = std::ranges::find(
                        ui.parameters, condition.parameter_id,
                        &fabric::project::AnimationParameter::id);
                    if (found == ui.parameters.end())
                        ui.parameters.push_back({condition.parameter_id,
                            std::holds_alternative<bool>(condition.value)
                                ? fabric::project::AnimationParameterValue{false}
                                : fabric::project::AnimationParameterValue{0.0F}});
                }
            }
            ImGui::Text("Active state: %s", ui.current_state.c_str());
            if (const auto* state = fabric::project::find_animation_state(
                    machine, ui.current_state))
                ImGui::TextDisabled("Clip: %s", state->clip.id.value.c_str());
            if (!ui.last_transition.empty())
                ImGui::Text("Last transition: %s", ui.last_transition.c_str());
            ImGui::SliderFloat("Normalized time", &ui.normalized_time, 0.0F, 1.0F);
            for (auto& parameter : ui.parameters) {
                ImGui::PushID(parameter.id.c_str());
                if (auto* value = std::get_if<bool>(&parameter.value))
                    ImGui::Checkbox(parameter.id.c_str(), value);
                else
                    ImGui::InputFloat(parameter.id.c_str(),
                                      &std::get<float>(parameter.value));
                ImGui::PopID();
            }
            if (ImGui::Button("Evaluate transition")) {
                if (const auto* transition =
                        fabric::project::select_animation_transition(
                            machine, ui.current_state, ui.parameters,
                            ui.normalized_time)) {
                    ui.current_state = transition->to_state;
                    ui.last_transition = transition->id;
                    ui.normalized_time = 0.0F;
                    status = "Animation Graph preview transitioned.";
                } else {
                    ui.last_transition.clear();
                    status = "No animation transition matched.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset preview")) {
                ui.current_state = machine.initial_state;
                ui.last_transition.clear();
                ui.normalized_time = 0.0F;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (ImGui::Button("Remove Animation Graph..."))
        ImGui::OpenPopup("Remove Animation Graph?");
    if (ImGui::BeginPopupModal("Remove Animation Graph?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Remove all animation states and transitions from this Entity?");
        if (ImGui::Button("Remove graph")) {
            commit_machine(std::nullopt, "Animation Graph removed.");
            ui.current_state.clear();
            ui.selected_state.clear();
            ui.connection_source.clear();
            ui.last_transition.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}


} // namespace fabric::asset_studio
