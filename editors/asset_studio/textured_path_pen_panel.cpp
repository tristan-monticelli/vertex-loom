#include "textured_path_pen_panel.hpp"

#include "editor_widgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <ranges>
#include <utility>

namespace fabric::asset_studio {

void draw_textured_path_pen_panel(
    editor::ProjectSession& session,
    TexturedPathPenPanelState& state,
    std::string& status) {
    const auto selected = session.selected_textured_path();
    if (!selected) return;
    if (state.document_id != selected->document.id.value) {
        state.document_id = selected->document.id.value;
        state.selected_command = 0U;
    }
    if (!selected->commands.empty()) {
        state.selected_command = std::min(
            state.selected_command, selected->commands.size() - 1U);
    } else {
        state.selected_command = 0U;
    }

    ImGui::SeparatorText("Pen and attachments");
    ImGui::Text("%zu path command(s)", selected->commands.size());
    for (std::size_t index = 0; index < selected->commands.size(); ++index) {
        const auto& command = selected->commands[index];
        const char* kind = index == 0U ? "Start attachment" :
            command.kind == project::TexturedPathCommandKind::line
                ? "Line point" : "Bezier point";
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(kind, state.selected_command == index))
            state.selected_command = index;
        ImGui::PopID();
    }
    if (!selected->commands.empty()) {
        auto command = selected->commands[state.selected_command];
        bool changed = ImGui::DragFloat2(
            state.selected_command == 0U ? "Start (world units)" :
                "Endpoint (world units)", &command.point.x, 0.05F);
        editor_ui::draw_technical_tooltip(
            "Position of the selected path command in world space.");
        if (command.kind == project::TexturedPathCommandKind::cubic) {
            changed |= ImGui::DragFloat2(
                "Handle in (world units)", &command.control1.x, 0.05F);
            editor_ui::draw_technical_tooltip("Incoming cubic handle position.");
            changed |= ImGui::DragFloat2(
                "Handle out (world units)", &command.control2.x, 0.05F);
            editor_ui::draw_technical_tooltip("Outgoing cubic handle position.");
        }
        if (changed) {
            auto candidate = *selected;
            candidate.commands[state.selected_command] = command;
            if (session.set_selected_textured_path(std::move(candidate)))
                status = "Textured path point updated.";
            else
                status = "Textured path rejected; inspect diagnostics.";
        }
    } else {
        ImGui::TextDisabled("Start with an attachment to draw the path.");
        if (ImGui::Button("Pen: add start attachment")) {
            auto candidate = *selected;
            candidate.commands.push_back({
                .kind = project::TexturedPathCommandKind::line,
                .point = {0.0F, 0.0F}});
            if (session.set_selected_textured_path(std::move(candidate))) {
                state.selected_command = 0U;
                status = "Textured path started.";
            } else {
                status = "Textured path rejected; inspect diagnostics.";
            }
        }
    }

    ImGui::BeginDisabled(selected->commands.empty());
    if (ImGui::Button("Pen: add line")) {
        auto candidate = *selected;
        const auto endpoint = candidate.commands.back().point;
        candidate.commands.push_back({
            .kind = project::TexturedPathCommandKind::line,
            .point = {endpoint.x + 1.0F, endpoint.y}});
        if (session.set_selected_textured_path(std::move(candidate))) {
            state.selected_command = selected->commands.size();
            status = "Textured path line added.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Pen: add Bezier")) {
        auto candidate = *selected;
        const auto endpoint = candidate.commands.back().point;
        candidate.commands.push_back({
            .kind = project::TexturedPathCommandKind::cubic,
            .point = {endpoint.x + 1.0F, endpoint.y},
            .control1 = {endpoint.x + 0.33F, endpoint.y},
            .control2 = {endpoint.x + 0.67F, endpoint.y}});
        if (session.set_selected_textured_path(std::move(candidate))) {
            state.selected_command = selected->commands.size();
            status = "Textured path Bezier point added.";
        }
    }
    ImGui::EndDisabled();
    editor_ui::draw_disabled_reason(
        selected->commands.empty(), "Start the path before adding a segment.");

    const bool at_minimum = selected->commands.size() <=
        (selected->closed ? 3U : 2U);
    ImGui::BeginDisabled(selected->commands.empty() || at_minimum);
    if (ImGui::Button("Remove last point")) {
        auto candidate = *selected;
        candidate.commands.pop_back();
        if (session.set_selected_textured_path(std::move(candidate))) {
            state.selected_command = std::min(
                state.selected_command, selected->commands.size() - 2U);
            status = "Textured path point removed.";
        }
    }
    ImGui::EndDisabled();
    editor_ui::draw_disabled_reason(
        selected->commands.empty() || at_minimum,
        "Keep the minimum number of points for this path.");
}

} // namespace fabric::asset_studio
