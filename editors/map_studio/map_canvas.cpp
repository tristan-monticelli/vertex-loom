#include "map_canvas.hpp"

#include "editor_widgets.hpp"
#include "fabric/project/document_storage.hpp"
#include "fabric/render/raster_image.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace fabric::map_studio {

using editor_ui::draw_disabled_reason;
using editor_ui::draw_technical_tooltip;

std::string collision_shape_text(const fabric::project::CollisionShape& shape) {
    std::string result;
    switch (shape.kind) {
    case fabric::project::CollisionShapeKind::circle: result = "circle"; break;
    case fabric::project::CollisionShapeKind::capsule: result = "capsule"; break;
    case fabric::project::CollisionShapeKind::polygon: result = "polygon"; break;
    case fabric::project::CollisionShapeKind::chain: result = "chain"; break;
    }
    result += " @ " + std::to_string(shape.center.x) + "," +
              std::to_string(shape.center.y);
    if (shape.kind == fabric::project::CollisionShapeKind::circle ||
        shape.kind == fabric::project::CollisionShapeKind::capsule)
        result += " r=" + std::to_string(shape.radius);
    if (shape.kind == fabric::project::CollisionShapeKind::capsule)
        result += " l=" + std::to_string(shape.length);
    result += shape.sensor ? " [sensor]" : " [solid]";
    return result;
}

bool layer_visible(const fabric::project::MapDocument& map, const std::string& layer_id) {
    const auto layer = std::find_if(map.layers.begin(), map.layers.end(),
                                    [&](const auto& candidate) {
                                        return candidate.id == layer_id;
                                    });
    return layer == map.layers.end() || layer->visible;
}

void render_map_preview_callback(const ImDrawList*, const ImDrawCmd* command) {
    auto* state = static_cast<MapPreviewRenderer*>(command->UserCallbackData);
    if (state == nullptr || state->renderer == nullptr || state->preview == nullptr ||
        state->project_root == nullptr || state->manifest == nullptr ||
        state->textures == nullptr) return;
    state->errors = state->preview->errors;
    if (!state->renderer->ready()) {
        state->errors.push_back("OpenGL initialization failed: " +
                                state->renderer->initialization_error());
        return;
    }
    if (state->preview->packets.empty()) return;
    const fabric::render::OpenGLTextureResolver resolver =
        [state](const fabric::core::ResourceId& id)
        -> std::optional<fabric::render::OpenGLTextureHandle> {
        const auto cached = state->textures->find(id.value);
        if (cached != state->textures->end()) {
            return fabric::render::OpenGLTextureHandle{
                cached->second.handle, cached->second.width, cached->second.height};
        }
        const auto loaded = fabric::project::load_texture_asset(
            *state->project_root, *state->manifest,
            fabric::project::texture_document_path(*state->manifest, id));
        if (!loaded.ok()) {
            state->errors.push_back("Texture document '" + id.value +
                                    "' is missing or invalid");
            return std::nullopt;
        }
        const auto decoded = fabric::render::load_png(
            *state->project_root / loaded.asset->source);
        if (!decoded.ok()) {
            state->errors.push_back("Texture PNG '" + id.value +
                                    "' could not be decoded");
            return std::nullopt;
        }
        MapTexture texture{.width = decoded.image->width,
                           .height = decoded.image->height};
        glGenTextures(1, &texture.handle);
        glBindTexture(GL_TEXTURE_2D, texture.handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        while (glGetError() != GL_NO_ERROR) {}
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     static_cast<GLsizei>(texture.width),
                     static_cast<GLsizei>(texture.height), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, decoded.image->rgba8.data());
        const auto upload_error = glGetError();
        glBindTexture(GL_TEXTURE_2D, 0);
        if (upload_error != GL_NO_ERROR) {
            glDeleteTextures(1, &texture.handle);
            state->errors.push_back("GPU rejected texture upload for '" +
                                    id.value + "' (OpenGL error " +
                                    std::to_string(upload_error) + ")");
            return std::nullopt;
        }
        const auto [inserted, _] = state->textures->emplace(id.value, texture);
        return fabric::render::OpenGLTextureHandle{
            inserted->second.handle, inserted->second.width, inserted->second.height};
    };
    const auto stats = state->renderer->draw(
        state->preview->packets, state->viewport, resolver);
    state->errors.insert(state->errors.end(), stats.errors.begin(),
                         stats.errors.end());
}

void draw_transform_editor(fabric::editor::MapSession& session,
                           const std::vector<std::string>& selected_instances,
                           TransformEditorState& state,
                           std::string& status) {
    if (selected_instances.size() != 1U || !session.map()) return;
    const auto& map = *session.map();
    const auto found = std::find_if(map.instances.begin(), map.instances.end(),
                                    [&](const auto& instance) {
                                        return instance.id == selected_instances.front();
                                    });
    if (found == map.instances.end()) return;
    const auto instance_id = found->id;
    if (state.instance_id != instance_id) {
        state.instance_id = instance_id;
        state.value = found->transform;
    }
    ImGui::SeparatorText("Transform gizmo");
    ImGui::TextDisabled("Selected: %s", instance_id.c_str());
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Position (world units)", &state.value.position.x, 0.1F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Position transformed" : "Transform rejected (layer locked or invalid)";
    }
    draw_technical_tooltip("Instance translation in map world space.");
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat("Rotation (degrees)", &state.value.rotation_degrees, 1.0F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Rotation transformed" : "Transform rejected (layer locked or invalid)";
    }
    draw_technical_tooltip("Instance rotation around its pivot.");
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Scale (factor)", &state.value.scale.x, 0.01F, -32.0F, 32.0F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Scale transformed" : "Transform rejected (layer locked or invalid)";
    }
    draw_technical_tooltip("Instance scale multiplier on each axis.");
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Pivot (world units)", &state.value.pivot.x, 0.01F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Pivot transformed" : "Transform rejected (layer locked or invalid)";
    }
    draw_technical_tooltip("Instance pivot in map world space.");
}

void draw_map_canvas(fabric::editor::MapSession& session,
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
                     const bool keep_placement_active,
                     std::string& placement_id,
                     std::string& placement_resource_id,
                     int& placement_kind,
                     fabric::editor::MapSnapSettings& snapping,
                     MapPreviewRenderer& preview_render_state,
                     fabric::editor::MechanicSession& mechanic_session,
                     MapMechanicOverlayState& mechanic_gizmo,
                     std::string& requested_mechanic_node,
                     std::string& status,
                     MapPlacementProbe* probe) {
    if (!session.map()) return;
    const auto& map = *session.map();
    ImGui::SeparatorText("Canvas");
    ImGui::TextDisabled("Active layer: %s", active_layer_id.c_str());
    const auto canvas_available = ImGui::GetContentRegionAvail();
    const ImVec2 canvas_size{
        canvas_available.x,
        std::clamp(canvas_available.y - 110.0F, 260.0F, 520.0F)};
    auto frame_instances = [&](const bool selected_only) {
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        bool found = false;
        for (const auto& instance : map.instances) {
            if (!layer_visible(map, instance.layer_id)) continue;
            if (selected_only && std::find(selected_instances.begin(), selected_instances.end(),
                                           instance.id) == selected_instances.end()) continue;
            min_x = std::min(min_x, instance.transform.position.x);
            min_y = std::min(min_y, instance.transform.position.y);
            max_x = std::max(max_x, instance.transform.position.x);
            max_y = std::max(max_y, instance.transform.position.y);
            found = true;
        }
        if (!found) return false;
        const auto width = std::max(max_x - min_x, 1.0F);
        const auto height = std::max(max_y - min_y, 1.0F);
        zoom = std::clamp(std::min((canvas_size.x - 48.0F) / width,
                                   (canvas_size.y - 48.0F) / height), 0.1F, 32.0F);
        const auto center_x = (min_x + max_x) * 0.5F;
        const auto center_y = (min_y + max_y) * 0.5F;
        pan = {-center_x * zoom, center_y * zoom};
        return true;
    };
    ImGui::BeginDisabled(selected_instances.empty());
    if (ImGui::Button("Frame selection"))
        status = frame_instances(true) ? "Selection framed" : "No visible selected instance";
    if (probe != nullptr && probe->enabled) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->frame_selection_seen = true;
        probe->frame_selection_screen = {
            (minimum.x + maximum.x) * 0.5F,
            (minimum.y + maximum.y) * 0.5F};
    }
    ImGui::EndDisabled();
    draw_disabled_reason(selected_instances.empty(),
                         "Select at least one instance before framing the selection.");
    ImGui::SameLine();
    if (ImGui::Button("Frame all"))
        status = frame_instances(false) ? "Map framed" : "No visible instance to frame";
    ImGui::SameLine();
    if (ImGui::Button("-##map-zoom"))
        zoom = std::clamp(zoom / 1.25F, 0.1F, 32.0F);
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f%%", zoom * 100.0F);
    ImGui::SameLine();
    if (ImGui::Button("+##map-zoom"))
        zoom = std::clamp(zoom * 1.25F, 0.1F, 32.0F);
    ImGui::SameLine();
    ImGui::Checkbox("Grid##map-canvas", &grid_visible);
    if (ImGui::CollapsingHeader("Snapping")) {
        ImGui::Checkbox("Snap translation", &snapping.enabled);
        ImGui::SetNextItemWidth(140.0F);
        ImGui::DragFloat("Grid size (world units)", &snapping.grid_size,
                         0.1F, 0.01F, 1024.0F);
        draw_technical_tooltip("Distance between snap grid lines.");
        ImGui::SetNextItemWidth(180.0F);
        ImGui::DragFloat2("Origin (world units)", &snapping.origin.x, 0.1F);
        draw_technical_tooltip("World-space origin used by the snap grid.");
    }
    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map-canvas", canvas_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 canvas_center{canvas_pos.x + canvas_size.x * 0.5F,
                               canvas_pos.y + canvas_size.y * 0.5F};
    auto* draw = ImGui::GetWindowDrawList();
    if (probe != nullptr && probe->enabled) {
        probe->canvas_seen = true;
        probe->canvas_hovered = hovered;
        const auto clip_min = draw->GetClipRectMin();
        const auto clip_max = draw->GetClipRectMax();
        const ImVec2 visible_min{std::max(canvas_pos.x, clip_min.x),
                                 std::max(canvas_pos.y, clip_min.y)};
        const ImVec2 visible_max{
            std::min(canvas_pos.x + canvas_size.x, clip_max.x),
            std::min(canvas_pos.y + canvas_size.y, clip_max.y)};
        probe->canvas_center = {
            (visible_min.x + visible_max.x) * 0.5F,
            (visible_min.y + visible_max.y) * 0.5F};
    }
    auto world_to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{canvas_center.x + pan.x + point.x * zoom,
                      canvas_center.y + pan.y - point.y * zoom};
    };
    auto screen_to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{(point.x - canvas_center.x - pan.x) / zoom,
                                  -(point.y - canvas_center.y - pan.y) / zoom};
    };
    const auto& io = ImGui::GetIO();
    auto transform_for = [&](const fabric::project::MapInstance& instance) {
        if (!gizmo.active) return instance.transform;
        if (gizmo.mode == CanvasGizmoMode::translate &&
            std::find(gizmo.group_ids.begin(), gizmo.group_ids.end(), instance.id) !=
                gizmo.group_ids.end()) {
            auto result = instance.transform;
            result.position.x += gizmo.preview_delta.x;
            result.position.y += gizmo.preview_delta.y;
            return result;
        }
        return gizmo.instance_id == instance.id ? gizmo.preview_transform : instance.transform;
    };
    auto collision_point_for = [&](const std::size_t collision_index,
                                   const std::size_t point_index,
                                   const fabric::core::Vec2 point) {
        return point_gizmo.active &&
                       point_gizmo.collision_index == static_cast<int>(collision_index) &&
                       point_gizmo.point_index == point_index
                   ? point_gizmo.preview_point : point;
    };

    draw->AddRectFilled(canvas_pos,
                        {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                        IM_COL32(22, 25, 31, 255));
    const float pixels_per_grid = zoom;
    float grid_step = 1.0F;
    while (grid_step * pixels_per_grid < 14.0F) grid_step *= 2.0F;
    while (grid_step * pixels_per_grid > 70.0F) grid_step *= 0.5F;
    const auto top_left = screen_to_world(canvas_pos);
    const auto bottom_right = screen_to_world(
        {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y});
    const auto first_x = static_cast<int>(std::floor(top_left.x / grid_step)) - 1;
    const auto last_x = static_cast<int>(std::ceil(bottom_right.x / grid_step)) + 1;
    const auto first_y = static_cast<int>(std::floor(bottom_right.y / grid_step)) - 1;
    const auto last_y = static_cast<int>(std::ceil(top_left.y / grid_step)) + 1;
    if (grid_visible) {
    for (int x = first_x; x <= last_x; ++x) {
        const auto line = world_to_screen({static_cast<float>(x) * grid_step, 0.0F});
        draw->AddLine({line.x, canvas_pos.y},
                      {line.x, canvas_pos.y + canvas_size.y},
                      x == 0 ? IM_COL32(105, 115, 130, 220) : IM_COL32(48, 54, 64, 180));
    }
    for (int y = first_y; y <= last_y; ++y) {
        const auto line = world_to_screen({0.0F, static_cast<float>(y) * grid_step});
        draw->AddLine({canvas_pos.x, line.y},
                      {canvas_pos.x + canvas_size.x, line.y},
                      y == 0 ? IM_COL32(105, 115, 130, 220) : IM_COL32(48, 54, 64, 180));
    }
    }
    const auto framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
    preview_render_state.viewport = {
        .width = std::max(1, static_cast<std::int32_t>(
            canvas_size.x * framebuffer_scale.x)),
        .height = std::max(1, static_cast<std::int32_t>(
            canvas_size.y * framebuffer_scale.y)),
        .world_bounds = {{top_left.x, bottom_right.y},
                         {bottom_right.x - top_left.x, top_left.y - bottom_right.y}},
        .x = std::max(0, static_cast<std::int32_t>(
            canvas_pos.x * framebuffer_scale.x)),
        .y = std::max(0, static_cast<std::int32_t>(
            (ImGui::GetIO().DisplaySize.y - canvas_pos.y - canvas_size.y) *
            framebuffer_scale.y)),
    };
    draw->AddCallback(render_map_preview_callback, &preview_render_state);
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    if (probe != nullptr) probe->mechanic.enabled = probe->enabled;
    const auto mechanic_overlay = draw_mechanic_map_overlay(
        session, mechanic_session, mechanic_gizmo,
        selected_instances.size() == 1U ? selected_instances.front()
                                        : std::string{},
        canvas_center, pan, zoom, hovered,
        placement_mode || gizmo.active || point_gizmo.active ||
            selection_box.active,
        status, probe != nullptr ? &probe->mechanic : nullptr);
    requested_mechanic_node = mechanic_overlay.open_node;
    const bool mechanic_pointer_captured =
        mechanic_overlay.pointer_captured || mechanic_gizmo.active;
    if (mechanic_overlay.map_changed) return;
    for (std::size_t collision_index = 0; collision_index < map.collisions.size();
         ++collision_index) {
        const auto& collision = map.collisions[collision_index];
        if (!layer_visible(map, collision.layer_id)) continue;
        const auto center = world_to_screen(collision.center);
        const auto color = collision.sensor ? IM_COL32(240, 190, 80, 210)
                                            : IM_COL32(220, 90, 90, 210);
        if (collision.kind == fabric::project::CollisionShapeKind::circle) {
            draw->AddCircle(center, collision.radius * zoom, color, 32, 2.0F);
        } else if (collision.kind == fabric::project::CollisionShapeKind::capsule) {
            const float half_length = collision.length * zoom * 0.5F;
            draw->AddLine({center.x - half_length, center.y},
                          {center.x + half_length, center.y}, color, 2.0F);
            draw->AddCircle({center.x - half_length, center.y}, collision.radius * zoom,
                            color, 24, 2.0F);
            draw->AddCircle({center.x + half_length, center.y}, collision.radius * zoom,
                            color, 24, 2.0F);
        } else if (collision.points.size() > 1U) {
            for (std::size_t point = 1; point < collision.points.size(); ++point)
                draw->AddLine(world_to_screen(collision_point_for(
                                  collision_index, point - 1, collision.points[point - 1])),
                              world_to_screen(collision_point_for(
                                  collision_index, point, collision.points[point])), color, 2.0F);
            if (collision.kind == fabric::project::CollisionShapeKind::polygon)
                draw->AddLine(world_to_screen(collision_point_for(
                                  collision_index, collision.points.size() - 1,
                                  collision.points.back())),
                              world_to_screen(collision_point_for(
                                  collision_index, 0, collision.points.front())), color, 2.0F);
        }
    }
    if (selected_collision_index >= 0 &&
        static_cast<std::size_t>(selected_collision_index) < map.collisions.size()) {
        const auto& collision = map.collisions[static_cast<std::size_t>(selected_collision_index)];
        if (collision.kind == fabric::project::CollisionShapeKind::polygon ||
            collision.kind == fabric::project::CollisionShapeKind::chain) {
            for (std::size_t point = 0; point < collision.points.size(); ++point) {
                const auto position = world_to_screen(collision_point_for(
                    static_cast<std::size_t>(selected_collision_index), point,
                    collision.points[point]));
                const auto active = point_gizmo.active &&
                    point_gizmo.collision_index == selected_collision_index &&
                    point_gizmo.point_index == point;
                draw->AddCircleFilled(position, active ? 7.0F : 5.0F,
                                      IM_COL32(250, 210, 90, 255));
            }
        }
    }
    for (std::size_t trigger_index = 0; trigger_index < map.triggers.size(); ++trigger_index) {
        const auto& trigger = map.triggers[trigger_index];
        if (trigger.collision_index >= map.collisions.size()) continue;
        const auto& collision = map.collisions[trigger.collision_index];
        if (!layer_visible(map, collision.layer_id)) continue;
        fabric::core::Vec2 anchor = collision.center;
        if (!collision.points.empty()) {
            anchor = {};
            for (const auto point : collision.points) {
                anchor.x += point.x;
                anchor.y += point.y;
            }
            const auto count = static_cast<float>(collision.points.size());
            anchor.x /= count;
            anchor.y /= count;
        }
        const auto label_position = world_to_screen(anchor);
        const auto color = static_cast<int>(trigger_index) == selected_trigger_index
            ? IM_COL32(255, 240, 100, 255) : IM_COL32(160, 220, 255, 230);
        draw->AddText({label_position.x + 8.0F, label_position.y + 8.0F}, color,
                      trigger.event_id.value.c_str());
    }
    for (const auto& instance : map.instances) {
        if (!layer_visible(map, instance.layer_id)) continue;
        const auto transform = transform_for(instance);
        const auto point = world_to_screen(transform.position);
        const bool selected = std::find(selected_instances.begin(), selected_instances.end(),
                                        instance.id) != selected_instances.end();
        draw->AddCircleFilled(point, selected ? 8.0F : 6.0F,
                              selected ? IM_COL32(90, 190, 255, 255)
                                       : IM_COL32(150, 205, 165, 255));
        draw->AddText({point.x + 9.0F, point.y - 7.0F}, IM_COL32(220, 225, 235, 230),
                      instance.id.c_str());
    }
    if (selected_instances.size() == 1U) {
        const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                           [&](const auto& instance) {
                                               return instance.id == selected_instances.front();
                                           });
        if (selected != map.instances.end() && layer_visible(map, selected->layer_id)) {
            const auto transform = transform_for(*selected);
            const auto center = world_to_screen(transform.position);
            const auto scale_handle = ImVec2{center.x + 36.0F, center.y};
            const auto rotate_handle = ImVec2{center.x, center.y - 36.0F};
            draw->AddLine({center.x - 18.0F, center.y}, {center.x + 48.0F, center.y},
                          IM_COL32(90, 190, 255, 180), 1.0F);
            draw->AddCircleFilled(scale_handle, 6.0F, IM_COL32(100, 220, 140, 240));
            draw->AddCircle(rotate_handle, 6.0F, IM_COL32(240, 180, 80, 240), 16, 2.0F);
            draw->AddCircle(center, 11.0F, IM_COL32(90, 190, 255, 240), 24, 2.0F);
        }
    } else if (selected_instances.size() > 1U) {
        ImVec2 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        ImVec2 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        bool found = false;
        for (const auto& instance : map.instances) {
            if (std::find(selected_instances.begin(), selected_instances.end(), instance.id) ==
                    selected_instances.end() || !layer_visible(map, instance.layer_id))
                continue;
            const auto point = world_to_screen(transform_for(instance).position);
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            found = true;
        }
        if (found) {
            minimum.x -= 12.0F;
            minimum.y -= 12.0F;
            maximum.x += 12.0F;
            maximum.y += 12.0F;
            draw->AddRect(minimum, maximum, IM_COL32(90, 190, 255, 230), 2.0F, 0, 2.0F);
        }
    }
    draw->AddRect(canvas_pos,
                  {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                  IM_COL32(130, 140, 155, 255));
    if (selection_box.active) {
        const ImVec2 minimum{std::min(selection_box.start_mouse.x,
                                      selection_box.current_mouse.x),
                             std::min(selection_box.start_mouse.y,
                                      selection_box.current_mouse.y)};
        const ImVec2 maximum{std::max(selection_box.start_mouse.x,
                                      selection_box.current_mouse.x),
                             std::max(selection_box.start_mouse.y,
                                      selection_box.current_mouse.y)};
        draw->AddRectFilled(minimum, maximum, IM_COL32(80, 160, 240, 35));
        draw->AddRect(minimum, maximum, IM_COL32(100, 190, 255, 220));
    }

    if (placement_mode && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        placement_mode = false;
        status = "Placement cancelled";
        return;
    }

    if (hovered) {
        if (io.MouseWheel != 0.0F) {
            const auto before = screen_to_world(io.MousePos);
            zoom = std::clamp(zoom * std::pow(1.15F, io.MouseWheel), 0.1F, 32.0F);
            const auto after = world_to_screen(before);
            pan.x += io.MousePos.x - after.x;
            pan.y += io.MousePos.y - after.y;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan.x += io.MouseDelta.x;
            pan.y += io.MouseDelta.y;
        }
        if (!io.WantTextInput && !placement_mode && !gizmo.active &&
            !point_gizmo.active && !selection_box.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                status = frame_instances(true) ? "Selection framed"
                                               : "No visible selected instance to frame";
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
                status = frame_instances(false) ? "Map framed"
                                                : "No visible instance to frame";
        }
        if (!io.WantTextInput && !placement_mode && !gizmo.active &&
            !point_gizmo.active && !selection_box.active && !selected_instances.empty()) {
            std::vector<fabric::core::ResourceId> ids;
            for (const auto& id : selected_instances) ids.push_back({.value = id});
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                const auto removed = session.remove_instances(ids);
                status = removed ? "Selected instances deleted"
                                 : "Delete rejected (selection locked or invalid)";
                if (removed) selected_instances.clear();
                return;
            }
            fabric::core::Vec2 nudge{};
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) nudge.x = -1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) nudge.x = 1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) nudge.y = 1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) nudge.y = -1.0F;
            if (nudge.x != 0.0F || nudge.y != 0.0F) {
                const auto moved = session.translate_instances(ids, nudge, snapping);
                status = moved ? "Selected instances nudged"
                                : "Nudge rejected (selection locked or invalid)";
                return;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) &&
                selected_instances.size() == 1U) {
                const auto duplicated = session.duplicate_instance(
                    ids.front(), {1.0F, 1.0F}, snapping);
                status = duplicated ? "Selected instance duplicated"
                                    : "Duplication rejected (locked or invalid)";
                return;
            }
        }
        if (!mechanic_pointer_captured && !mechanic_gizmo.active &&
            !placement_mode && !gizmo.active && !point_gizmo.active && io.KeyCtrl &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_instances.size() == 1U) {
            const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                               [&](const auto& instance) {
                                                   return instance.id == selected_instances.front();
                                               });
            if (selected != map.instances.end()) {
                const auto target = screen_to_world(io.MousePos);
                const fabric::core::Vec2 offset{
                    target.x - selected->transform.position.x,
                    target.y - selected->transform.position.y};
                const auto duplicated = session.duplicate_instance(
                    {.value = selected->id}, offset, snapping);
                status = duplicated ? "Instance duplicated at cursor"
                                    : "Duplication rejected (locked or invalid)";
                return;
            }
        }
        if (placement_mode && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            fabric::project::MapInstance instance;
            const auto placed_id = placement_id;
            instance.id = placed_id;
            instance.layer_id = active_layer_id;
            instance.transform.position = screen_to_world(io.MousePos);
            if (placement_kind == 0)
                instance.entity = fabric::project::ResourceReference{
                    {.value = placement_resource_id}, "entity"};
            else
                instance.prefab = fabric::project::ResourceReference{
                    {.value = placement_resource_id}, "prefab"};
            const auto placed = session.place_instance(std::move(instance), snapping);
            status = placed
                ? (keep_placement_active
                       ? "Instance placed; click again or press Escape"
                       : "Instance placed")
                : "Placement rejected (id, resource, layer or lock)";
            if (placed) {
                selected_instances = {placed_id};
                placement_id.clear();
                if (!keep_placement_active) placement_mode = false;
                if (probe != nullptr && probe->enabled)
                    ++probe->successful_placements;
            }
            return;
        }
        if (!mechanic_pointer_captured && !mechanic_gizmo.active &&
            !gizmo.active && !point_gizmo.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_collision_index >= 0) {
            const auto collision_index = static_cast<std::size_t>(selected_collision_index);
            if (collision_index < map.collisions.size()) {
                const auto& collision = map.collisions[collision_index];
                if (collision.kind == fabric::project::CollisionShapeKind::polygon ||
                    collision.kind == fabric::project::CollisionShapeKind::chain) {
                    for (std::size_t point = 0; point < collision.points.size(); ++point) {
                        const auto screen = world_to_screen(collision.points[point]);
                        const auto dx = io.MousePos.x - screen.x;
                        const auto dy = io.MousePos.y - screen.y;
                        if (std::sqrt(dx * dx + dy * dy) <= 10.0F) {
                            point_gizmo.active = true;
                            point_gizmo.collision_index = selected_collision_index;
                            point_gizmo.point_index = point;
                            point_gizmo.preview_point = collision.points[point];
                            break;
                        }
                    }
                }
            }
        }
        if (point_gizmo.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                point_gizmo.preview_point = screen_to_world(io.MousePos);
            } else {
                const auto index = static_cast<std::size_t>(point_gizmo.collision_index);
                if (index < map.collisions.size() &&
                    point_gizmo.point_index < map.collisions[index].points.size()) {
                    auto shape = map.collisions[index];
                    shape.points[point_gizmo.point_index] = point_gizmo.preview_point;
                    const auto committed = session.set_collision_shape(index, std::move(shape));
                    status = committed ? "Collision point committed"
                                       : "Collision point rejected (layer locked or invalid)";
                }
                point_gizmo.active = false;
                point_gizmo.collision_index = -1;
            }
        }
        if (!mechanic_pointer_captured && !mechanic_gizmo.active &&
            !gizmo.active && !point_gizmo.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !selected_instances.empty()) {
            const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                               [&](const auto& instance) {
                                                   return instance.id == selected_instances.front();
                                               });
            if (selected_instances.size() == 1U && selected != map.instances.end()) {
                const auto transform = transform_for(*selected);
                const auto center = world_to_screen(transform.position);
                const auto scale_handle = ImVec2{center.x + 36.0F, center.y};
                const auto rotate_handle = ImVec2{center.x, center.y - 36.0F};
                const auto distance = [](const ImVec2 a, const ImVec2 b) {
                    const auto dx = a.x - b.x;
                    const auto dy = a.y - b.y;
                    return std::sqrt(dx * dx + dy * dy);
                };
                const auto handle_distance = distance(io.MousePos, scale_handle);
                const auto rotate_distance = distance(io.MousePos, rotate_handle);
                const auto center_distance = distance(io.MousePos, center);
                if (handle_distance <= 10.0F || rotate_distance <= 10.0F ||
                    center_distance <= 12.0F) {
                    gizmo.active = true;
                    gizmo.instance_id = selected->id;
                    gizmo.start_mouse = io.MousePos;
                    gizmo.start_transform = selected->transform;
                    gizmo.preview_transform = selected->transform;
                    gizmo.mode = handle_distance <= 10.0F
                        ? CanvasGizmoMode::scale
                        : (rotate_distance <= 10.0F ? CanvasGizmoMode::rotate
                                                   : CanvasGizmoMode::translate);
                }
            } else if (selected_instances.size() > 1U) {
                const auto hit = std::find_if(map.instances.begin(), map.instances.end(),
                                              [&](const auto& instance) {
                                                  if (std::find(selected_instances.begin(),
                                                                selected_instances.end(),
                                                                instance.id) == selected_instances.end())
                                                      return false;
                                                  const auto point = world_to_screen(
                                                      instance.transform.position);
                                                  const auto dx = io.MousePos.x - point.x;
                                                  const auto dy = io.MousePos.y - point.y;
                                                  return std::sqrt(dx * dx + dy * dy) <= 12.0F;
                                              });
                if (hit != map.instances.end()) {
                    gizmo.active = true;
                    gizmo.mode = CanvasGizmoMode::translate;
                    gizmo.instance_id = hit->id;
                    gizmo.start_mouse = io.MousePos;
                    gizmo.preview_delta = {};
                    gizmo.group_ids = selected_instances;
                }
            }
        }
        if (gizmo.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (gizmo.mode == CanvasGizmoMode::translate) {
                    const auto start = screen_to_world(gizmo.start_mouse);
                    const auto current = screen_to_world(io.MousePos);
                    gizmo.preview_delta = {current.x - start.x, current.y - start.y};
                    if (gizmo.group_ids.size() == 1U)
                        gizmo.preview_transform.position = {
                            gizmo.start_transform.position.x + gizmo.preview_delta.x,
                            gizmo.start_transform.position.y + gizmo.preview_delta.y};
                } else if (gizmo.mode == CanvasGizmoMode::rotate) {
                    const auto center = world_to_screen(gizmo.start_transform.position);
                    const auto start_angle = std::atan2(gizmo.start_mouse.y - center.y,
                                                        gizmo.start_mouse.x - center.x);
                    const auto current_angle = std::atan2(io.MousePos.y - center.y,
                                                          io.MousePos.x - center.x);
                    constexpr float radians_to_degrees = 57.29577951308232F;
                    gizmo.preview_transform.rotation_degrees =
                        gizmo.start_transform.rotation_degrees +
                        (current_angle - start_angle) * radians_to_degrees;
                } else {
                    const auto delta = (io.MousePos.x - gizmo.start_mouse.x) / 48.0F;
                    const auto factor = std::max(0.05F, 1.0F + delta);
                    gizmo.preview_transform.scale = {
                        gizmo.start_transform.scale.x * factor,
                        gizmo.start_transform.scale.y * factor};
                }
            } else {
                bool committed = false;
                if (gizmo.group_ids.size() > 1U) {
                    std::vector<fabric::core::ResourceId> ids;
                    for (const auto& id : gizmo.group_ids) ids.push_back({.value = id});
                    committed = session.translate_instances(ids, gizmo.preview_delta, snapping);
                } else {
                    committed = session.set_instance_transform(
                        {.value = gizmo.instance_id}, gizmo.preview_transform,
                        gizmo.mode == CanvasGizmoMode::translate
                            ? snapping : fabric::editor::MapSnapSettings{.enabled = false});
                }
                status = committed ? "Canvas transform committed"
                                   : "Canvas transform rejected (layer locked or invalid)";
                gizmo.active = false;
                gizmo.instance_id.clear();
                gizmo.group_ids.clear();
                gizmo.preview_delta = {};
            }
        }
        if (!mechanic_pointer_captured && !mechanic_gizmo.active &&
            !gizmo.active && !point_gizmo.active && !placement_mode &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl) {
            selection_box.active = true;
            selection_box.append = io.KeyShift;
            selection_box.start_mouse = io.MousePos;
            selection_box.current_mouse = io.MousePos;
        }
        if (selection_box.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                selection_box.current_mouse = io.MousePos;
            } else {
                const auto start = screen_to_world(selection_box.start_mouse);
                const auto end = screen_to_world(selection_box.current_mouse);
                const auto min_x = std::min(start.x, end.x);
                const auto max_x = std::max(start.x, end.x);
                const auto min_y = std::min(start.y, end.y);
                const auto max_y = std::max(start.y, end.y);
                const auto width = std::abs(selection_box.current_mouse.x -
                                            selection_box.start_mouse.x);
                const auto height = std::abs(selection_box.current_mouse.y -
                                             selection_box.start_mouse.y);
                if (width >= 5.0F || height >= 5.0F) {
                    if (!selection_box.append) selected_instances.clear();
                    for (const auto& instance : map.instances) {
                        if (!layer_visible(map, instance.layer_id)) continue;
                        const auto& position = instance.transform.position;
                        if (position.x >= min_x && position.x <= max_x &&
                            position.y >= min_y && position.y <= max_y &&
                            std::find(selected_instances.begin(), selected_instances.end(),
                                      instance.id) == selected_instances.end())
                            selected_instances.push_back(instance.id);
                    }
                    status = "Rectangle selection changed";
                } else {
                    const auto world = screen_to_world(selection_box.start_mouse);
                    auto hit = map.instances.end();
                    float best_distance = 12.0F / zoom;
                    for (auto candidate = map.instances.begin(); candidate != map.instances.end();
                         ++candidate) {
                        if (!layer_visible(map, candidate->layer_id)) continue;
                        const auto dx = candidate->transform.position.x - world.x;
                        const auto dy = candidate->transform.position.y - world.y;
                        const auto distance = std::sqrt(dx * dx + dy * dy);
                        if (distance <= best_distance) {
                            best_distance = distance;
                            hit = candidate;
                        }
                    }
                    if (!selection_box.append) selected_instances.clear();
                    if (hit != map.instances.end()) {
                        const auto existing = std::find(selected_instances.begin(),
                                                        selected_instances.end(), hit->id);
                        if (selection_box.append && existing != selected_instances.end())
                            selected_instances.erase(existing);
                        else if (existing == selected_instances.end())
                            selected_instances.push_back(hit->id);
                        status = "Canvas selection changed";
                    }
                }
                selection_box.active = false;
            }
        }
        if (!mechanic_pointer_captured && !mechanic_gizmo.active &&
            !gizmo.active && !selection_box.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto world = screen_to_world(io.MousePos);
            auto hit = map.instances.end();
            float best_distance = 12.0F / zoom;
            for (auto candidate = map.instances.begin(); candidate != map.instances.end();
                 ++candidate) {
                if (!layer_visible(map, candidate->layer_id)) continue;
                const auto dx = candidate->transform.position.x - world.x;
                const auto dy = candidate->transform.position.y - world.y;
                const auto distance = std::sqrt(dx * dx + dy * dy);
                if (distance <= best_distance) {
                    best_distance = distance;
                    hit = candidate;
                }
            }
            if (!io.KeyShift) selected_instances.clear();
            if (hit != map.instances.end()) {
                const auto selected = std::find(selected_instances.begin(),
                                                selected_instances.end(), hit->id);
                if (io.KeyShift && selected != selected_instances.end())
                    selected_instances.erase(selected);
                else if (selected == selected_instances.end())
                    selected_instances.push_back(hit->id);
                status = "Canvas selection changed";
            }
        }
    }
    ImGui::TextDisabled("Zoom %.2fx · pan %.1f, %.1f · clic: sélectionner · molette: zoom · bouton milieu: déplacer",
                       zoom, pan.x, pan.y);
}

} // namespace fabric::map_studio
