#pragma once

#include "animation_timeline_workspace.hpp"

#include <imgui.h>

#include <span>
#include <string>
#include <string_view>

namespace fabric::asset_studio {

struct AnimationInspectorProbe {
    bool enabled{};
    bool workflow_enabled{};
    bool node_picker_seen{};
    bool quick_key_seen{};
    bool playhead_seen{};
    bool auto_key_seen{};
    bool workflow_position_key_seen{};
    ImVec2 quick_key_screen{};
    ImVec2 playhead_target_screen{};
    ImVec2 auto_key_screen{};
    ImVec2 workflow_position_key_screen{};
};

using AnimationInspectorResourcePicker = bool (*)(
    const char*, std::span<const editor::StudioResource>,
    editor::StudioResourceKind, std::string&, bool, bool);
using AnimationResourceKindLabel = std::string_view (*)(
    editor::StudioResourceKind);

void draw_animation_inspector(
    editor::ProjectSession& session,
    AnimationWorkspaceState& state,
    bool show_advanced_ids,
    std::string& status,
    AnimationInspectorResourcePicker draw_resource_picker,
    AnimationResourceKindLabel resource_kind_label,
    AnimationInspectorProbe* probe = nullptr);

} // namespace fabric::asset_studio
