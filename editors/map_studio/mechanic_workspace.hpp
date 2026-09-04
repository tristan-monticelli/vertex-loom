#pragma once

#include "fabric/editor/map_session.hpp"
#include "fabric/editor/mechanic_presets.hpp"
#include "fabric/editor/mechanic_session.hpp"
#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <string>

namespace fabric::map_studio {

struct MechanicWorkspaceState {
    std::string open_id;
    std::string new_id;
    std::string new_name;
    std::string selected_node;
    std::string new_node_id;
    int new_node_kind{};
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    std::string canvas_connection_source;
    bool pending_canvas_connection{};
    std::string spatial_drag_node;
    std::string spatial_drag_property;
    core::Vec2 spatial_drag_start_value{};
    ImVec2 spatial_drag_start_mouse{};
    float spatial_zoom{32.0F};
    editor::RotatingPlatformPresetRequest platform;
    int platform_activation{};
    int platform_direction{};
    std::string platform_visual_entity;
    physics::MechanicPreviewCharacterConfig preview_character;
    float preview_character_speed{3.0F};
};

struct MechanicWorkspaceProbe {
    bool enabled{};
    bool canvas_seen{};
    bool spatial_canvas_seen{};
    bool spatial_handle_seen{};
    bool spatial_handle_moved{};
    bool link_seen{};
    bool source_seen{};
    bool target_seen{};
    bool source_clicked{};
    bool target_clicked{};
    ImVec2 source_screen{};
    ImVec2 target_screen{};
    ImVec2 spatial_handle_screen{};
    core::Vec2 spatial_handle_original{};
    std::string spatial_handle_node;
    std::string spatial_handle_property;
    project::MechanicConnection expected_connection;
};

void draw_mechanic_workspace(
    editor::MechanicSession& session,
    const editor::MapSession& map_session,
    MechanicWorkspaceState& state,
    std::string& status,
    editor::ProjectSession& resource_catalog,
    MechanicWorkspaceProbe* probe = nullptr);

} // namespace fabric::map_studio
