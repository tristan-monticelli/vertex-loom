#pragma once

#include "fabric/editor/map_session.hpp"
#include "fabric/editor/mechanic_presets.hpp"
#include "fabric/editor/mechanic_session.hpp"
#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <string>

namespace fabric::map_studio {

enum class MechanicSpatialDragKind {
    none,
    move,
    resize,
    rotate,
};

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
    std::string spatial_drag_handle_node;
    std::string spatial_drag_node;
    std::string spatial_drag_property;
    MechanicSpatialDragKind spatial_drag_kind{MechanicSpatialDragKind::none};
    core::Vec2 spatial_drag_start_value{};
    core::Vec2 spatial_drag_start_size{};
    float spatial_drag_start_rotation{};
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
    bool instance_action_seen{};
    bool instance_action_clicked{};
    ImVec2 instance_action_screen{};
    bool canvas_seen{};
    bool spatial_canvas_seen{};
    bool spatial_handle_seen{};
    bool spatial_handle_moved{};
    bool body_handle_seen{};
    bool resize_handle_seen{};
    bool resize_handle_moved{};
    bool rotation_handle_seen{};
    bool rotation_handle_moved{};
    bool joint_handle_seen{};
    bool joint_handle_moved{};
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
    ImVec2 body_handle_screen{};
    ImVec2 resize_handle_screen{};
    core::Vec2 resize_handle_original{};
    ImVec2 rotation_handle_screen{};
    float rotation_handle_original{};
    ImVec2 joint_handle_screen{};
    core::Vec2 joint_handle_original{};
    std::string joint_handle_node;
    std::string joint_mutation_node;
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
