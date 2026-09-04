#pragma once

#include "fabric/editor/editor_action_registry.hpp"
#include "fabric/editor/project_session.hpp"

#include <functional>
#include <span>
#include <string>

namespace fabric::asset_studio {

struct EntityWorkflowState {
    bool advanced_mode{};
};

struct EntityWorkflowProbe {
    bool enabled{};
    bool action_probe_enabled{};
    std::function<void(float, float)> record_animate_widget;
    std::function<void()> record_animate_click;
    std::function<void()> record_animate_action;
    std::function<void(bool)> record_behavior_picker;
};

using EntityWorkflowResourcePicker = bool (*)(
    const char*, std::span<const editor::StudioResource>,
    editor::StudioResourceKind, std::string&, bool, bool);

void draw_entity_workflow_panel(
    editor::ProjectSession& session, editor::EditorActionRegistry& actions,
    EntityWorkflowState& state, bool animation_graph_open,
    std::string& status, EntityWorkflowResourcePicker draw_resource_picker,
    const EntityWorkflowProbe* probe = nullptr);

} // namespace fabric::asset_studio
