#pragma once

#include "fabric/editor/map_session.hpp"
#include "fabric/physics/mechanic_simulation.hpp"
#include "fabric/render/map_preview.hpp"
#include "fabric/render/opengl_vector_renderer.hpp"

#include <SDL_opengl.h>
#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fabric::map_studio {

struct TransformEditorState {
    std::string instance_id;
    core::Transform value{};
};

enum class CanvasGizmoMode { translate, rotate, scale };

struct CanvasGizmoState {
    bool active{};
    CanvasGizmoMode mode{CanvasGizmoMode::translate};
    std::string instance_id;
    ImVec2 start_mouse{};
    core::Transform start_transform{};
    core::Transform preview_transform{};
    std::vector<std::string> group_ids;
    core::Vec2 preview_delta{};
};

struct CollisionPointGizmoState {
    bool active{};
    int collision_index{-1};
    std::size_t point_index{};
    core::Vec2 preview_point{};
};

struct SelectionBoxState {
    bool active{};
    bool append{};
    ImVec2 start_mouse{};
    ImVec2 current_mouse{};
};

struct MapPlacementProbe {
    bool enabled{};
    bool canvas_seen{};
    bool canvas_hovered{};
    bool mechanic_overlay_seen{};
    ImVec2 canvas_center{};
    bool placement_button_seen{};
    ImVec2 placement_button_screen{};
    std::size_t successful_placements{};
};

struct MapTexture {
    GLuint handle{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct MapPreviewRenderer {
    render::OpenGLVectorRenderer* renderer{};
    const render::MapPreviewResult* preview{};
    const std::filesystem::path* project_root{};
    const project::ProjectManifest* manifest{};
    std::unordered_map<std::string, MapTexture>* textures{};
    render::OpenGLVectorViewport viewport{};
    std::vector<std::string> errors;
};

std::string collision_shape_text(const project::CollisionShape& shape);

void draw_transform_editor(
    editor::MapSession& session,
    const std::vector<std::string>& selected_instances,
    TransformEditorState& state,
    std::string& status);

void draw_map_canvas(
    editor::MapSession& session,
    std::vector<std::string>& selected_instances,
    ImVec2& pan,
    float& zoom,
    bool& grid_visible,
    CanvasGizmoState& gizmo,
    int selected_collision_index,
    CollisionPointGizmoState& point_gizmo,
    int selected_trigger_index,
    const std::string& active_layer_id,
    SelectionBoxState& selection_box,
    bool& placement_mode,
    bool keep_placement_active,
    std::string& placement_id,
    std::string& placement_resource_id,
    int& placement_kind,
    editor::MapSnapSettings& snapping,
    MapPreviewRenderer& preview_render_state,
    const physics::MechanicSimulation& mechanic_preview,
    const std::optional<core::ResourceId>& mechanic_preview_instance,
    std::string& status,
    MapPlacementProbe* probe = nullptr);

} // namespace fabric::map_studio
