#pragma once

#include "fabric/editor/behavior_session.hpp"
#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fabric::asset_studio {

struct BehaviorWorkspaceState {
    std::string selected_node_id;
    int node_type{};
    std::string new_node_id{"node"};
    std::string canvas_connection_source;
    std::string node_search;
    std::string debug_document_id;
    std::vector<std::string> breakpoints;
    std::vector<std::string> traced_nodes;
    bool debug_paused{};
    std::size_t trace_cursor{};
    int signal_source{};
    std::string semantic_id{"action"};
    std::string connection_id{"connection"};
    std::string from_node;
    std::string from_port{"out"};
    std::string to_node;
    std::string to_port{"in"};
};

struct BehaviorWorkspaceProbe {
    bool enabled{};
    bool canvas_seen{};
    bool link_seen{};
    bool add_seen{};
    bool add_clicked{};
    bool connect_seen{};
    bool target_seen{};
    bool connect_clicked{};
    bool target_clicked{};
    bool breakpoint_seen{};
    bool breakpoint_clicked{};
    bool evaluate_seen{};
    bool evaluate_clicked{};
    bool trace_highlight_seen{};
    bool paused_seen{};
    ImVec2 connect_screen{};
    ImVec2 target_screen{};
    ImVec2 add_screen{};
    ImVec2 breakpoint_screen{};
    ImVec2 evaluate_screen{};
};

using BehaviorResourceReferenceDrawer = bool (*)(
    const char*, std::span<const editor::StudioResource>,
    project::ResourceReference&);

void draw_behavior_workspace(
    editor::ProjectSession& project_session,
    editor::BehaviorSession& behavior_session,
    BehaviorWorkspaceState& state,
    std::string& status,
    BehaviorResourceReferenceDrawer draw_resource_reference,
    BehaviorWorkspaceProbe* probe = nullptr);

} // namespace fabric::asset_studio
