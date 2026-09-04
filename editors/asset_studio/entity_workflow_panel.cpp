#include "entity_workflow_panel.hpp"

#include "editor_widgets.hpp"
#include "fabric/project/xpbd.hpp"

#include <imgui.h>

#include <optional>

namespace fabric::asset_studio {

void draw_entity_workflow_panel(
    editor::ProjectSession& session, editor::EditorActionRegistry& actions,
    EntityWorkflowState& state, const bool animation_graph_open,
    std::string& status,
    const EntityWorkflowResourcePicker resource_picker,
    const EntityWorkflowProbe* probe) {
    const auto& selected = session.selected_entity();
    if (!selected) return;
    const auto& entity = *selected;

    ImGui::SeparatorText("Entity");
    ImGui::TextWrapped(
        "Select a node, adjust it in Transform, then animate it when the pose is ready.");
    ImGui::Checkbox("Show advanced controls", &state.advanced_mode);
    if (state.advanced_mode && entity.xpbd) {
        const auto diagnostics = project::measure_xpbd_system(*entity.xpbd);
        ImGui::Text("XPBD · %zu particles · %zu constraints",
                    diagnostics.particle_count, diagnostics.constraint_count);
        ImGui::TextDisabled("error %.4f max / %.4f RMS · energy %.4f",
                            diagnostics.maximum_constraint_error,
                            diagnostics.rms_constraint_error,
                            diagnostics.compliant_energy);
    }

    const auto animate = actions.availability(
        editor::editor_action_ids::animate_selection);
    ImGui::BeginDisabled(!animate.enabled);
    const bool animate_clicked =
        ImGui::Button("Animate selected node...", {-1.0F, 0.0F});
    ImGui::EndDisabled();
    if (probe && probe->enabled && probe->record_animate_widget) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->record_animate_widget((minimum.x + maximum.x) * 0.5F,
                                     (minimum.y + maximum.y) * 0.5F);
    }
    if (animate_clicked) {
        if (probe && probe->enabled && probe->record_animate_click)
            probe->record_animate_click();
        static_cast<void>(actions.invoke(
            editor::editor_action_ids::animate_selection));
    }
    editor_ui::draw_disabled_reason(!animate.enabled,
                                    animate.disabled_reason);
    if (probe && probe->action_probe_enabled && probe->record_animate_action)
        probe->record_animate_action();

    const auto graph = actions.availability(
        editor::editor_action_ids::toggle_animation_graph);
    ImGui::BeginDisabled(!graph.enabled);
    const bool graph_clicked = ImGui::Button(
        animation_graph_open ? "Close Animation Graph" : "Open Animation Graph",
        {-1.0F, 0.0F});
    ImGui::EndDisabled();
    if (graph_clicked) {
        static_cast<void>(actions.invoke(
            editor::editor_action_ids::toggle_animation_graph));
    }
    editor_ui::draw_disabled_reason(!graph.enabled, graph.disabled_reason);

    ImGui::SeparatorText("Logic");
    ImGui::TextDisabled("Optional behavior graph evaluated by this Entity.");
    std::string behavior_id = entity.behavior
        ? entity.behavior->id.value : std::string{};
    if (resource_picker("Behavior", session.resources(),
                        editor::StudioResourceKind::behavior, behavior_id,
                        true, true)) {
        const auto reference = behavior_id.empty()
            ? std::optional<project::ResourceReference>{}
            : std::optional<project::ResourceReference>{
                  project::ResourceReference{{.value = behavior_id},
                                             "behavior"}};
        status = session.set_selected_entity_behavior(reference)
            ? "Entity behavior changed."
            : "Behavior attachment rejected; inspect diagnostics.";
    }
    if (probe && probe->enabled && probe->record_behavior_picker)
        probe->record_behavior_picker(!state.advanced_mode);
}

} // namespace fabric::asset_studio
