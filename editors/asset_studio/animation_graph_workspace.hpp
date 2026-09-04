#pragma once

#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <span>
#include <string>
#include <vector>

namespace fabric::asset_studio {

struct AnimationGraphWorkspaceState {
    bool open{};
    std::string document_id;
    std::string current_state;
    std::string selected_state;
    std::string connection_source;
    std::string last_transition;
    std::string new_state_clip_id;
    float normalized_time{};
    std::vector<project::AnimationParameter> parameters;
};

struct AnimationGraphWorkspaceProbe {
    bool enabled{};
    bool graph_seen{};
    bool canvas_seen{};
    bool link_seen{};
    bool add_seen{};
    bool connect_seen{};
    bool target_seen{};
    ImVec2 connect_screen{};
    ImVec2 target_screen{};
    ImVec2 add_screen{};
};

using AnimationResourcePicker = bool (*)(
    const char*, std::span<const editor::StudioResource>,
    editor::StudioResourceKind, std::string&, bool, bool);

void draw_animation_graph_workspace(
    editor::ProjectSession& session,
    AnimationGraphWorkspaceState& state,
    std::string& status,
    AnimationResourcePicker draw_resource_picker,
    AnimationGraphWorkspaceProbe* probe = nullptr);

} // namespace fabric::asset_studio
