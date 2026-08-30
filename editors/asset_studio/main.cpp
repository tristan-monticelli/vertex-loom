#include "fabric/editor/canvas_interaction.hpp"
#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/behavior_session.hpp"
#include "fabric/editor/session_transition.hpp"
#include "fabric/editor/transformation_session.hpp"
#include "fabric/editor/visual_presets.hpp"
#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/render/textured_path_geometry.hpp"
#include "fabric/render/vector_geometry.hpp"
#include "fabric/render/visual_composition_renderer.hpp"
#include "import_workflow.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>
#include <nfd.h>
#include <nfd_sdl2.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using fabric::asset_studio::AssetPreview;
using fabric::asset_studio::ImportUiState;
using fabric::asset_studio::PreviewKind;
using fabric::asset_studio::SourceImportFields;
using fabric::asset_studio::clear_asset_preview;
using fabric::asset_studio::draw_import_workflow;
using fabric::asset_studio::upload_preview;

fabric::editor::ProjectSession* active_picker_session = nullptr;
std::unordered_map<std::string, AssetPreview>* active_picker_texture_cache = nullptr;
bool ui_focus_probe_enabled = false;
bool ui_focus_probe_succeeded = false;

struct ResourceDragPayload {
    int kind{};
    char id[256]{};
};

bool is_entity_artwork_kind(const fabric::editor::StudioResourceKind kind) {
    return kind == fabric::editor::StudioResourceKind::texture ||
        kind == fabric::editor::StudioResourceKind::vector ||
        kind == fabric::editor::StudioResourceKind::visual_component;
}

constexpr ImGuiWindowFlags fixed_panel_flags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

std::string input_binding_label(const fabric::project::InputBinding& binding) {
    if (binding.device == fabric::project::InputDevice::keyboard) {
        const auto* name = SDL_GetKeyName(static_cast<SDL_Keycode>(binding.code));
        return (name != nullptr && *name != '\0') ? std::string(name) : "Unknown key";
    }
    return "Gamepad button " + std::to_string(binding.code);
}

void draw_disabled_reason(const bool disabled, const std::string_view reason) {
    if (!disabled || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        return;
    ImGui::SetTooltip("%s", std::string(reason).c_str());
}

void same_line_if_room(const float minimum_width = 96.0F) {
    if (ImGui::GetContentRegionAvail().x >= minimum_width)
        ImGui::SameLine();
}

bool draw_resource_name_field(const char* label, std::string& name,
                              const float width = 560.0F) {
    ImGui::SetNextItemWidth(width);
    return ImGui::InputText(label, &name);
}

void draw_resource_identity_fields(std::string& name, std::string& id) {
    static_cast<void>(draw_resource_name_field("Name##resource-name", name));
    ImGui::SetNextItemWidth(360.0F);
    ImGui::InputText("Resource id##resource-id", &id);
}

void draw_technical_tooltip(const std::string_view text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", std::string(text).c_str());
}

struct CreationUiState {
    struct BehaviorFields {
        std::string name{"Entity behavior"};
        std::string id{"entity-behavior"};
    } behavior;
    struct TransformationFields {
        std::string name{"Entity transformation"};
        std::string id{"entity-transformation"};
        std::string source_id;
        std::string destination_id;
    } transformation;
    struct VisualCompositionFields {
        std::string name{"Visual composition"};
        std::string id{"visual-composition"};
        float size[2]{10.0F, 10.0F};
    } composition;
    struct VisualComponentFields {
        std::string name{"Visual component"};
        std::string id{"visual-component"};
        std::string composition_id;
        float size[2]{10.0F, 10.0F};
    } component;
    fabric::editor::CreateProjectPrompt project;
    fabric::editor::CreateVectorArtworkPrompt artwork;
    fabric::editor::CreateMaterialPrompt material;
    fabric::editor::CreateEntityPrompt entity;
    fabric::editor::CreateAnimationPrompt animation;
    fabric::editor::CreateInputPrompt input;
    fabric::editor::VisualPresetRequest visual_preset;
    std::optional<fabric::editor::CreateVectorArtworkPrompt> prepared_artwork;
    bool request_project{};
    bool request_artwork{};
    bool request_material{};
    bool request_entity{};
    bool request_animation{};
    bool request_input{};
    bool request_visual_preset{};
    bool request_visual_composition{};
    bool request_visual_component{};
    bool request_behavior{};
    bool request_transformation{};
    bool project_publish_attempted{};
    bool input_capture{};
    std::size_t input_capture_action{};
    std::size_t input_capture_binding{};
    bool input_capture_existing{};
};

bool draw_typed_resource_reference(
    const char* label,
    const std::span<const fabric::editor::StudioResource> resources,
    fabric::project::ResourceReference& reference);

bool draw_behavior_node_picker(
    const char* label,
    const std::span<const fabric::project::BehaviorNodeDefinition> nodes,
    std::string& selected_id);

bool draw_behavior_port_picker(
    const char* label,
    const std::span<const fabric::project::BehaviorNodeDefinition> nodes,
    std::string_view node_id,
    std::string& selected_id,
    fabric::project::BehaviorPortDirection direction);

#if defined(__APPLE__)
constexpr const char* new_shortcut = "Cmd+N";
constexpr const char* open_shortcut = "Cmd+O";
constexpr const char* save_shortcut = "Cmd+S";
constexpr const char* import_shortcut = "Cmd+I";
constexpr const char* import_svg_shortcut = "Cmd+Shift+I";
constexpr const char* quit_shortcut = "Cmd+Q";
constexpr const char* undo_shortcut = "Cmd+Z";
constexpr const char* redo_shortcut = "Cmd+Shift+Z";
#else
constexpr const char* new_shortcut = "Ctrl+N";
constexpr const char* open_shortcut = "Ctrl+O";
constexpr const char* save_shortcut = "Ctrl+S";
constexpr const char* import_shortcut = "Ctrl+I";
constexpr const char* import_svg_shortcut = "Ctrl+Shift+I";
constexpr const char* quit_shortcut = "Ctrl+Q";
constexpr const char* undo_shortcut = "Ctrl+Z";
constexpr const char* redo_shortcut = "Ctrl+Shift+Z";
#endif

struct ProjectSettingsUiState {
    std::string name;
    double pixels_per_unit{fabric::project::default_pixels_per_unit};
    bool runtime_enabled{};
    float spawn_x{};
    float spawn_y{};
    std::array<std::string, 3> runtime_actions{};
    bool camera_follow_character{};
    bool camera_limits_enabled{};
    float camera_x{};
    float camera_y{};
    float camera_width{100.0F};
    float camera_height{100.0F};
    std::string audio_id;
    bool request{};
};

struct CanvasUiState {
    enum class Tool {
        move,
        rotate,
        scale,
        pivot,
        pen,
    };

    enum class DragOperation {
        none,
        move,
        rotate,
        scale,
        pivot,
        path_point,
        path_selection,
        bezier_handle1,
        bezier_handle2,
    };

    float zoom{1.0F};
    ImVec2 pan{};
    std::size_t selected_node{};
    bool native_canvas{};
    Tool tool{Tool::move};
    fabric::editor::BezierHandleMode bezier_handle_mode{
        fabric::editor::BezierHandleMode::linked};
    bool dragging{};
    DragOperation drag_operation{DragOperation::none};
    ImVec2 drag_start_mouse{};
    fabric::core::Transform drag_start_transform;
    fabric::project::VectorNode drag_start_node;
    bool entity_gizmo_dragging{};
    ImVec2 entity_gizmo_start_mouse{};
    ImVec2 entity_gizmo_screen{};
    fabric::core::Transform entity_gizmo_start_transform;
    std::size_t path_command_index{};
    std::vector<std::size_t> selected_path_points;
    ImVec2 native_origin{};
    ImVec2 native_size{};
    fabric::core::Rect native_world_bounds;
    fabric::core::Rect entity_world_bounds{{-5.0F, -5.0F}, {10.0F, 10.0F}};
    std::string crop_resource_id;
    std::optional<fabric::editor::RasterCropDrag> crop_drag;
    ImVec2 crop_start_mouse{};
    fabric::project::RasterView crop_start_view;
};

struct AnimationUiState {
    struct ClipboardEntry {
        fabric::project::PropertyBinding binding;
        fabric::project::AnimationKey key;
        fabric::project::AnimationInterpolation interpolation{
            fabric::project::AnimationInterpolation::linear};
        fabric::project::AnimationComposition composition{
            fabric::project::AnimationComposition::replace};
        fabric::project::AnimationEasing easing{
            fabric::project::AnimationEasing::linear};
    };
    struct KeySelection {
        fabric::project::PropertyBinding binding;
        std::size_t index{};
    };
    std::string node_id{"root"};
    std::string component_id{"transform"};
    std::string property_id{"position"};
    int binding_preset{};
    std::string visual_component_id;
    std::string marker_id{"marker"};
    float scrub_time{};
    float key_time{};
    float marker_time{};
    float key_value[2]{};
    int key_kind{};
    float key_scalar{};
    float key_color[4]{1.0F, 1.0F, 1.0F, 1.0F};
    bool tangents_enabled{};
    float key_in_tangent[2]{};
    float key_out_tangent[2]{};
    float key_in_tangent_scalar{};
    float key_out_tangent_scalar{};
    float key_in_tangent_color[4]{};
    float key_out_tangent_color[4]{};
    bool key_boolean{};
    bool auto_key{};
    std::string key_resource_id;
    float segment_start_time{};
    float segment_end_time{1.0F};
    float segment_start_value[2]{};
    float segment_end_value[2]{1.0F, 1.0F};
    float segment_start_scalar{};
    float segment_end_scalar{1.0F};
    float segment_start_color[4]{1.0F, 1.0F, 1.0F, 1.0F};
    float segment_end_color[4]{1.0F, 1.0F, 1.0F, 1.0F};
    bool segment_start_boolean{};
    bool segment_end_boolean{true};
    std::string segment_start_resource_id;
    std::string segment_end_resource_id;
    fabric::project::AnimationInterpolation interpolation{
        fabric::project::AnimationInterpolation::linear};
    fabric::project::AnimationEasing easing{
        fabric::project::AnimationEasing::linear};
    fabric::project::AnimationComposition composition{
        fabric::project::AnimationComposition::replace};
    bool snap_keys{true};
    float key_snap_interval{0.1F};
    std::vector<KeySelection> selected_keys;
    std::vector<ClipboardEntry> key_clipboard;
};

struct TexturedPathUiState {
    bool animate_texture{};
    float scroll_speed{1.0F};
    float preview_offset{};
};

struct EntityPreviewResult {
    std::vector<fabric::render::VectorDrawPacket> packets;
    fabric::core::Rect bounds{{-5.0F, -5.0F}, {10.0F, 10.0F}};
    std::vector<std::string> errors;
};

fabric::core::Vec2 transform_entity_point(
    const fabric::core::Vec2 point, const fabric::core::Transform& transform) {
    const auto local = fabric::core::Vec2{
        (point.x - transform.pivot.x) * transform.scale.x,
        (point.y - transform.pivot.y) * transform.scale.y};
    const float angle = transform.rotation_degrees *
        std::numbers::pi_v<float> / 180.0F;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {local.x * cosine - local.y * sine + transform.pivot.x +
                transform.position.x,
            local.x * sine + local.y * cosine + transform.pivot.y +
                transform.position.y};
}

fabric::core::Vec2 transform_entity_point(
    const fabric::core::Vec2 point, const fabric::project::EntityDefinition& entity,
    const std::size_t node_index) {
    const auto& node = entity.nodes[node_index];
    const auto transformed = transform_entity_point(point, node.transform);
    if (!node.parent) return transformed;
    const auto parent = std::find_if(
        entity.nodes.begin(), entity.nodes.end(),
        [&](const auto& candidate) { return candidate.id == *node.parent; });
    if (parent == entity.nodes.end()) return transformed;
    return transform_entity_point(
        transformed, entity,
        static_cast<std::size_t>(std::distance(entity.nodes.begin(), parent)));
}

void transform_entity_packet(fabric::render::VectorDrawPacket& packet,
                              const fabric::project::EntityDefinition& entity,
                              const std::size_t node_index) {
    for (auto& point : packet.outline) {
        point = transform_entity_point(point, entity, node_index);
    }
    for (auto& point : packet.fill_vertices) {
        point = transform_entity_point(point, entity, node_index);
    }
}

void apply_entity_material(fabric::render::VectorDrawPacket& packet,
                           const fabric::project::MaterialDefinition& material) {
    if (packet.fill_color) {
        packet.fill_color->red *= material.color.red;
        packet.fill_color->green *= material.color.green;
        packet.fill_color->blue *= material.color.blue;
        packet.fill_color->alpha *= material.color.alpha * material.opacity;
    } else if (!material.texture) {
        auto color = material.color;
        color.alpha *= material.opacity;
        packet.fill_color = color;
    }
    if (material.texture) {
        packet.image_fill = fabric::project::VectorImageFill{
            .texture = *material.texture,
            .transform = material.uv_transform,
            .opacity = material.opacity};
        packet.fill_color.reset();
        if (!packet.fill_vertices.empty()) {
            auto min_x = packet.fill_vertices.front().x;
            auto max_x = min_x;
            auto min_y = packet.fill_vertices.front().y;
            auto max_y = min_y;
            for (const auto point : packet.fill_vertices) {
                min_x = std::min(min_x, point.x);
                max_x = std::max(max_x, point.x);
                min_y = std::min(min_y, point.y);
                max_y = std::max(max_y, point.y);
            }
            const auto width = std::max(max_x - min_x, 1.0e-6F);
            const auto height = std::max(max_y - min_y, 1.0e-6F);
            packet.fill_uv.clear();
            for (const auto point : packet.fill_vertices) {
                packet.fill_uv.push_back(
                    {(point.x - min_x) / width, (point.y - min_y) / height});
            }
        }
    }
}

EntityPreviewResult build_entity_preview(
    const std::filesystem::path& project_root,
    const fabric::project::ProjectManifest& manifest,
    fabric::project::EntityDefinition entity,
    const fabric::project::AnimationClip* animation = nullptr,
    const float animation_time = 0.0F) {
    EntityPreviewResult result;
    std::optional<fabric::project::EvaluationResult> evaluated_animation;
    if (animation != nullptr) {
        evaluated_animation = fabric::project::evaluate_animation(
            *animation, animation_time);
        if (!evaluated_animation->ok()) {
            result.errors.push_back("animation: evaluation failed");
        }
        for (const auto& property : evaluated_animation->properties) {
            if (property.binding.component_id != "transform") continue;
            const auto node = std::find_if(
                entity.nodes.begin(), entity.nodes.end(),
                [&](const auto& candidate) {
                    return candidate.id == property.binding.node_id;
                });
            if (node == entity.nodes.end()) continue;
            const bool additive = property.composition ==
                fabric::project::AnimationComposition::additive;
            if (property.binding.property_id == "position") {
                if (const auto* value = std::get_if<fabric::core::Vec2>(
                        &property.value)) {
                    node->transform.position = additive
                        ? fabric::core::Vec2{
                              node->transform.position.x + value->x,
                              node->transform.position.y + value->y}
                        : *value;
                }
            } else if (property.binding.property_id == "rotationDegrees") {
                if (const auto* value = std::get_if<float>(&property.value)) {
                    node->transform.rotation_degrees = additive
                        ? node->transform.rotation_degrees + *value
                        : *value;
                }
            } else if (property.binding.property_id == "scale") {
                if (const auto* value = std::get_if<fabric::core::Vec2>(
                        &property.value)) {
                    node->transform.scale = additive
                        ? fabric::core::Vec2{
                              node->transform.scale.x + value->x,
                              node->transform.scale.y + value->y}
                        : *value;
                }
            }
        }
    }
    for (std::size_t node_index = 0; node_index < entity.nodes.size();
         ++node_index) {
        const auto& node = entity.nodes[node_index];
        if (!node.visible) continue;
        std::optional<fabric::project::MaterialDefinition> material;
        if (node.drawable.material) {
            const auto loaded = fabric::project::load_material(
                project_root, manifest,
                fabric::project::material_document_path(
                    manifest, node.drawable.material->id));
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors) {
                    result.errors.push_back(error.field + ": " + error.message);
                }
                continue;
            }
            material = *loaded.asset;
        }
        if (material && evaluated_animation && evaluated_animation->ok()) {
            for (const auto& property : evaluated_animation->properties) {
                if (property.binding.node_id != node.id ||
                    property.binding.component_id != "material") continue;
                const bool additive = property.composition ==
                    fabric::project::AnimationComposition::additive;
                if (property.binding.property_id == "opacity") {
                    if (const auto* value = std::get_if<float>(&property.value)) {
                        material->opacity = additive
                            ? material->opacity + *value : *value;
                    }
                } else if (property.binding.property_id == "color") {
                    if (const auto* value = std::get_if<fabric::core::Color>(
                            &property.value)) {
                        material->color = additive
                            ? fabric::core::Color{
                                  material->color.red + value->red,
                                  material->color.green + value->green,
                                  material->color.blue + value->blue,
                                  material->color.alpha + value->alpha}
                            : *value;
                    }
                }
            }
            material->opacity = std::clamp(material->opacity, 0.0F, 1.0F);
        }
        if (node.drawable.kind == fabric::project::EntityDrawableKind::vector &&
            node.drawable.resource) {
            auto loaded = fabric::project::load_vector_asset(
                project_root, manifest,
                fabric::project::vector_document_path(
                    manifest, node.drawable.resource->id));
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors) {
                    result.errors.push_back(error.field + ": " + error.message);
                }
                continue;
            }
            auto drawable = std::move(*loaded.asset);
            if (drawable.source_kind == fabric::project::VectorSourceKind::linked_svg) {
                auto converted = fabric::render::convert_svg_to_native(
                    project_root / drawable.source,
                    drawable.document.id, drawable.document.name);
                if (!converted.ok()) {
                    for (const auto& error : converted.errors) {
                        result.errors.push_back(error.field + ": " + error.message);
                    }
                    continue;
                }
                drawable = std::move(*converted.asset);
            }
            auto geometry = fabric::render::build_native_draw_packets(drawable);
            if (!geometry.ok()) {
                result.errors.insert(result.errors.end(), geometry.errors.begin(),
                                     geometry.errors.end());
                continue;
            }
            for (auto& packet : geometry.packets) {
                if (material) apply_entity_material(packet, *material);
                transform_entity_packet(packet, entity, node_index);
                packet.node_id = entity.document.id.value + ":" + node.id + ":" +
                    packet.node_id;
                result.packets.push_back(std::move(packet));
            }
        } else if (node.drawable.kind ==
                       fabric::project::EntityDrawableKind::visual_component &&
                   node.drawable.resource) {
            auto loaded = fabric::project::load_visual_component(
                project_root, manifest,
                fabric::project::visual_component_document_path(
                    manifest, node.drawable.resource->id));
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors)
                    result.errors.push_back(error.field + ": " + error.message);
                continue;
            }
            const auto component_instance =
                node.drawable.component_instance.value_or(
                    fabric::project::VisualComponentInstance{});
            auto visual = evaluated_animation && evaluated_animation->ok()
                ? fabric::render::resolve_animated_visual_component(
                      project_root, manifest,
                      *loaded.asset, component_instance, node.id,
                      *evaluated_animation)
                : fabric::render::resolve_visual_component(
                      project_root, manifest,
                      *loaded.asset, component_instance);
            result.errors.insert(result.errors.end(), visual.errors.begin(),
                                 visual.errors.end());
            for (auto& packet : visual.packets) {
                transform_entity_packet(packet, entity, node_index);
                packet.node_id = entity.document.id.value + ":" + node.id + ":" +
                    packet.node_id;
                result.packets.push_back(std::move(packet));
            }
        } else if (node.drawable.kind == fabric::project::EntityDrawableKind::texture &&
                   node.drawable.resource) {
            const auto loaded = fabric::project::load_texture_asset(
                project_root, manifest,
                fabric::project::texture_document_path(
                    manifest, node.drawable.resource->id));
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors) {
                    result.errors.push_back(error.field + ": " + error.message);
                }
                continue;
            }
            auto geometry = fabric::render::build_raster_view_draw_packets({
                .node_id = entity.document.id.value + ":" + node.id,
                .texture = *node.drawable.resource,
                .source_width = loaded.asset->width,
                .source_height = loaded.asset->height,
                .pixels_per_unit = static_cast<float>(
                    manifest.pixels_per_unit),
                .view = loaded.asset->view,
            });
            if (!geometry.ok()) {
                result.errors.insert(result.errors.end(), geometry.errors.begin(),
                                     geometry.errors.end());
                continue;
            }
            auto packet = std::move(geometry.packets.front());
            if (material) apply_entity_material(packet, *material);
            transform_entity_packet(packet, entity, node_index);
            result.packets.push_back(std::move(packet));
        }
    }
    if (!result.packets.empty()) {
        auto min_x = std::numeric_limits<float>::max();
        auto min_y = std::numeric_limits<float>::max();
        auto max_x = std::numeric_limits<float>::lowest();
        auto max_y = std::numeric_limits<float>::lowest();
        for (const auto& packet : result.packets) {
            const auto points = packet.fill_vertices.empty()
                ? packet.outline : packet.fill_vertices;
            for (const auto point : points) {
                min_x = std::min(min_x, point.x);
                min_y = std::min(min_y, point.y);
                max_x = std::max(max_x, point.x);
                max_y = std::max(max_y, point.y);
            }
        }
        const float margin = std::max(1.0F, std::max(max_x - min_x, max_y - min_y) * 0.1F);
        result.bounds = {{min_x - margin, min_y - margin},
                         {std::max(2.0F * margin, max_x - min_x + 2.0F * margin),
                          std::max(2.0F * margin, max_y - min_y + 2.0F * margin)}};
    }
    return result;
}

EntityPreviewResult build_entity_preview(
    const fabric::editor::ProjectSession& session,
    const fabric::project::AnimationClip* animation = nullptr,
    const float animation_time = 0.0F) {
    if (!session.selected_entity() || !session.manifest()) return {};
    return build_entity_preview(session.project_root(), *session.manifest(),
                                *session.selected_entity(), animation,
                                animation_time);
}

ImU32 color_to_u32(const fabric::core::Color& color);

void draw_entity_preview_thumbnail(const char* id,
                                   const EntityPreviewResult& preview,
                                   const ImVec2 size) {
    ImGui::InvisibleButton(id, size);
    const auto origin = ImGui::GetItemRectMin();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                        IM_COL32(20, 24, 31, 255), 5.0F);
    const auto bounds = preview.bounds;
    const float scale = std::max(0.001F, std::min(
        (size.x - 20.0F) / std::max(bounds.size.x, 0.001F),
        (size.y - 20.0F) / std::max(bounds.size.y, 0.001F)));
    const auto point = [&](const fabric::core::Vec2 value) {
        return ImVec2{
            origin.x + (value.x - bounds.origin.x) * scale + 10.0F,
            origin.y + size.y - (value.y - bounds.origin.y) * scale - 10.0F};
    };
    for (const auto& packet : preview.packets) {
        const auto color = packet.fill_color
            ? color_to_u32(*packet.fill_color)
            : IM_COL32(115, 170, 230, 220);
        for (std::size_t index = 0; index + 2U < packet.fill_indices.size();
             index += 3U) {
            const auto a = packet.fill_indices[index];
            const auto b = packet.fill_indices[index + 1U];
            const auto c = packet.fill_indices[index + 2U];
            if (a < packet.fill_vertices.size() &&
                b < packet.fill_vertices.size() &&
                c < packet.fill_vertices.size())
                draw->AddTriangleFilled(point(packet.fill_vertices[a]),
                                        point(packet.fill_vertices[b]),
                                        point(packet.fill_vertices[c]), color);
        }
        if (packet.outline.size() > 1U)
            for (std::size_t index = 1; index < packet.outline.size(); ++index)
                draw->AddLine(point(packet.outline[index - 1U]),
                              point(packet.outline[index]),
                              IM_COL32(225, 232, 240, 255), 1.5F);
    }
    if (preview.packets.empty())
        draw->AddText({origin.x + 10.0F, origin.y + 10.0F},
                      IM_COL32(150, 160, 175, 255), "No drawable");
}

fabric::render::VisualCompositionDrawResult build_visual_preview(
    const fabric::editor::ProjectSession& session,
    const TexturedPathUiState& path_ui) {
    if (!session.manifest() || session.selected_resource() == nullptr) return {};
    using Kind = fabric::editor::StudioResourceKind;
    const auto kind = session.selected_resource()->kind;
    if (kind == Kind::visual_component && session.selected_visual_component()) {
        return fabric::render::resolve_visual_component(
            session.project_root(), *session.manifest(),
            *session.selected_visual_component());
    }
    if (kind == Kind::visual_composition &&
        session.selected_visual_composition()) {
        return fabric::render::resolve_visual_composition(
            session.project_root(), *session.manifest(),
            *session.selected_visual_composition());
    }
    if (kind == Kind::textured_path && session.selected_textured_path()) {
        auto path = *session.selected_textured_path();
        path.uv_offset.x += path_ui.preview_offset;
        auto geometry = fabric::render::build_textured_path_draw_packets(path);
        fabric::render::VisualCompositionDrawResult result{
            .packets = std::move(geometry.packets),
            .errors = std::move(geometry.errors)};
        if (!result.packets.empty()) {
            const auto& points = result.packets.front().fill_vertices;
            auto min_x = std::ranges::min_element(points, {},
                &fabric::core::Vec2::x)->x;
            auto min_y = std::ranges::min_element(points, {},
                &fabric::core::Vec2::y)->y;
            auto max_x = std::ranges::max_element(points, {},
                &fabric::core::Vec2::x)->x;
            auto max_y = std::ranges::max_element(points, {},
                &fabric::core::Vec2::y)->y;
            const float margin = std::max(
                1.0F, std::max(max_x - min_x, max_y - min_y) * 0.1F);
            result.bounds = {{min_x - margin, min_y - margin},
                {max_x - min_x + 2.0F * margin,
                 max_y - min_y + 2.0F * margin}};
        }
        return result;
    }
    return {};
}

void copy_path_to_buffer(const std::filesystem::path& path,
                         std::array<char, 1024>& buffer) {
    const std::string value = path.string();
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
}

template <std::size_t Size>
bool choose_folder(SDL_Window* window, std::array<char, Size>& destination,
                   std::string& status) {
    nfdu8char_t* selected_path = nullptr;
    nfdpickfolderu8args_t arguments{};
    NFD_GetNativeWindowFromSDLWindow(window, &arguments.parentWindow);
    const nfdresult_t result = NFD_PickFolderU8_With(
        &selected_path, &arguments);
    if (result == NFD_CANCEL) {
        return false;
    }
    if (result == NFD_ERROR) {
        status = "Native folder dialog failed: " +
            std::string(NFD_GetError() == nullptr ? "unknown error"
                                                   : NFD_GetError());
        return false;
    }
    copy_path_to_buffer(std::filesystem::path{selected_path}, destination);
    NFD_FreePathU8(selected_path);
    return true;
}

void apply_studio_style() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 5.0F;
    style.ChildRounding = 4.0F;
    style.FrameRounding = 4.0F;
    style.TabRounding = 4.0F;
    style.WindowPadding = {10.0F, 10.0F};

    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = {0.055F, 0.063F, 0.078F, 1.0F};
    colors[ImGuiCol_TitleBg] = {0.075F, 0.086F, 0.105F, 1.0F};
    colors[ImGuiCol_TitleBgActive] = {0.11F, 0.13F, 0.16F, 1.0F};
    colors[ImGuiCol_Header] = {0.20F, 0.36F, 0.40F, 1.0F};
    colors[ImGuiCol_HeaderHovered] = {0.25F, 0.47F, 0.51F, 1.0F};
    colors[ImGuiCol_Button] = {0.20F, 0.36F, 0.40F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = {0.25F, 0.47F, 0.51F, 1.0F};
    colors[ImGuiCol_CheckMark] = {0.89F, 0.68F, 0.34F, 1.0F};
}

bool select_and_preview_resource(fabric::editor::ProjectSession& session,
                                 const fabric::editor::StudioResource& resource,
                                 AssetPreview& preview, std::string& status,
                                 const std::string_view status_prefix = "Selected: ") {
    if (!session.select_resource(resource.kind, resource.id)) {
        status = "Resource could not be loaded; inspect diagnostics.";
        return false;
    }
    if (session.imported_texture()) {
        upload_preview(preview, session.imported_texture()->image);
        preview.kind = PreviewKind::texture;
    } else if (session.imported_vector()) {
        upload_preview(preview, session.imported_vector()->preview);
        preview.kind = PreviewKind::vector;
    } else {
        clear_asset_preview(preview);
    }
    status = std::string(status_prefix) + resource.name;
    return true;
}

std::string_view studio_resource_kind_label(
    fabric::editor::StudioResourceKind kind);

bool duplicate_project_resource(
    fabric::editor::ProjectSession& session,
    const fabric::editor::StudioResource& resource,
    AssetPreview& preview, std::string& status,
    fabric::editor::ResourceDuplicationOptions options = {}) {
    const auto base = resource.id.value + "-copy";
    auto candidate = base;
    for (std::size_t suffix = 2U;
         std::ranges::any_of(session.resources(), [&](const auto& existing) {
             return existing.kind == resource.kind &&
                 existing.id.value == candidate;
         }); ++suffix)
        candidate = base + "-" + std::to_string(suffix);
    const auto copy_name = resource.name + " Copy";
    if (!session.duplicate_resource(resource.kind, resource.id,
                                    {.value = candidate}, copy_name,
                                    std::move(options))) {
        status = "Resource duplication failed; inspect diagnostics.";
        return false;
    }
    const auto* selected = session.selected_resource();
    return selected != nullptr &&
        select_and_preview_resource(session, *selected, preview, status,
                                    "Duplicated: ");
}

std::vector<fabric::editor::StudioResource> direct_duplication_candidates(
    fabric::editor::ProjectSession& session,
    const fabric::editor::StudioResource& resource) {
    using Json = nlohmann::json;
    std::vector<fabric::project::ResourceReference> references;
    std::ifstream input(resource.document_path);
    if (!input) return {};
    try {
        const auto document = Json::parse(input);
        const std::function<void(const Json&)> collect = [&](const Json& value) {
            if (value.is_object()) {
                const auto id = value.find("id");
                const auto type = value.find("expectedType");
                if (id != value.end() && type != value.end() &&
                    id->is_string() && type->is_string())
                    references.push_back({
                        {.value = id->get<std::string>()},
                        type->get<std::string>()});
                for (const auto& [_, child] : value.items()) collect(child);
            } else if (value.is_array()) {
                for (const auto& child : value) collect(child);
            }
        };
        collect(document);
    } catch (const nlohmann::json::exception&) {
        return {};
    }
    std::vector<fabric::editor::StudioResource> candidates;
    for (const auto& reference : references) {
        const auto candidate = std::ranges::find_if(
            session.resources(), [&](const auto& value) {
                const auto expected_type = [&] {
                    using Kind = fabric::editor::StudioResourceKind;
                    switch (value.kind) {
                    case Kind::textured_path: return std::string_view{"texturedPath"};
                    case Kind::visual_composition: return std::string_view{"visualComposition"};
                    case Kind::visual_component: return std::string_view{"visualComponent"};
                    default: return studio_resource_kind_label(value.kind);
                    }
                }();
                const bool type_matches = expected_type == reference.expected_type;
                return type_matches &&
                    value.id == reference.id;
            });
        if (candidate != session.resources().end() &&
            std::ranges::none_of(candidates, [&](const auto& value) {
                return value.kind == candidate->kind && value.id == candidate->id;
            }))
            candidates.push_back(*candidate);
    }
    return candidates;
}

std::string file_url(const std::filesystem::path& path) {
    constexpr char hexadecimal[] = "0123456789ABCDEF";
    std::string url{"file://"};
    for (const auto byte : path.generic_string()) {
        const auto value = static_cast<unsigned char>(byte);
        const bool safe = (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || byte == '/' || byte == ':' ||
            byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (safe) {
            url.push_back(byte);
        } else {
            url.push_back('%');
            url.push_back(hexadecimal[value >> 4U]);
            url.push_back(hexadecimal[value & 0x0FU]);
        }
    }
    return url;
}

void reveal_project_resource(
    const fabric::editor::ProjectSession& session,
    const fabric::editor::StudioResource& resource,
    std::string& status) {
    const auto directory = std::filesystem::absolute(
        session.project_root() / resource.document_path).parent_path();
    status = SDL_OpenURL(file_url(directory).c_str()) == 0
        ? "Resource folder opened."
        : std::string{"Could not reveal the resource: "} + SDL_GetError();
}

std::string_view studio_resource_kind_label(
    const fabric::editor::StudioResourceKind kind) {
    using Kind = fabric::editor::StudioResourceKind;
    switch (kind) {
    case Kind::texture: return "texture";
    case Kind::vector: return "vector";
    case Kind::material: return "material";
    case Kind::entity: return "entity";
    case Kind::animation: return "animation";
    case Kind::input: return "input";
    case Kind::audio: return "audio";
    case Kind::behavior: return "behavior";
    case Kind::transformation: return "transformation";
    case Kind::textured_path: return "textured path";
    case Kind::visual_composition: return "composition";
    case Kind::visual_component: return "component";
    case Kind::map: return "map";
    case Kind::scene: return "scene";
    case Kind::mechanic: return "mechanic";
    case Kind::replay: return "replay";
    }
    return "resource";
}

void draw_project_tree(fabric::editor::ProjectSession& session,
                       AssetPreview& preview, std::string& status) {
    if (!session.has_project()) {
        ImGui::TextDisabled("No project open");
        ImGui::Spacing();
        ImGui::SeparatorText("Current state");
        ImGui::BulletText("Current project: none");
        ImGui::BulletText("Active resource: none");
        ImGui::TextWrapped("Next action: create a project or open an existing one.");
        ImGui::TextDisabled("Shortcuts: Cmd/Ctrl+O open project, Cmd/Ctrl+N create project");
        ImGui::TextWrapped("Create or open a Vertex Loom project to begin.");
        return;
    }

    static ImGuiTextFilter filter;
    static std::optional<fabric::editor::StudioResource> delete_request;
    static std::vector<fabric::editor::StudioResource> delete_impact;
    static std::string replacement_id;
    static std::optional<fabric::editor::StudioResource> rename_request;
    static std::string rename_value;
    static std::optional<fabric::editor::StudioResource> duplicate_options_request;
    static std::vector<fabric::editor::StudioResource> duplicate_candidates;
    static std::vector<char> duplicate_candidate_selected;
    bool open_delete_popup = false;
    bool open_rename_popup = false;
    bool open_duplicate_options_popup = false;
    const auto request_delete = [&](const fabric::editor::StudioResource& resource) {
        const auto incoming = session.incoming_references(
            resource.kind, resource.id);
        if (!incoming) {
            status = "Reference analysis failed; inspect diagnostics.";
            return;
        }
        delete_request = resource;
        delete_impact = *incoming;
        replacement_id.clear();
        open_delete_popup = true;
    };
    const auto request_rename = [&](const fabric::editor::StudioResource& resource) {
        rename_request = resource;
        rename_value = resource.name;
        open_rename_popup = true;
    };
    const auto request_duplicate_options =
        [&](const fabric::editor::StudioResource& resource) {
            duplicate_options_request = resource;
            duplicate_candidates = direct_duplication_candidates(session, resource);
            duplicate_candidate_selected.assign(duplicate_candidates.size(), false);
            open_duplicate_options_popup = true;
        };
    filter.Draw("Search", -1.0F);
    static int kind_filter{};
    const char* kind_filters[] = {
        "All types", "Textures", "Vector artworks", "Materials / fills",
        "Entities", "Animations", "Input bindings", "Behaviors",
        "Transformations", "Textured paths", "Visual compositions",
        "Visual components", "Maps", "Scenes", "Mechanics", "Replays",
        "Audio"};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("##resource-kind-filter", &kind_filter, kind_filters,
                 static_cast<int>(std::size(kind_filters)));
    if (const auto* selected = session.selected_resource()) {
        if (ImGui::Button("Duplicate")) {
            const auto resource = *selected;
            duplicate_project_resource(session, resource, preview, status);
        }
        same_line_if_room();
        if (ImGui::Button("Rename...")) request_rename(*selected);
        same_line_if_room();
        if (ImGui::Button("Copy ID")) {
            SDL_SetClipboardText(selected->id.value.c_str());
            status = "Resource ID copied.";
        }
        same_line_if_room();
        if (ImGui::Button("Copy path")) {
            const auto path = selected->document_path.generic_string();
            SDL_SetClipboardText(path.c_str());
            status = "Resource path copied.";
        }
        same_line_if_room();
        if (ImGui::Button("Reveal"))
            reveal_project_resource(session, *selected, status);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.55F, 0.16F, 0.15F, 1.0F});
        if (ImGui::Button("Delete...")) request_delete(*selected);
        ImGui::PopStyleColor();
        if (session.can_restore_trashed_resource()) {
            same_line_if_room();
            if (ImGui::Button("Undo delete")) {
                status = session.restore_trashed_resource()
                    ? "Deleted resource restored."
                    : "Restore failed; inspect diagnostics.";
            }
        }
    }
    ImGui::Spacing();
    const auto draw_kind = [&](const char* label,
                               const fabric::editor::StudioResourceKind kind,
                               const int filter_index) {
        if (kind_filter != 0 && kind_filter != filter_index) return;
        bool any = false;
        for (const auto& resource : session.resources())
            if (resource.kind == kind &&
                filter.PassFilter(resource.name.c_str(),
                                  resource.id.value.c_str())) any = true;
        if (!any && kind_filter == 0) return;
        const auto flags = ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!ImGui::TreeNodeEx(label, flags)) return;
        std::optional<fabric::editor::StudioResource> duplicate_request;
        std::optional<fabric::editor::StudioResource> context_delete_request;
        std::optional<fabric::editor::StudioResource> context_rename_request;
        for (const auto& resource : session.resources()) {
            if (resource.kind != kind ||
                !filter.PassFilter(resource.name.c_str(),
                                   resource.id.value.c_str())) {
                continue;
            }
            any = true;
            const auto* selected = session.selected_resource();
            const bool is_selected = selected != nullptr &&
                selected->kind == resource.kind && selected->id == resource.id;
            const std::string item_label = resource.name + "##resource-row-" +
                resource.id.value;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                select_and_preview_resource(session, resource, preview, status);
            }
            if (is_entity_artwork_kind(resource.kind) &&
                ImGui::BeginDragDropSource()) {
                ResourceDragPayload payload;
                payload.kind = static_cast<int>(resource.kind);
                std::snprintf(payload.id, sizeof(payload.id), "%s",
                              resource.id.value.c_str());
                ImGui::SetDragDropPayload("VERTEX_LOOM_RESOURCE", &payload,
                                          sizeof(payload));
                ImGui::Text("Drag %s", resource.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Duplicate")) {
                    duplicate_request = resource;
                }
                if (ImGui::MenuItem("Duplicate with selected dependencies..."))
                    request_duplicate_options(resource);
                if (ImGui::MenuItem("Rename..."))
                    context_rename_request = resource;
                if (ImGui::MenuItem("Copy ID")) {
                    SDL_SetClipboardText(resource.id.value.c_str());
                    status = "Resource ID copied.";
                }
                if (ImGui::MenuItem("Copy path")) {
                    const auto path = resource.document_path.generic_string();
                    SDL_SetClipboardText(path.c_str());
                    status = "Resource path copied.";
                }
                if (ImGui::MenuItem("Reveal on disk"))
                    reveal_project_resource(session, resource, status);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete..."))
                    context_delete_request = resource;
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s%s", resource.id.value.c_str(),
                                is_selected && session.dirty() ? "  •" : "");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s",
                    resource.document_path.generic_string().c_str());
            if (resource.native) {
                ImGui::SameLine();
                ImGui::TextDisabled("native");
            }
        }
        if (duplicate_request)
            duplicate_project_resource(
                session, *duplicate_request, preview, status);
        if (context_delete_request)
            request_delete(*context_delete_request);
        if (context_rename_request)
            request_rename(*context_rename_request);
        if (!any) {
            ImGui::TextDisabled("None");
        }
        ImGui::TreePop();
    };
    draw_kind("Textures", fabric::editor::StudioResourceKind::texture, 1);
    draw_kind("Vector artworks", fabric::editor::StudioResourceKind::vector, 2);
    draw_kind("Materials / fills", fabric::editor::StudioResourceKind::material, 3);
    draw_kind("Entities", fabric::editor::StudioResourceKind::entity, 4);
    draw_kind("Animations", fabric::editor::StudioResourceKind::animation, 5);
    draw_kind("Input bindings", fabric::editor::StudioResourceKind::input, 6);
    draw_kind("Behaviors", fabric::editor::StudioResourceKind::behavior, 7);
    draw_kind("Transformations",
              fabric::editor::StudioResourceKind::transformation, 8);
    draw_kind("Textured paths",
              fabric::editor::StudioResourceKind::textured_path, 9);
    draw_kind("Visual compositions",
              fabric::editor::StudioResourceKind::visual_composition, 10);
    draw_kind("Visual components",
              fabric::editor::StudioResourceKind::visual_component, 11);
    draw_kind("Maps", fabric::editor::StudioResourceKind::map, 12);
    draw_kind("Scenes", fabric::editor::StudioResourceKind::scene, 13);
    draw_kind("Mechanics", fabric::editor::StudioResourceKind::mechanic, 14);
    draw_kind("Replays", fabric::editor::StudioResourceKind::replay, 15);
    draw_kind("Audio", fabric::editor::StudioResourceKind::audio, 16);

    if (open_delete_popup) ImGui::OpenPopup("Delete resource");
    if (open_duplicate_options_popup)
        ImGui::OpenPopup("Duplicate with dependencies");
    if (ImGui::BeginPopupModal("Duplicate with dependencies", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!duplicate_options_request) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextWrapped("Choose dependencies to clone for %s.",
                               duplicate_options_request->name.c_str());
            if (duplicate_candidates.empty())
                ImGui::TextDisabled("No supported direct dependencies found.");
            for (std::size_t index = 0; index < duplicate_candidates.size(); ++index) {
                auto& candidate = duplicate_candidates[index];
                ImGui::PushID(static_cast<int>(index));
                bool selected_dependency = duplicate_candidate_selected[index] != 0;
                if (ImGui::Checkbox(candidate.name.c_str(), &selected_dependency))
                    duplicate_candidate_selected[index] = selected_dependency ? 1 : 0;
                ImGui::SameLine();
                ImGui::TextDisabled("%s · %s", candidate.id.value.c_str(),
                                    studio_resource_kind_label(candidate.kind).data());
                ImGui::PopID();
            }
            if (ImGui::Button("Duplicate")) {
                fabric::editor::ResourceDuplicationOptions options;
                for (std::size_t index = 0; index < duplicate_candidates.size(); ++index) {
                    if (!duplicate_candidate_selected[index]) continue;
                    const auto& dependency = duplicate_candidates[index];
                    options.dependencies.push_back({
                        .kind = dependency.kind,
                        .source_id = dependency.id,
                        .destination_id = {.value = dependency.id.value + "-copy"},
                        .destination_name = dependency.name + " Copy"});
                }
                duplicate_project_resource(session, *duplicate_options_request,
                                           preview, status, std::move(options));
                duplicate_options_request.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                duplicate_options_request.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete resource", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!delete_request) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Delete %s?", delete_request->name.c_str());
            ImGui::TextDisabled("%s · %s",
                delete_request->id.value.c_str(),
                delete_request->document_path.generic_string().c_str());
            ImGui::Spacing();
            if (delete_impact.empty()) {
                ImGui::TextWrapped(
                    "The document will move to the recoverable project trash. Shared PNG/SVG sources are kept.");
            } else {
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                   "Blocked: %zu incoming reference(s)",
                                   delete_impact.size());
                for (const auto& source : delete_impact)
                    ImGui::BulletText("%s (%s)", source.name.c_str(),
                                      source.id.value.c_str());
                ImGui::TextWrapped(
                    "Replace the references or cancel before deleting this resource.");
                ImGui::SeparatorText("Replacement");
                const auto replacement = std::ranges::find_if(
                    session.resources(), [&](const auto& resource) {
                        return resource.kind == delete_request->kind &&
                            resource.id.value == replacement_id;
                    });
                const std::string replacement_label =
                    replacement != session.resources().end()
                    ? replacement->name
                    : replacement_id.empty()
                    ? std::string{"Choose a replacement..."}
                    : std::string{"Missing: "} + replacement_id;
                if (ImGui::BeginCombo("Resource", replacement_label.c_str())) {
                    for (const auto& candidate : session.resources()) {
                        if (candidate.kind != delete_request->kind ||
                            candidate.id == delete_request->id) continue;
                        const bool selected = candidate.id.value == replacement_id;
                        if (ImGui::Selectable(candidate.name.c_str(), selected))
                            replacement_id = candidate.id.value;
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", candidate.id.value.c_str());
                    }
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(replacement == session.resources().end());
                if (ImGui::Button("Replace references")) {
                    const auto target = *delete_request;
                    if (session.replace_incoming_references(
                            target.kind, target.id, {.value = replacement_id})) {
                        const auto remaining = session.incoming_references(
                            target.kind, target.id);
                        delete_impact = remaining ? *remaining : delete_impact;
                        replacement_id.clear();
                        status = "References replaced; resource can now be moved to trash.";
                    } else {
                        status = "Reference replacement failed; inspect diagnostics.";
                    }
                }
                ImGui::EndDisabled();
                draw_disabled_reason(replacement == session.resources().end(),
                                     "Choose a valid same-type replacement resource first.");
            }
            ImGui::BeginDisabled(!delete_impact.empty());
            ImGui::PushStyleColor(
                ImGuiCol_Button, ImVec4{0.55F, 0.16F, 0.15F, 1.0F});
            if (ImGui::Button("Move to trash", {140.0F, 0.0F})) {
                const auto target = *delete_request;
                if (session.trash_resource(target.kind, target.id, true)) {
                    clear_asset_preview(preview);
                    status = "Resource moved to project trash; Undo delete is available.";
                    delete_request.reset();
                    delete_impact.clear();
                    replacement_id.clear();
                    ImGui::CloseCurrentPopup();
                } else {
                    status = "Delete failed; inspect diagnostics.";
                }
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            draw_disabled_reason(!delete_impact.empty(),
                                 "Resolve the incoming references before moving this resource to trash.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100.0F, 0.0F})) {
                delete_request.reset();
                delete_impact.clear();
                replacement_id.clear();
                ImGui::CloseCurrentPopup();
                status = "Delete cancelled.";
            }
        }
        ImGui::EndPopup();
    }
    if (open_rename_popup) ImGui::OpenPopup("Rename resource");
    if (ImGui::BeginPopupModal("Rename resource", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!rename_request) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextDisabled("Stable ID: %s",
                                rename_request->id.value.c_str());
            draw_resource_name_field("Visible name", rename_value, 420.0F);
            ImGui::BeginDisabled(rename_value.empty());
            if (ImGui::Button("Rename", {110.0F, 0.0F})) {
                const auto target = *rename_request;
                if (session.rename_resource(
                        target.kind, target.id, rename_value)) {
                    status = "Resource renamed; stable ID and path kept.";
                    rename_request.reset();
                    ImGui::CloseCurrentPopup();
                } else {
                    status = "Rename failed; inspect diagnostics.";
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(rename_value.empty(),
                                 "Enter a visible name before renaming the resource.");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100.0F, 0.0F})) {
                rename_request.reset();
                ImGui::CloseCurrentPopup();
                status = "Rename cancelled.";
            }
        }
        ImGui::EndPopup();
    }
}

void draw_existing_resource_popup(fabric::editor::ProjectSession& session,
                                  AssetPreview& preview,
                                  std::string& status) {
    if (!ImGui::BeginPopup("Add existing resource")) {
        return;
    }
    ImGui::TextWrapped(
        "Select a resource already registered in this project. No document is created.");
    ImGui::Separator();
    bool any = false;
    for (const auto& resource : session.resources()) {
        any = true;
        const auto* selected = session.selected_resource();
        const bool is_selected = selected != nullptr &&
            selected->kind == resource.kind && selected->id == resource.id;
        const std::string label = resource.name + " (" +
            std::string(studio_resource_kind_label(resource.kind)) +
            ")##existing-" + resource.id.value;
        if (ImGui::Selectable(label.c_str(), is_selected)) {
            if (select_and_preview_resource(session, resource, preview, status,
                                             "Added existing resource: ")) {
                ImGui::CloseCurrentPopup();
            }
        }
    }
    if (!any) {
        ImGui::TextDisabled("No registered resources.");
    }
    ImGui::EndPopup();
}

std::string_view diagnostic_expectation(const fabric::project::ErrorCode code) {
    using fabric::project::ErrorCode;
    switch (code) {
    case ErrorCode::io_error: return "the file operation is writable";
    case ErrorCode::invalid_json: return "valid JSON for the resource schema";
    case ErrorCode::invalid_manifest: return "a valid project manifest";
    case ErrorCode::unsupported_schema_version:
        return "a supported schema version";
    case ErrorCode::invalid_resource_id:
        return "a non-empty, portable resource ID";
    case ErrorCode::invalid_path: return "a relative path inside the project";
    case ErrorCode::missing_file: return "an existing referenced file";
    case ErrorCode::missing_directory: return "an existing referenced directory";
    case ErrorCode::directory_not_empty: return "an empty destination directory";
    case ErrorCode::invalid_asset: return "an asset matching its contract";
    case ErrorCode::asset_already_exists: return "a unique asset destination";
    case ErrorCode::duplicate_resource: return "a unique resource identity";
    case ErrorCode::missing_resource: return "an indexed resource reference";
    case ErrorCode::resource_type_mismatch:
        return "a reference with the expected resource type";
    case ErrorCode::resource_cycle: return "an acyclic resource graph";
    }
    return "a valid project value";
}

std::string_view diagnostic_action(const fabric::project::ErrorCode code) {
    using fabric::project::ErrorCode;
    switch (code) {
    case ErrorCode::io_error: return "check permissions and retry";
    case ErrorCode::invalid_json: return "correct the document and validate again";
    case ErrorCode::invalid_manifest:
    case ErrorCode::unsupported_schema_version:
        return "restore a supported manifest or migrate the project";
    case ErrorCode::invalid_resource_id:
    case ErrorCode::invalid_path:
        return "edit the value in the indicated field";
    case ErrorCode::missing_file:
    case ErrorCode::missing_directory:
        return "restore the missing project asset or choose another reference";
    case ErrorCode::directory_not_empty:
    case ErrorCode::asset_already_exists:
    case ErrorCode::duplicate_resource:
        return "choose a different destination or identifier";
    case ErrorCode::invalid_asset:
    case ErrorCode::missing_resource:
    case ErrorCode::resource_type_mismatch:
        return "choose a compatible resource and validate again";
    case ErrorCode::resource_cycle:
        return "remove the cyclic reference";
    }
    return "correct the field and validate again";
}

void draw_diagnostics(const fabric::editor::ProjectSession& session) {
    if (session.errors().empty()) {
        ImGui::TextDisabled("Validation diagnostics will appear here.");
        return;
    }

    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s - %s",
                           std::string(fabric::project::to_string(error.code)).c_str(),
                           error.field.c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("Cause: %s", error.message.c_str());
        ImGui::TextWrapped("Expected: %s",
                           diagnostic_expectation(error.code).data());
        ImGui::TextWrapped("Action: %s", diagnostic_action(error.code).data());
        ImGui::Separator();
    }
}

void write_frame_capture(const std::filesystem::path& project_path,
                         SDL_Window* window, const char* filename) {
    if (project_path.empty() || window == nullptr || filename == nullptr) return;
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream image(project_path / filename, std::ios::binary);
    if (!image) return;
    image << "P6\n" << width << ' ' << height << "\n255\n";
    const auto row_size = static_cast<std::size_t>(width) * 3U;
    for (int row = height - 1; row >= 0; --row) {
        image.write(reinterpret_cast<const char*>(pixels.data() +
                                                   static_cast<std::size_t>(row) *
                                                       row_size),
                    static_cast<std::streamsize>(row_size));
    }
}

void write_e2e_failure_artifacts(const std::filesystem::path& project_path,
                                 SDL_Window* window,
                                 const std::string& status,
                                 const fabric::editor::ProjectSession& session) {
    if (project_path.empty() || window == nullptr) return;
    std::ofstream report(project_path / "asset_studio-e2e-failure.txt");
    if (report) {
        report << "status: " << status << '\n';
        for (const auto& error : session.errors()) {
            report << fabric::project::to_string(error.code) << " | "
                   << error.field << " | " << error.message << '\n';
        }
    }
    write_frame_capture(project_path, window, "asset_studio-e2e-failure.ppm");
}

void write_ui_test_registry(const std::filesystem::path& project_path,
                            const fabric::editor::ProjectSession& session) {
    if (project_path.empty()) return;
    nlohmann::json registry = {
        {"schema", "asset-studio-ui-test-v1"},
        {"widgets", nlohmann::json::array()}};
    auto& widgets = registry["widgets"];
    for (const auto& resource : session.resources()) {
        widgets.push_back({
            {"id", "resource-row-" + resource.id.value},
            {"kind", "resource"},
            {"resource_kind", studio_resource_kind_label(resource.kind)},
            {"resource_id", resource.id.value}});
    }
    if (session.selected_entity()) {
        for (const auto& node : session.selected_entity()->nodes) {
            widgets.push_back({
                {"id", "entity-node-" + node.id},
                {"kind", "entity_node"},
                {"node_id", node.id}});
        }
    }
    std::ofstream output(project_path / "asset-studio-ui-widgets.json");
    if (output) output << registry.dump(2) << '\n';
}

void write_ui_focus_probe(const std::filesystem::path& project_path,
                          const bool focused) {
    if (project_path.empty()) return;
    nlohmann::json probe = {
        {"schema", "asset-studio-ui-focus-test-v1"},
        {"focused_first_invalid_field", focused},
        {"scroll_requested", true}};
    std::ofstream output(project_path / "asset-studio-ui-focus.json");
    if (output) output << probe.dump(2) << '\n';
}

void draw_behavior_editor(fabric::editor::ProjectSession& project_session,
                          fabric::editor::BehaviorSession& behavior_session,
                          CreationUiState& creation, std::string& status) {
    if (creation.request_behavior) {
        ImGui::OpenPopup("Create behavior");
        creation.request_behavior = false;
    }
    if (ImGui::BeginPopupModal("Create behavior", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Create a BehaviorGraph v1 attachable to any entity.");
        draw_resource_identity_fields(creation.behavior.name,
                                      creation.behavior.id);
        const bool valid = !creation.behavior.name.empty() &&
            fabric::core::ResourceId::is_valid(creation.behavior.id);
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Create")) {
            fabric::project::BehaviorGraph graph;
            graph.document.id = {.value = creation.behavior.id};
            graph.document.name = creation.behavior.name;
            if (behavior_session.create(project_session.project_root(), graph) &&
                project_session.refresh_resources() &&
                project_session.select_resource(
                    fabric::editor::StudioResourceKind::behavior,
                    graph.document.id)) {
                status = "Behavior created and saved.";
                ImGui::CloseCurrentPopup();
            } else status = "Behavior creation failed; inspect diagnostics.";
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Enter a non-empty name and a valid resource ID.");
        draw_disabled_reason(!valid,
                             "Enter a non-empty name and a valid unique resource id.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const auto* selected = project_session.selected_resource();
    if (!selected || selected->kind != fabric::editor::StudioResourceKind::behavior)
        return;
    if (!behavior_session.has_graph() ||
        behavior_session.graph()->document.id != selected->id) {
        if (!behavior_session.open(project_session.project_root(), selected->id)) {
            status = "Behavior could not be opened.";
            return;
        }
    }

    ImGui::SetNextWindowSize({720.0F, 620.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Behavior Graph")) { ImGui::End(); return; }
    const auto& graph = *behavior_session.graph();
    ImGui::Text("%s", graph.document.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", graph.document.id.value.c_str());
    if (ImGui::Button("Save behavior"))
        status = behavior_session.save() ? "Behavior saved." : "Behavior save failed.";
    ImGui::SameLine();
    ImGui::BeginDisabled(!behavior_session.can_undo());
    if (ImGui::Button("Undo##behavior")) static_cast<void>(behavior_session.undo());
    ImGui::EndDisabled();
    draw_disabled_reason(!behavior_session.can_undo(), "No behavior edit to undo.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!behavior_session.can_redo());
    if (ImGui::Button("Redo##behavior")) static_cast<void>(behavior_session.redo());
    ImGui::EndDisabled();
    draw_disabled_reason(!behavior_session.can_redo(), "No behavior edit to redo.");

    static int selected_node = -1;
    static int node_type = 0;
    static std::string new_node_id{"node"};
    static constexpr const char* node_types[] = {
        "action_source", "ai_source", "event_source", "trigger_source",
        "timer_source", "property_source", "condition", "branch", "sequence",
        "delay", "cooldown", "state", "transition", "set_property",
        "emit_event", "play_animation", "move", "activate_mechanic",
        "transform_entity"};
    ImGui::SeparatorText("Nodes");
    ImGui::SetNextItemWidth(190.0F);
    ImGui::Combo("Type", &node_type, node_types,
                 static_cast<int>(std::size(node_types)));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0F);
    ImGui::InputText("Id", &new_node_id);
    ImGui::SameLine();
    if (ImGui::Button("Add node")) {
        if (behavior_session.add_node(node_types[node_type], new_node_id)) {
            selected_node = static_cast<int>(behavior_session.graph()->nodes.size() - 1U);
            status = "Behavior node added.";
        } else status = "Node rejected; inspect Behavior diagnostics.";
    }
    if (ImGui::BeginChild("Behavior node list", {260.0F, 230.0F}, true)) {
        for (std::size_t index = 0; index < behavior_session.graph()->nodes.size(); ++index) {
            const auto& node = behavior_session.graph()->nodes[index];
            const std::string label = node.id + "  [" + node.type + "]##behavior-node";
            if (ImGui::Selectable(label.c_str(), selected_node == static_cast<int>(index)))
                selected_node = static_cast<int>(index);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Behavior node inspector", {0.0F, 230.0F}, true)) {
        if (selected_node >= 0 &&
            selected_node < static_cast<int>(behavior_session.graph()->nodes.size())) {
            const auto node = behavior_session.graph()->nodes[static_cast<std::size_t>(selected_node)];
            ImGui::Text("%s", node.type.c_str());
            for (const auto& property : node.properties) {
                ImGui::PushID(property.id.c_str());
                auto value = property.value;
                bool changed = false;
                const std::string numeric_label = property.id +
                    " (declared units)##behavior-numeric-" + property.id;
                if (auto* typed = std::get_if<bool>(&value)) changed = ImGui::Checkbox(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<std::int64_t>(&value)) {
                    int visible = static_cast<int>(*typed);
                    changed = ImGui::InputInt(numeric_label.c_str(), &visible);
                    *typed = visible;
                    ImGui::SetItemTooltip("Integer value interpreted using the behavior property schema.");
                } else if (auto* typed = std::get_if<float>(&value)) {
                    changed = ImGui::InputFloat(numeric_label.c_str(), typed);
                    ImGui::SetItemTooltip("Real value interpreted using the behavior property schema.");
                }
                else if (auto* typed = std::get_if<std::string>(&value)) changed = ImGui::InputText(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<fabric::core::Vec2>(&value)) {
                    float values[2]{typed->x, typed->y}; changed = ImGui::InputFloat2(numeric_label.c_str(), values);
                    *typed = {values[0], values[1]};
                    ImGui::SetItemTooltip("Vector value interpreted using the behavior property schema.");
                } else if (auto* typed = std::get_if<fabric::project::ResourceReference>(&value)) {
                    changed = draw_typed_resource_reference(
                        property.id.c_str(), project_session.resources(), *typed);
                    ImGui::TextDisabled("expected: %s", typed->expected_type.c_str());
                }
                if (changed) static_cast<void>(behavior_session.set_node_property(
                    {.value = node.id}, property.id, std::move(value)));
                ImGui::PopID();
            }
            if (ImGui::Button("Duplicate node")) {
                const auto copy_id = node.id + "-copy";
                if (behavior_session.duplicate_node({.value = node.id}, copy_id))
                    selected_node = static_cast<int>(behavior_session.graph()->nodes.size() - 1U);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, {0.55F, 0.16F, 0.16F, 1.0F});
            if (ImGui::Button("Delete node")) {
                if (behavior_session.remove_node({.value = node.id})) selected_node = -1;
            }
            ImGui::PopStyleColor();
        } else ImGui::TextDisabled("Select a node to edit all typed properties.");
    }
    ImGui::EndChild();

    static std::string connection_id{"connection"}, from_node, from_port{"out"},
        to_node, to_port{"in"};
    ImGui::SeparatorText("Connections");
    ImGui::InputText("Connection id", &connection_id);
    static_cast<void>(draw_behavior_node_picker(
        "From node", behavior_session.graph()->nodes, from_node)); ImGui::SameLine();
    static_cast<void>(draw_behavior_port_picker(
        "From port", behavior_session.graph()->nodes, from_node, from_port,
        fabric::project::BehaviorPortDirection::output));
    static_cast<void>(draw_behavior_node_picker(
        "To node", behavior_session.graph()->nodes, to_node)); ImGui::SameLine();
    static_cast<void>(draw_behavior_port_picker(
        "To port", behavior_session.graph()->nodes, to_node, to_port,
        fabric::project::BehaviorPortDirection::input));
    if (ImGui::Button("Connect"))
        static_cast<void>(behavior_session.connect({connection_id, from_node, from_port, to_node, to_port}));
    std::optional<std::string> remove_connection;
    for (const auto& connection : behavior_session.graph()->connections) {
        ImGui::BulletText("%s: %s.%s -> %s.%s", connection.id.c_str(),
                          connection.from_node.c_str(), connection.from_port.c_str(),
                          connection.to_node.c_str(), connection.to_port.c_str());
        ImGui::SameLine(); ImGui::PushID(connection.id.c_str());
        if (ImGui::SmallButton("Remove")) remove_connection = connection.id;
        ImGui::PopID();
    }
    if (remove_connection)
        static_cast<void>(behavior_session.disconnect({.value = *remove_connection}));

    static int signal_source = 0;
    static std::string semantic_id{"action"};
    static constexpr const char* source_labels[] = {"Action", "AI", "Map event", "Trigger", "Timer", "Property"};
    ImGui::SeparatorText("Step preview");
    ImGui::Combo("Signal source", &signal_source, source_labels, 6);
    ImGui::InputText("Semantic id", &semantic_id);
    if (ImGui::Button("Evaluate one fixed step")) {
        const auto actions = behavior_session.preview(
            {static_cast<fabric::runtime::BehaviorSignalSource>(signal_source), semantic_id, {}},
            1.0F / 60.0F);
        status = "Behavior preview produced " + std::to_string(actions.size()) + " action(s).";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset preview")) behavior_session.reset_preview();
    if (ImGui::BeginChild("Behavior trace", {0.0F, 100.0F}, true))
        for (const auto& entry : behavior_session.trace())
            ImGui::Text("%s — %s", entry.node_id.c_str(), entry.message.c_str());
    ImGui::EndChild();
    for (const auto& error : behavior_session.errors())
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s: %s",
                           error.field.c_str(), error.message.c_str());
    ImGui::End();
}

void draw_prompt_error(const fabric::editor::PromptValidation& validation,
                       const std::string_view field) {
    if (const auto error = validation.error_for(field)) {
        static int last_error_frame = -1;
        const auto frame = ImGui::GetFrameCount();
        if (last_error_frame != frame) {
            ImGui::SetScrollHereY(0.0F);
            last_error_frame = frame;
        }
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s",
                           std::string(*error).c_str());
    }
}

void focus_prompt_field(const fabric::editor::PromptValidation& validation,
                        const std::string_view field,
                        const std::string_view scope) {
    static std::string focused_error;
    if (validation.errors.empty()) {
        focused_error.clear();
        return;
    }
    if (validation.errors.front().field != field) return;
    const std::string key = std::string(scope) + ":" +
        std::to_string(ImGui::GetID(std::string(field).c_str())) + ":" +
        std::string(field) + ":" +
        validation.errors.front().message;
    if (focused_error == key) {
        if (ui_focus_probe_enabled && ImGui::IsItemFocused())
            ui_focus_probe_succeeded = true;
        return;
    }
    ImGui::SetKeyboardFocusHere(-1);
    ImGui::SetScrollHereY(0.0F);
    if (ui_focus_probe_enabled && ImGui::IsItemFocused())
        ui_focus_probe_succeeded = true;
    focused_error = key;
}

void draw_prompt_summary(const fabric::editor::PromptValidation& validation) {
    ImGui::SeparatorText("Review");
    for (const auto& line : validation.summary) {
        ImGui::TextWrapped("%s", line.c_str());
    }
}

bool text_contains_ascii_insensitive(const std::string_view text,
                                     const std::string_view query) {
    if (query.empty()) return true;
    const auto fold = [](const char value) {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value - 'A' + 'a') : value;
    };
    for (std::size_t start = 0; start + query.size() <= text.size(); ++start) {
        bool matches = true;
        for (std::size_t offset = 0; offset < query.size(); ++offset) {
            if (fold(text[start + offset]) != fold(query[offset])) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

bool draw_project_resource_picker(
    const char* label,
    const std::span<const fabric::editor::StudioResource> resources,
    const fabric::editor::StudioResourceKind expected_kind,
    std::string& selected_id, const bool optional) {
    const auto selected = std::ranges::find_if(
        resources, [&](const auto& resource) {
            return resource.kind == expected_kind &&
                resource.id.value == selected_id;
        });
    const std::string preview = selected != resources.end()
        ? selected->name
        : selected_id.empty() ? std::string{"Choose a project resource..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(420.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##resource-search", "Search by name or id...",
                                 &filter);
        if (optional && ImGui::Selectable("Clear selection", selected_id.empty())) {
            selected_id.clear();
            changed = true;
        }
        if (optional) ImGui::Separator();
        bool found = false;
        for (const auto& resource : resources) {
            if (resource.kind != expected_kind ||
                (!text_contains_ascii_insensitive(resource.name, filter) &&
                 !text_contains_ascii_insensitive(resource.id.value, filter))) {
                continue;
            }
            found = true;
            const bool is_selected = resource.id.value == selected_id;
            const std::string item_label = resource.name + "##resource-" +
                resource.id.value;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                selected_id = resource.id.value;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", resource.id.value.c_str());
        }
        if (!found) ImGui::TextDisabled("No matching project resource.");
        ImGui::EndCombo();
    }
    if (selected != resources.end()) {
        if (selected->kind == fabric::editor::StudioResourceKind::texture &&
            active_picker_session != nullptr && active_picker_texture_cache != nullptr) {
            auto& cache = *active_picker_texture_cache;
            auto cached = cache.find(selected->id.value);
            if (cached == cache.end() && active_picker_session->manifest()) {
                const auto loaded = fabric::project::load_texture_asset(
                    active_picker_session->project_root(),
                    *active_picker_session->manifest(), selected->document_path);
                if (loaded.ok()) {
                    const auto decoded = fabric::render::load_png(
                        active_picker_session->project_root() / loaded.asset->source);
                    if (decoded.ok()) {
                        AssetPreview thumbnail;
                        upload_preview(thumbnail, *decoded.image);
                        cached = cache.emplace(selected->id.value,
                                               std::move(thumbnail)).first;
                    }
                }
            }
            if (cached != cache.end() && cached->second.texture != 0U) {
                ImGui::Image(static_cast<ImTextureID>(cached->second.texture),
                             {64.0F, 64.0F});
            }
        }
        ImGui::TextDisabled("Type: %s",
                           studio_resource_kind_label(selected->kind).data());
        ImGui::TextDisabled("Path: %s",
                            selected->document_path.generic_string().c_str());
        const std::string dimensions =
            selected->width != 0U && selected->height != 0U
                ? std::to_string(selected->width) + "x" +
                    std::to_string(selected->height)
                : "n/a";
        ImGui::TextDisabled("Dimensions: %s", dimensions.c_str());
        ImGui::TextDisabled("Format: %s",
                            selected->format.empty() ? "n/a" :
                                                        selected->format.c_str());
        if (active_picker_session != nullptr) {
            const auto references = active_picker_session->incoming_references(
                selected->kind, selected->id);
            if (references) {
                ImGui::TextDisabled("Incoming references: %zu",
                                   references->size());
            }
            const std::string open_label = "Open in Resource Explorer##picker-" +
                selected->id.value;
            if (ImGui::SmallButton(open_label.c_str())) {
                static_cast<void>(active_picker_session->select_resource(
                    selected->kind, selected->id));
            }
        }
    }
    return changed;
}

bool draw_entity_node_picker(
    const char* label,
    const std::span<const fabric::project::EntityNode> nodes,
    std::string& selected_id) {
    const auto selected = std::ranges::find_if(
        nodes, [&](const auto& node) { return node.id == selected_id; });
    const std::string preview = selected != nodes.end()
        ? selected->name
        : selected_id.empty() ? std::string{"Choose an entity node..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(420.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##entity-node-search", "Search by name or id...",
                                 &filter);
        for (const auto& node : nodes) {
            if (!text_contains_ascii_insensitive(node.name, filter) &&
                !text_contains_ascii_insensitive(node.id, filter))
                continue;
            const bool is_selected = node.id == selected_id;
            const std::string item_label = node.name + "##entity-node-option-" +
                node.id;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                selected_id = node.id;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", node.id.c_str());
        }
        if (selected == nodes.end() && !selected_id.empty())
            ImGui::TextDisabled("Missing node reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_behavior_node_picker(
    const char* label,
    const std::span<const fabric::project::BehaviorNodeDefinition> nodes,
    std::string& selected_id) {
    const auto selected = std::ranges::find_if(
        nodes, [&](const auto& node) { return node.id == selected_id; });
    const std::string preview = selected != nodes.end()
        ? selected->id
        : selected_id.empty() ? std::string{"Choose a graph node..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(280.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##behavior-node-search", "Search node ID or type...",
                                 &filter);
        bool found = false;
        for (const auto& node : nodes) {
            if (!text_contains_ascii_insensitive(node.id, filter) &&
                !text_contains_ascii_insensitive(node.type, filter))
                continue;
            found = true;
            const bool is_selected = node.id == selected_id;
            const std::string item_label = node.id + " (" + node.type + ")##behavior-node-option-" +
                node.id;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                selected_id = node.id;
                changed = true;
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        if (!found) ImGui::TextDisabled("No matching graph node.");
        if (selected == nodes.end() && !selected_id.empty())
            ImGui::TextDisabled("Missing node reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_behavior_port_picker(
    const char* label,
    const std::span<const fabric::project::BehaviorNodeDefinition> nodes,
    const std::string_view node_id,
    std::string& selected_id,
    const fabric::project::BehaviorPortDirection direction) {
    const auto node = std::ranges::find_if(
        nodes, [&](const auto& candidate) { return candidate.id == node_id; });
    const fabric::project::BehaviorPortDefinition* selected = nullptr;
    if (node != nodes.end()) {
        const auto selected_it = std::ranges::find_if(node->ports, [&](const auto& port) {
            return port.id == selected_id && port.direction == direction;
        });
        if (selected_it != node->ports.end()) selected = &*selected_it;
    }
    const std::string preview = selected != nullptr
        ? selected->id + " (" + std::string{fabric::project::to_string(selected->type)} + ")"
        : selected_id.empty() ? std::string{"Choose a graph port..."}
                              : std::string{"Missing: "} + selected_id;
    bool changed = false;
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        static std::unordered_map<ImGuiID, std::string> filters;
        auto& filter = filters[ImGui::GetID(label)];
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##behavior-port-search", "Search port ID or type...",
                                 &filter);
        bool found = false;
        if (node != nodes.end()) {
            for (const auto& port : node->ports) {
                if (port.direction != direction) continue;
                const auto type = std::string{fabric::project::to_string(port.type)};
                auto haystack = port.id + " " + type;
                auto needle = filter;
                std::ranges::transform(haystack, haystack.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                std::ranges::transform(needle, needle.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                if (!needle.empty() && haystack.find(needle) == std::string::npos) continue;
                found = true;
                const bool is_selected = port.id == selected_id;
                const auto item_label = port.id + " (" + type + ")##behavior-port-option-" + port.id;
                if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                    selected_id = port.id;
                    changed = true;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
        }
        if (!found) ImGui::TextDisabled("No matching graph port for this node.");
        if (node == nodes.end()) ImGui::TextDisabled("Choose a graph node first.");
        else if (selected == nullptr && !selected_id.empty())
            ImGui::TextDisabled("Missing port reference: %s", selected_id.c_str());
        ImGui::EndCombo();
    }
    return changed;
}

std::optional<fabric::editor::StudioResourceKind>
resource_kind_for_contract(const std::string_view expected_type) {
    using Kind = fabric::editor::StudioResourceKind;
    if (expected_type == "texture") return Kind::texture;
    if (expected_type == "vector") return Kind::vector;
    if (expected_type == "material") return Kind::material;
    if (expected_type == "entity") return Kind::entity;
    if (expected_type == "animation") return Kind::animation;
    if (expected_type == "input") return Kind::input;
    if (expected_type == "behavior") return Kind::behavior;
    if (expected_type == "transformation") return Kind::transformation;
    if (expected_type == "texturedPath") return Kind::textured_path;
    if (expected_type == "visualComposition") return Kind::visual_composition;
    if (expected_type == "visualComponent") return Kind::visual_component;
    if (expected_type == "map") return Kind::map;
    if (expected_type == "scene") return Kind::scene;
    if (expected_type == "mechanic") return Kind::mechanic;
    if (expected_type == "replay") return Kind::replay;
    if (expected_type == "audio") return Kind::audio;
    return std::nullopt;
}

bool draw_typed_resource_reference(
    const char* label,
    const std::span<const fabric::editor::StudioResource> resources,
    fabric::project::ResourceReference& reference) {
    const auto kind = resource_kind_for_contract(reference.expected_type);
    if (!kind) return ImGui::InputText(label, &reference.id.value);
    std::string selected_id = reference.id.value;
    const bool changed = draw_project_resource_picker(
        label, resources, *kind, selected_id, false);
    if (changed) reference.id.value = std::move(selected_id);
    return changed;
}

bool draw_transfer_mode(const char* label, fabric::project::TransferMode& value,
                        const bool structural = false,
                        const bool incompatible = false) {
    using Mode = fabric::project::TransferMode;
    const auto preview = std::string(fabric::project::to_string(value));
    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const auto option : {Mode::preserve, Mode::reset, Mode::mapping,
                                  Mode::error}) {
            if ((structural && option != Mode::preserve && option != Mode::reset) ||
                (incompatible && option != Mode::reset && option != Mode::error))
                continue;
            const auto name = std::string(fabric::project::to_string(option));
            if (ImGui::Selectable(name.c_str(), value == option)) {
                value = option;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void draw_transformation_editor(
    fabric::editor::ProjectSession& project_session,
    fabric::editor::TransformationSession& transformation_session,
    CreationUiState& creation, std::string& status) {
    using Kind = fabric::editor::StudioResourceKind;
    if (creation.request_transformation) {
        ImGui::OpenPopup("Create transformation");
        creation.request_transformation = false;
    }
    if (ImGui::BeginPopupModal("Create transformation", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Create a reusable atomic EntityTransformation v1 policy.");
        draw_resource_identity_fields(creation.transformation.name,
                                      creation.transformation.id);
        static_cast<void>(draw_project_resource_picker(
            "Source entity", project_session.resources(), Kind::entity,
            creation.transformation.source_id, false));
        static_cast<void>(draw_project_resource_picker(
            "Destination entity", project_session.resources(), Kind::entity,
            creation.transformation.destination_id, false));
        const bool valid = !creation.transformation.name.empty() &&
            fabric::core::ResourceId::is_valid(creation.transformation.id) &&
            !creation.transformation.source_id.empty() &&
            !creation.transformation.destination_id.empty() &&
            creation.transformation.source_id !=
                creation.transformation.destination_id;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Create transformation")) {
            fabric::project::EntityTransformation value;
            value.document.id = {.value = creation.transformation.id};
            value.document.name = creation.transformation.name;
            value.source_entity = {
                {.value = creation.transformation.source_id}, "entity"};
            value.destination_entity = {
                {.value = creation.transformation.destination_id}, "entity"};
            if (transformation_session.create(
                    project_session.project_root(), value) &&
                project_session.refresh_resources() &&
                project_session.select_resource(Kind::transformation,
                                                value.document.id)) {
                status = "Transformation created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Transformation creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Enter valid IDs and choose distinct source and destination entities.");
        draw_disabled_reason(!valid,
                             "Enter a valid name/id and choose two different entity resources.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const auto* selected = project_session.selected_resource();
    if (!selected || selected->kind != Kind::transformation) return;
    if (!transformation_session.has_transformation() ||
        transformation_session.transformation()->document.id != selected->id) {
        if (!transformation_session.open(project_session.project_root(),
                                         selected->id)) {
            status = "Transformation could not be opened.";
            return;
        }
    }

    ImGui::SetNextWindowSize({650.0F, 720.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entity Transformation")) { ImGui::End(); return; }
    const auto& current = *transformation_session.transformation();
    ImGui::Text("%s", current.document.name.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("%s", current.document.id.value.c_str());
    if (ImGui::Button("Save transformation"))
        status = transformation_session.save()
            ? "Transformation saved." : "Transformation save failed.";
    ImGui::SameLine();
    ImGui::BeginDisabled(!transformation_session.can_undo());
    if (ImGui::Button("Undo##transformation"))
        static_cast<void>(transformation_session.undo());
    ImGui::EndDisabled(); ImGui::SameLine();
    draw_disabled_reason(!transformation_session.can_undo(),
                         "No transformation edit to undo.");
    ImGui::BeginDisabled(!transformation_session.can_redo());
    if (ImGui::Button("Redo##transformation"))
        static_cast<void>(transformation_session.redo());
    ImGui::EndDisabled();
    draw_disabled_reason(!transformation_session.can_redo(),
                         "No transformation edit to redo.");

    std::string source = current.source_entity.id.value;
    if (draw_project_resource_picker("Source entity##transformation",
            project_session.resources(), Kind::entity, source, false))
        static_cast<void>(transformation_session.set_source({.value = source}));
    std::string destination = current.destination_entity.id.value;
    if (draw_project_resource_picker("Destination entity##transformation",
            project_session.resources(), Kind::entity, destination, false))
        static_cast<void>(transformation_session.set_destination(
            {.value = destination}));

    ImGui::SeparatorText("Atomic state transfer policy");
    auto policy = transformation_session.transformation()->policy;
    bool changed = false;
    changed |= draw_transfer_mode("World transform", policy.world_transform, true);
    changed |= draw_transfer_mode("Instance id", policy.instance_id, true);
    changed |= draw_transfer_mode("Layer and Z", policy.layer_and_z, true);
    changed |= draw_transfer_mode("Physics", policy.physics, true);
    changed |= draw_transfer_mode("Properties", policy.properties);
    changed |= draw_transfer_mode("Behavior parameters", policy.behavior_parameters);
    changed |= draw_transfer_mode("Animation and time", policy.animation);
    changed |= draw_transfer_mode("Timers and cooldowns",
                                  policy.timers_and_cooldowns, true);
    changed |= draw_transfer_mode("Camera follow", policy.camera_follow, true);
    changed |= draw_transfer_mode("Incompatible values",
                                  policy.incompatible_values, false, true);
    if (changed)
        static_cast<void>(transformation_session.set_policy(policy));

    ImGui::SeparatorText("Explicit mappings");
    std::optional<std::size_t> remove_mapping;
    for (std::size_t index = 0;
         index < transformation_session.transformation()->policy.mappings.size();
         ++index) {
        auto next = transformation_session.transformation()->policy;
        auto& mapping = next.mappings[index];
        ImGui::PushID(static_cast<int>(index));
        int domain = static_cast<int>(mapping.domain);
        static constexpr const char* domains[]{"Property", "Behavior parameter",
                                                "Animation"};
        bool row_changed = ImGui::Combo("Domain", &domain, domains, 3);
        mapping.domain = static_cast<fabric::project::TransferDomain>(domain);
        row_changed |= ImGui::InputText("Source", &mapping.source);
        row_changed |= ImGui::InputText("Target", &mapping.target);
        if (ImGui::Button("Remove mapping")) remove_mapping = index;
        if (row_changed)
            static_cast<void>(transformation_session.set_policy(std::move(next)));
        ImGui::Separator(); ImGui::PopID();
    }
    if (remove_mapping) {
        auto next = transformation_session.transformation()->policy;
        next.mappings.erase(next.mappings.begin() +
                            static_cast<std::ptrdiff_t>(*remove_mapping));
        static_cast<void>(transformation_session.set_policy(std::move(next)));
    }
    if (ImGui::Button("Add property mapping")) {
        auto next = transformation_session.transformation()->policy;
        const auto suffix = std::to_string(next.mappings.size() + 1U);
        next.mappings.push_back({fabric::project::TransferDomain::property,
                                 "source-" + suffix, "target-" + suffix});
        static_cast<void>(transformation_session.set_policy(std::move(next)));
    }

    ImGui::SeparatorText("Preview contract");
    ImGui::TextWrapped("%s -> %s. Preview Runtime applies this policy in one "
                       "atomic replacement; failure keeps the source instance.",
                       current.source_entity.id.value.c_str(),
                       current.destination_entity.id.value.c_str());
    if (project_session.manifest()) {
        const auto source_entity = fabric::project::load_entity(
            project_session.project_root(), *project_session.manifest(),
            fabric::project::entity_document_path(
                *project_session.manifest(), current.source_entity.id));
        const auto destination_entity = fabric::project::load_entity(
            project_session.project_root(), *project_session.manifest(),
            fabric::project::entity_document_path(
                *project_session.manifest(), current.destination_entity.id));
        if (source_entity.ok() && destination_entity.ok()) {
            const auto source_preview = build_entity_preview(
                project_session.project_root(), *project_session.manifest(),
                *source_entity.entity);
            const auto destination_preview = build_entity_preview(
                project_session.project_root(), *project_session.manifest(),
                *destination_entity.entity);
            if (ImGui::BeginTable("Transformation previews", 2,
                                  ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::Text("Source · %s",
                            current.source_entity.id.value.c_str());
                draw_entity_preview_thumbnail(
                    "##transformation-source", source_preview,
                    {ImGui::GetContentRegionAvail().x, 180.0F});
                ImGui::TableNextColumn();
                ImGui::Text("Destination · %s",
                            current.destination_entity.id.value.c_str());
                draw_entity_preview_thumbnail(
                    "##transformation-destination", destination_preview,
                    {ImGui::GetContentRegionAvail().x, 180.0F});
                ImGui::EndTable();
            }
        }
    }
    for (const auto& issue : transformation_session.errors())
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s: %s",
                           issue.field.c_str(), issue.message.c_str());
    ImGui::End();
}

ImU32 color_to_u32(const fabric::core::Color& color) {
    const auto channel = [](const float value) {
        return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
    };
    return IM_COL32(channel(color.red), channel(color.green),
                    channel(color.blue), channel(color.alpha));
}

void draw_native_vector_canvas(fabric::editor::ProjectSession& session,
                               CanvasUiState& canvas, const ImVec2 available) {
    if (!session.created_vector()) return;
    const auto& asset = *session.created_vector();
    if (!asset.native || asset.native->size.x <= 0.0F ||
        asset.native->size.y <= 0.0F) {
        ImGui::TextDisabled("Native artwork has no drawable canvas.");
        return;
    }
    ImGui::InvisibleButton("Native canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 origin = ImGui::GetItemRectMin();
    canvas.native_canvas = true;
    canvas.native_origin = origin;
    canvas.native_size = available;
    const ImVec2 center{origin.x + available.x * 0.5F,
                        origin.y + available.y * 0.5F};
    const float fit = std::min((available.x - 80.0F) / asset.native->size.x,
                               (available.y - 80.0F) / asset.native->size.y);
    const bool hovered = ImGui::IsItemHovered();
    auto& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.0F) {
        const float old_scale = fit * canvas.zoom;
        const ImVec2 mouse = io.MousePos;
        const ImVec2 world_under_cursor{
            (mouse.x - center.x - canvas.pan.x) / old_scale,
            -(mouse.y - center.y - canvas.pan.y) / old_scale};
        canvas.zoom = std::clamp(
            canvas.zoom * (io.MouseWheel > 0.0F ? 1.15F : 1.0F / 1.15F),
            0.1F, 20.0F);
        const float new_scale = fit * canvas.zoom;
        canvas.pan.x = mouse.x - center.x - world_under_cursor.x * new_scale;
        canvas.pan.y = mouse.y - center.y + world_under_cursor.y * new_scale;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        canvas.pan.x += delta.x;
        canvas.pan.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    const float pixels_per_unit = fit * canvas.zoom;
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{center.x + canvas.pan.x + point.x * pixels_per_unit,
                      center.y + canvas.pan.y - point.y * pixels_per_unit};
    };
    const auto to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{
            (point.x - center.x - canvas.pan.x) / pixels_per_unit,
            -(point.y - center.y - canvas.pan.y) / pixels_per_unit};
    };
    const auto transform_point = [](const fabric::project::VectorNode& node,
                                    const fabric::core::Vec2 point) {
        const float x = (point.x - node.transform.pivot.x) *
            node.transform.scale.x;
        const float y = (point.y - node.transform.pivot.y) *
            node.transform.scale.y;
        const float angle = node.transform.rotation_degrees *
            std::numbers::pi_v<float> / 180.0F;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        return fabric::core::Vec2{
            node.transform.position.x + node.transform.pivot.x +
                x * cosine - y * sine,
            node.transform.position.y + node.transform.pivot.y +
                x * sine + y * cosine};
    };
    const auto world_to_local = [](const fabric::project::VectorNode& node,
                                   const fabric::core::Vec2 world) {
        const float angle = -node.transform.rotation_degrees *
            std::numbers::pi_v<float> / 180.0F;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float x = world.x - node.transform.position.x - node.transform.pivot.x;
        const float y = world.y - node.transform.position.y - node.transform.pivot.y;
        return fabric::core::Vec2{
            (x * cosine + y * sine) /
                std::max(std::abs(node.transform.scale.x), 0.0001F) +
                node.transform.pivot.x,
            (-x * sine + y * cosine) /
                std::max(std::abs(node.transform.scale.y), 0.0001F) +
                node.transform.pivot.y};
    };
    auto* draw_list = ImGui::GetWindowDrawList();
    const float target_grid_pixels = 48.0F;
    const float raw_grid_step = target_grid_pixels / pixels_per_unit;
    const float grid_power = std::pow(10.0F,
                                      std::floor(std::log10(raw_grid_step)));
    const float normalized_grid = raw_grid_step / grid_power;
    const float grid_step = (normalized_grid <= 1.0F ? 1.0F
                              : normalized_grid <= 2.0F ? 2.0F
                              : normalized_grid <= 5.0F ? 5.0F
                                                       : 10.0F) * grid_power;
    const float world_half_width = available.x / (2.0F * pixels_per_unit);
    const float world_half_height = available.y / (2.0F * pixels_per_unit);
    const float world_left = -world_half_width - canvas.pan.x / pixels_per_unit;
    const float world_right = world_half_width - canvas.pan.x / pixels_per_unit;
    const float world_bottom = -world_half_height + canvas.pan.y / pixels_per_unit;
    const float world_top = world_half_height + canvas.pan.y / pixels_per_unit;
    canvas.native_world_bounds = {
        .origin = {world_left, world_bottom},
        .size = {world_right - world_left, world_top - world_bottom},
    };
    const int first_vertical = static_cast<int>(std::floor(world_left / grid_step));
    const int last_vertical = static_cast<int>(std::ceil(world_right / grid_step));
    const int first_horizontal = static_cast<int>(std::floor(world_bottom / grid_step));
    const int last_horizontal = static_cast<int>(std::ceil(world_top / grid_step));
    const fabric::project::VectorNode* selected_node =
        !asset.native->nodes.empty() &&
                canvas.selected_node < asset.native->nodes.size()
            ? &asset.native->nodes[canvas.selected_node]
            : nullptr;
    ImVec2 rotate_handle{};
    ImVec2 rotate_anchor{};
    ImVec2 scale_handle{};
    ImVec2 pivot_handle{};
    ImVec2 transform_center{};
    if (selected_node != nullptr) {
        const auto& bounds = selected_node->shape.bounds;
        const fabric::core::Vec2 local_center{
            bounds.origin.x + bounds.size.x * 0.5F,
            bounds.origin.y + bounds.size.y * 0.5F};
        const auto world_center = transform_point(*selected_node, local_center);
        const auto world_top = transform_point(
            *selected_node,
            {local_center.x, bounds.origin.y + bounds.size.y});
        const auto world_bottom_right = transform_point(
            *selected_node,
            {bounds.origin.x + bounds.size.x,
             bounds.origin.y});
        transform_center = to_screen(world_center);
        const ImVec2 top = to_screen(world_top);
        rotate_anchor = top;
        const auto extended = fabric::editor::extend_canvas_handle(
            {transform_center.x, transform_center.y}, {top.x, top.y}, 30.0F);
        rotate_handle = {extended.x, extended.y};
        scale_handle = to_screen(world_bottom_right);
        pivot_handle = to_screen(
            transform_point(*selected_node, selected_node->transform.pivot));
    }
    bool path_command_edited = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = io.MousePos;
        const auto distance = [](const ImVec2 left, const ImVec2 right) {
            return std::hypot(left.x - right.x, left.y - right.y);
        };
        CanvasUiState::DragOperation operation =
            CanvasUiState::DragOperation::none;
        if (selected_node != nullptr && !selected_node->locked) {
            if (canvas.tool == CanvasUiState::Tool::pen &&
                selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
                auto changed = *selected_node;
                const auto local = world_to_local(*selected_node, to_world(mouse));
                std::size_t insert_index = changed.shape.path.size();
                float closest_segment = 14.0F;
                fabric::core::Vec2 previous{};
                bool has_previous = false;
                for (std::size_t index = 0; index < changed.shape.path.size(); ++index) {
                    const auto& command = changed.shape.path[index];
                    if (command.kind == fabric::project::VectorPathCommandKind::move) {
                        previous = command.point;
                        has_previous = true;
                        continue;
                    }
                    if ((command.kind == fabric::project::VectorPathCommandKind::line ||
                         command.kind == fabric::project::VectorPathCommandKind::cubic) &&
                        has_previous) {
                        const auto start = to_screen(transform_point(*selected_node, previous));
                        const auto end = to_screen(transform_point(*selected_node, command.point));
                        const ImVec2 segment{end.x - start.x, end.y - start.y};
                        const ImVec2 relative{mouse.x - start.x, mouse.y - start.y};
                        const float length_squared = segment.x * segment.x + segment.y * segment.y;
                        const float factor = length_squared > 0.0001F
                            ? std::clamp((relative.x * segment.x + relative.y * segment.y) /
                                             length_squared,
                                         0.0F, 1.0F)
                            : 0.0F;
                        const ImVec2 projection{start.x + segment.x * factor,
                                                start.y + segment.y * factor};
                        const float distance = std::hypot(
                            mouse.x - projection.x, mouse.y - projection.y);
                        if (distance < closest_segment) {
                            closest_segment = distance;
                            insert_index = index;
                        }
                        previous = command.point;
                    } else if (command.kind != fabric::project::VectorPathCommandKind::close) {
                        previous = command.point;
                    }
                }
                if (insert_index == changed.shape.path.size() &&
                    !changed.shape.path.empty() &&
                    changed.shape.path.back().kind ==
                        fabric::project::VectorPathCommandKind::close)
                    insert_index -= 1U;
                if (changed.shape.path.empty()) {
                    changed.shape.path.push_back({
                        .kind = fabric::project::VectorPathCommandKind::move,
                        .point = local});
                    static_cast<void>(session.set_selected_vector_node(
                        canvas.selected_node, std::move(changed)));
                } else {
                    const auto command = fabric::project::VectorShape::PathCommand{
                        .kind = fabric::project::VectorPathCommandKind::line,
                        .point = local};
                    const bool inserted = fabric::project::insert_path_command(
                        changed.shape, insert_index, command);
                    const bool applied = inserted &&
                        session.set_selected_vector_node(
                            canvas.selected_node, std::move(changed));
                    if (applied) {
                        canvas.selected_path_points = {insert_index};
                        path_command_edited = true;
                    }
                }
            }
            if (canvas.tool == CanvasUiState::Tool::move &&
                selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
                const auto distance = [](const ImVec2 left, const ImVec2 right) {
                    return std::hypot(left.x - right.x, left.y - right.y);
                };
                float closest = 10.0F;
                std::optional<std::size_t> hit_anchor;
                for (std::size_t index = 0; index < selected_node->shape.path.size(); ++index) {
                    const auto& command = selected_node->shape.path[index];
                    if (command.kind == fabric::project::VectorPathCommandKind::move ||
                        command.kind == fabric::project::VectorPathCommandKind::line ||
                        command.kind == fabric::project::VectorPathCommandKind::cubic) {
                        const auto candidate = to_screen(
                            transform_point(*selected_node, command.point));
                        const float hit = distance(mouse, candidate);
                        if (hit <= closest) {
                            closest = hit;
                            hit_anchor = index;
                        }
                    }
                    if (command.kind == fabric::project::VectorPathCommandKind::cubic) {
                        for (const auto [handle, candidate_operation] : {
                                 std::pair{command.control1,
                                           CanvasUiState::DragOperation::bezier_handle1},
                                 std::pair{command.control2,
                                           CanvasUiState::DragOperation::bezier_handle2}}) {
                            const auto candidate = to_screen(
                                transform_point(*selected_node, handle));
                            const float hit = distance(mouse, candidate);
                            if (hit <= closest) {
                                closest = hit;
                                operation = candidate_operation;
                                canvas.path_command_index = index;
                            }
                        }
                    }
                }
                if (operation == CanvasUiState::DragOperation::none && hit_anchor) {
                    const auto selected = std::ranges::find(
                        canvas.selected_path_points, *hit_anchor);
                    if (io.KeyShift) {
                        if (selected == canvas.selected_path_points.end())
                            canvas.selected_path_points.push_back(*hit_anchor);
                        else
                            canvas.selected_path_points.erase(selected);
                    } else {
                        if (selected == canvas.selected_path_points.end())
                            canvas.selected_path_points = {*hit_anchor};
                        operation = canvas.selected_path_points.size() > 1U
                            ? CanvasUiState::DragOperation::path_selection
                            : CanvasUiState::DragOperation::path_point;
                        canvas.path_command_index = *hit_anchor;
                    }
                }
            }
            if (operation == CanvasUiState::DragOperation::none &&
                canvas.tool == CanvasUiState::Tool::rotate &&
                distance(mouse, rotate_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::rotate;
            } else if (operation == CanvasUiState::DragOperation::none &&
                       canvas.tool == CanvasUiState::Tool::scale &&
                       distance(mouse, scale_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::scale;
            } else if (operation == CanvasUiState::DragOperation::none &&
                       canvas.tool == CanvasUiState::Tool::pivot &&
                       distance(mouse, pivot_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::pivot;
            }
        }
        if (operation == CanvasUiState::DragOperation::none &&
            !path_command_edited) {
            const auto world = to_world(mouse);
            const auto hit_node = fabric::editor::topmost_vector_node_at(
                asset.native->nodes, world, 8.0F / pixels_per_unit);
            if (hit_node) {
                if (*hit_node == canvas.selected_node && selected_node != nullptr &&
                    !selected_node->locked &&
                    canvas.tool == CanvasUiState::Tool::move) {
                    operation = CanvasUiState::DragOperation::move;
                } else {
                    canvas.selected_path_points.clear();
                    canvas.selected_node = *hit_node;
                }
            }
        }
        if (operation != CanvasUiState::DragOperation::none &&
            selected_node != nullptr) {
            canvas.dragging = true;
            canvas.drag_operation = operation;
            canvas.drag_start_mouse = mouse;
            canvas.drag_start_transform = selected_node->transform;
            canvas.drag_start_node = *selected_node;
        }
    }
    const bool right_click = ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
        (io.MouseDown[ImGuiMouseButton_Right] &&
         io.MouseDownDuration[ImGuiMouseButton_Right] == 0.0F);
    if (hovered && right_click &&
        selected_node != nullptr && !selected_node->locked &&
        canvas.tool == CanvasUiState::Tool::pen &&
        selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
        const ImVec2 mouse = io.MousePos;
        std::optional<std::size_t> hit;
        float closest = 10.0F;
        for (std::size_t index = 0; index < selected_node->shape.path.size(); ++index) {
            const auto& command = selected_node->shape.path[index];
            if (command.kind == fabric::project::VectorPathCommandKind::move ||
                command.kind == fabric::project::VectorPathCommandKind::line ||
                command.kind == fabric::project::VectorPathCommandKind::cubic) {
                const auto candidate = to_screen(
                    transform_point(*selected_node, command.point));
                const float distance = std::hypot(
                    mouse.x - candidate.x, mouse.y - candidate.y);
                if (distance < closest) {
                    closest = distance;
                    hit = index;
                }
            }
        }
        if (hit) {
            auto changed = *selected_node;
            if (fabric::project::remove_path_command(changed.shape, *hit)) {
                static_cast<void>(session.set_selected_vector_node(
                    canvas.selected_node, std::move(changed)));
            }
            canvas.selected_path_points.clear();
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        canvas.dragging = false;
        canvas.drag_operation = CanvasUiState::DragOperation::none;
    }
    if (canvas.dragging && !canvas.drag_start_node.locked &&
        (io.MousePos.x != canvas.drag_start_mouse.x ||
         io.MousePos.y != canvas.drag_start_mouse.y)) {
        const bool path_drag =
            canvas.drag_operation == CanvasUiState::DragOperation::path_selection ||
            canvas.drag_operation == CanvasUiState::DragOperation::path_point ||
            canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle1 ||
            canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle2;
        auto changed = path_drag ? canvas.drag_start_node : *selected_node;
        const auto& start = canvas.drag_start_transform;
        const auto start_mouse = to_world(canvas.drag_start_mouse);
        const auto current_mouse = to_world(io.MousePos);
        const auto world_to_local = [&](const fabric::core::Vec2 world) {
            const auto& transform = canvas.drag_start_node.transform;
            const float angle = -transform.rotation_degrees *
                std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const float x = world.x - transform.position.x - transform.pivot.x;
            const float y = world.y - transform.position.y - transform.pivot.y;
            return fabric::core::Vec2{
                (x * cosine + y * sine) /
                    std::max(std::abs(transform.scale.x), 0.0001F) +
                    transform.pivot.x,
                (-x * sine + y * cosine) /
                    std::max(std::abs(transform.scale.y), 0.0001F) +
                    transform.pivot.y};
        };
        if (canvas.drag_operation == CanvasUiState::DragOperation::path_selection) {
            changed = canvas.drag_start_node;
            const fabric::core::Vec2 delta{
                current_mouse.x - start_mouse.x,
                current_mouse.y - start_mouse.y};
            static_cast<void>(fabric::project::transform_path_points(
                changed.shape, canvas.selected_path_points, delta, 0.0F,
                {1.0F, 1.0F}));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::path_point &&
            canvas.path_command_index < changed.shape.path.size()) {
            changed.shape.path[canvas.path_command_index].point =
                world_to_local(current_mouse);
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle1 &&
                   canvas.path_command_index < changed.shape.path.size()) {
            static_cast<void>(fabric::editor::update_bezier_handle(
                changed.shape, canvas.path_command_index, true,
                world_to_local(current_mouse), canvas.bezier_handle_mode));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::bezier_handle2 &&
                   canvas.path_command_index < changed.shape.path.size()) {
            static_cast<void>(fabric::editor::update_bezier_handle(
                changed.shape, canvas.path_command_index, false,
                world_to_local(current_mouse), canvas.bezier_handle_mode));
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::move) {
            changed.transform.position = {
                start.position.x + current_mouse.x - start_mouse.x,
                start.position.y + current_mouse.y - start_mouse.y};
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::rotate) {
            const auto start_vector = fabric::core::Vec2{
                start_mouse.x - (start.position.x + start.pivot.x),
                start_mouse.y - (start.position.y + start.pivot.y)};
            const auto current_vector = fabric::core::Vec2{
                current_mouse.x - (start.position.x + start.pivot.x),
                current_mouse.y - (start.position.y + start.pivot.y)};
            const float start_angle = std::atan2(start_vector.y, start_vector.x);
            const float current_angle =
                std::atan2(current_vector.y, current_vector.x);
            changed.transform.rotation_degrees =
                start.rotation_degrees +
                (current_angle - start_angle) * 180.0F /
                    std::numbers::pi_v<float>;
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::scale) {
            const auto local_from_world = [&](const fabric::core::Vec2 world) {
                const float angle = -start.rotation_degrees *
                    std::numbers::pi_v<float> / 180.0F;
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                const float x = world.x - start.position.x - start.pivot.x;
                const float y = world.y - start.position.y - start.pivot.y;
                return fabric::core::Vec2{
                    (x * cosine + y * sine) /
                        std::max(std::abs(start.scale.x), 0.0001F),
                    (-x * sine + y * cosine) /
                        std::max(std::abs(start.scale.y), 0.0001F)};
            };
            const auto start_local = local_from_world(start_mouse);
            const auto current_local = local_from_world(current_mouse);
            const float ratio_x = std::abs(start_local.x) > 0.0001F
                ? current_local.x / start_local.x
                : 1.0F;
            const float ratio_y = std::abs(start_local.y) > 0.0001F
                ? current_local.y / start_local.y
                : 1.0F;
            changed.transform.scale = {
                std::copysign(std::max(0.01F, std::abs(start.scale.x * ratio_x)),
                              start.scale.x),
                std::copysign(std::max(0.01F, std::abs(start.scale.y * ratio_y)),
                              start.scale.y)};
        } else if (canvas.drag_operation == CanvasUiState::DragOperation::pivot) {
            const fabric::core::Vec2 next_pivot{
                current_mouse.x - start.position.x,
                current_mouse.y - start.position.y};
            const float angle = start.rotation_degrees *
                std::numbers::pi_v<float> / 180.0F;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const auto apply_linear = [&](const fabric::core::Vec2 value) {
                return fabric::core::Vec2{
                    cosine * start.scale.x * value.x -
                        sine * start.scale.y * value.y,
                    sine * start.scale.x * value.x +
                        cosine * start.scale.y * value.y};
            };
            const auto pivot_delta = fabric::core::Vec2{
                start.pivot.x - next_pivot.x,
                start.pivot.y - next_pivot.y};
            const auto transformed_delta = apply_linear(pivot_delta);
            changed.transform.position = {
                start.position.x + pivot_delta.x - transformed_delta.x,
                start.position.y + pivot_delta.y - transformed_delta.y};
            changed.transform.pivot = next_pivot;
        }
        static_cast<void>(session.set_selected_vector_node(
            canvas.selected_node, std::move(changed)));
    }
    draw_list->PushClipRect(origin, {origin.x + available.x,
                                     origin.y + available.y}, true);
    for (int index = first_vertical; index <= last_vertical; ++index) {
        const auto line_start = to_screen({static_cast<float>(index) * grid_step,
                                           world_bottom});
        const auto line_end = to_screen({static_cast<float>(index) * grid_step,
                                         world_top});
        draw_list->AddLine(line_start, line_end,
                           index == 0 ? IM_COL32(135, 155, 165, 150)
                                      : IM_COL32(90, 105, 115, 70));
    }
    for (int index = first_horizontal; index <= last_horizontal; ++index) {
        const auto line_start = to_screen({world_left,
                                           static_cast<float>(index) * grid_step});
        const auto line_end = to_screen({world_right,
                                         static_cast<float>(index) * grid_step});
        draw_list->AddLine(line_start, line_end,
                           index == 0 ? IM_COL32(135, 155, 165, 150)
                                      : IM_COL32(90, 105, 115, 70));
    }
    draw_list->AddText({origin.x + 10.0F, origin.y + 10.0F},
                       IM_COL32(185, 200, 205, 220),
                       ("Grid: " + std::to_string(grid_step) + " world units").c_str());
    for (std::size_t node_index = 0;
         node_index < asset.native->nodes.size(); ++node_index) {
        const auto& node = asset.native->nodes[node_index];
        if (!node.visible) {
            continue;
        }
        const auto node_transform_point = [&](const fabric::core::Vec2 point) {
            return transform_point(node, point);
        };
        const auto& bounds = node.shape.bounds;
        std::vector<ImVec2> points;
        if (node.shape.kind == fabric::project::VectorShapeKind::ellipse) {
            constexpr int segments = 64;
            points.reserve(segments);
            const fabric::core::Vec2 ellipse_center{
                bounds.origin.x + bounds.size.x * 0.5F,
                bounds.origin.y + bounds.size.y * 0.5F};
            for (int segment = 0; segment < segments; ++segment) {
                const float angle = 2.0F * std::numbers::pi_v<float> *
                    static_cast<float>(segment) / static_cast<float>(segments);
                points.push_back(to_screen(node_transform_point({
                    ellipse_center.x + std::cos(angle) * bounds.size.x * 0.5F,
                    ellipse_center.y + std::sin(angle) * bounds.size.y * 0.5F})));
            }
        } else if (node.shape.kind == fabric::project::VectorShapeKind::line &&
                   node.shape.points.size() == 2U) {
            points = {to_screen(node_transform_point(node.shape.points[0])),
                      to_screen(node_transform_point(node.shape.points[1]))};
        } else if (node.shape.kind == fabric::project::VectorShapeKind::path) {
            fabric::core::Vec2 current{};
            fabric::core::Vec2 first{};
            bool has_current = false;
            for (const auto& command : node.shape.path) {
                if (command.kind == fabric::project::VectorPathCommandKind::move) {
                    current = command.point;
                    first = current;
                    has_current = true;
                    points.push_back(to_screen(node_transform_point(current)));
                } else if (command.kind == fabric::project::VectorPathCommandKind::line &&
                           has_current) {
                    current = command.point;
                    points.push_back(to_screen(node_transform_point(current)));
                } else if (command.kind == fabric::project::VectorPathCommandKind::cubic &&
                           has_current) {
                    const auto start = current;
                    for (int segment = 1; segment <= 12; ++segment) {
                        const float t = static_cast<float>(segment) / 12.0F;
                        const float inverse = 1.0F - t;
                        current = {
                            inverse * inverse * inverse * start.x +
                                3.0F * inverse * inverse * t * command.control1.x +
                                3.0F * inverse * t * t * command.control2.x +
                                t * t * t * command.point.x,
                            inverse * inverse * inverse * start.y +
                                3.0F * inverse * inverse * t * command.control1.y +
                                3.0F * inverse * t * t * command.control2.y +
                                t * t * t * command.point.y};
                        points.push_back(to_screen(node_transform_point(current)));
                    }
                } else if (command.kind == fabric::project::VectorPathCommandKind::close &&
                           has_current) {
                    current = first;
                    points.push_back(to_screen(node_transform_point(current)));
                }
            }
        } else {
            points = {
                to_screen(node_transform_point(bounds.origin)),
                to_screen(node_transform_point({bounds.origin.x + bounds.size.x,
                                           bounds.origin.y})),
                to_screen(node_transform_point({bounds.origin.x + bounds.size.x,
                                           bounds.origin.y + bounds.size.y})),
                to_screen(node_transform_point({bounds.origin.x,
                                           bounds.origin.y + bounds.size.y})),
            };
        }
        fabric::core::Color fill{0.35F, 0.55F, 0.58F, 1.0F};
        if (node.fill.kind == fabric::project::VectorFillKind::solid &&
            node.fill.color) {
            fill = *node.fill.color;
        } else if (node.fill.kind == fabric::project::VectorFillKind::none) {
            fill.alpha = 0.0F;
        } else if (node.fill.kind == fabric::project::VectorFillKind::image) {
            fill = {0.89F, 0.68F, 0.34F, 0.8F};
        }
        if (fill.alpha > 0.0F &&
            node.shape.kind != fabric::project::VectorShapeKind::line) {
            draw_list->AddConvexPolyFilled(points.data(),
                                           static_cast<int>(points.size()),
                                           color_to_u32(fill));
        }
        const bool selected = node_index == canvas.selected_node;
        const auto stroke_color = selected
            ? IM_COL32(236, 180, 75, 255)
            : (node.stroke.has_value()
                   ? color_to_u32(node.stroke->color)
                   : IM_COL32(225, 230, 235, 255));
        const float stroke_width = node.stroke.has_value()
            ? std::max(1.0F, node.stroke->width * pixels_per_unit)
            : (selected ? 2.5F : 1.5F);
        const bool closed_path =
            node.shape.kind == fabric::project::VectorShapeKind::path &&
            !node.shape.path.empty() &&
            node.shape.path.back().kind ==
                fabric::project::VectorPathCommandKind::close;
        draw_list->AddPolyline(
            points.data(), static_cast<int>(points.size()),
            stroke_color,
            node.shape.kind == fabric::project::VectorShapeKind::line ||
                    (node.shape.kind == fabric::project::VectorShapeKind::path &&
                     !closed_path)
                ? ImDrawFlags_None
                : ImDrawFlags_Closed,
            stroke_width);
    }
    if (selected_node != nullptr && !selected_node->locked) {
        if (canvas.tool == CanvasUiState::Tool::rotate) {
            draw_list->AddLine(rotate_anchor, rotate_handle,
                               IM_COL32(236, 180, 75, 220), 1.5F);
            draw_list->AddCircleFilled(rotate_handle, 6.0F,
                                       IM_COL32(236, 180, 75, 255));
        } else if (canvas.tool == CanvasUiState::Tool::scale) {
            draw_list->AddRectFilled(
                {scale_handle.x - 6.0F, scale_handle.y - 6.0F},
                {scale_handle.x + 6.0F, scale_handle.y + 6.0F},
                IM_COL32(98, 180, 240, 255));
        } else if (canvas.tool == CanvasUiState::Tool::pivot) {
            draw_list->AddLine({pivot_handle.x - 7.0F, pivot_handle.y},
                               {pivot_handle.x + 7.0F, pivot_handle.y},
                               IM_COL32(180, 110, 235, 255), 2.0F);
            draw_list->AddLine({pivot_handle.x, pivot_handle.y - 7.0F},
                               {pivot_handle.x, pivot_handle.y + 7.0F},
                               IM_COL32(180, 110, 235, 255), 2.0F);
        } else if (canvas.tool == CanvasUiState::Tool::move &&
                   selected_node->shape.kind == fabric::project::VectorShapeKind::path) {
            for (std::size_t index = 0; index < selected_node->shape.path.size(); ++index) {
                const auto& command = selected_node->shape.path[index];
                if (command.kind == fabric::project::VectorPathCommandKind::move ||
                    command.kind == fabric::project::VectorPathCommandKind::line ||
                    command.kind == fabric::project::VectorPathCommandKind::cubic) {
                    const auto point_selected = std::ranges::find(
                        canvas.selected_path_points, index) !=
                        canvas.selected_path_points.end();
                    draw_list->AddCircleFilled(
                        to_screen(transform_point(*selected_node, command.point)),
                        5.0F, point_selected
                            ? IM_COL32(100, 210, 255, 255)
                            : IM_COL32(236, 180, 75, 255));
                }
                if (command.kind == fabric::project::VectorPathCommandKind::cubic) {
                    const auto anchor = to_screen(
                        transform_point(*selected_node, command.point));
                    const auto handle1 = to_screen(
                        transform_point(*selected_node, command.control1));
                    const auto handle2 = to_screen(
                        transform_point(*selected_node, command.control2));
                    draw_list->AddLine(anchor, handle1,
                                       IM_COL32(180, 110, 235, 210), 1.0F);
                    draw_list->AddLine(anchor, handle2,
                                       IM_COL32(180, 110, 235, 210), 1.0F);
                    draw_list->AddCircleFilled(handle1, 4.0F,
                                               IM_COL32(180, 110, 235, 255));
                    draw_list->AddCircleFilled(handle2, 4.0F,
                                               IM_COL32(180, 110, 235, 255));
                }
            }
        }
    }
    draw_list->PopClipRect();
    if (hovered) {
        ImGui::SetTooltip("Click a shape to select it. Move drags the selected shape; on a path, drag anchors or Bézier handles. Rotate, Scale and Pivot drag only their active handle. Middle drag: pan | Wheel: zoom %.0f%%",
                          canvas.zoom * 100.0F);
    }
}

void draw_packet_preview_canvas(CanvasUiState& canvas,
                                const ImVec2 available,
                                const std::string_view label,
                                fabric::editor::ProjectSession* editable_session = nullptr) {
    ImGui::InvisibleButton("Entity canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 origin = ImGui::GetItemRectMin();
    canvas.native_canvas = true;
    canvas.native_origin = origin;
    canvas.native_size = available;
    const auto bounds = canvas.entity_world_bounds;
    const ImVec2 center{origin.x + available.x * 0.5F,
                        origin.y + available.y * 0.5F};
    const float fit = std::min(
        (available.x - 80.0F) / std::max(bounds.size.x, 1.0F),
        (available.y - 80.0F) / std::max(bounds.size.y, 1.0F));
    const float pixels_per_unit = std::max(0.01F, fit * canvas.zoom);
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        canvas.zoom = std::clamp(
            canvas.zoom * (ImGui::GetIO().MouseWheel > 0.0F ? 1.15F : 1.0F / 1.15F),
            0.1F, 20.0F);
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        canvas.pan.x += delta.x;
        canvas.pan.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    const auto to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{center.x + canvas.pan.x + point.x * pixels_per_unit,
                      center.y + canvas.pan.y - point.y * pixels_per_unit};
    };
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(to_screen({0.0F, bounds.origin.y}),
                       to_screen({0.0F, bounds.origin.y + bounds.size.y}),
                       IM_COL32(100, 110, 125, 100));
    draw_list->AddLine(to_screen({bounds.origin.x, 0.0F}),
                       to_screen({bounds.origin.x + bounds.size.x, 0.0F}),
                       IM_COL32(100, 110, 125, 100));
    const float world_half_width = available.x / (2.0F * pixels_per_unit);
    const float world_half_height = available.y / (2.0F * pixels_per_unit);
    canvas.native_world_bounds = {
        .origin = {-world_half_width - canvas.pan.x / pixels_per_unit,
                   -world_half_height + canvas.pan.y / pixels_per_unit},
        .size = {2.0F * world_half_width, 2.0F * world_half_height}};
    ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 8.0F});
    ImGui::TextDisabled("%s · %.0f%%", std::string(label).c_str(),
                        canvas.zoom * 100.0F);
    if (editable_session && editable_session->selected_entity() &&
        canvas.selected_node < editable_session->selected_entity()->nodes.size()) {
        const auto& node = editable_session->selected_entity()->nodes[
            canvas.selected_node];
        const auto gizmo = to_screen(node.transform.position);
        canvas.entity_gizmo_screen = gizmo;
        auto* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine({gizmo.x - 12.0F, gizmo.y},
                           {gizmo.x + 12.0F, gizmo.y},
                           IM_COL32(100, 210, 255, 230), 2.0F);
        draw_list->AddLine({gizmo.x, gizmo.y - 12.0F},
                           {gizmo.x, gizmo.y + 12.0F},
                           IM_COL32(100, 210, 255, 230), 2.0F);
        draw_list->AddCircleFilled(gizmo, 5.0F, IM_COL32(100, 210, 255, 255));
        const bool canvas_hovered = ImGui::IsMouseHoveringRect(
            origin, {origin.x + available.x, origin.y + available.y});
        if (canvas_hovered && !node.locked &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            std::hypot(ImGui::GetIO().MousePos.x - gizmo.x,
                       ImGui::GetIO().MousePos.y - gizmo.y) <= 14.0F) {
            canvas.entity_gizmo_dragging = true;
            canvas.entity_gizmo_start_mouse = ImGui::GetIO().MousePos;
            canvas.entity_gizmo_start_transform = node.transform;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            canvas.entity_gizmo_dragging = false;
        if (canvas.entity_gizmo_dragging && !node.locked) {
            const ImVec2 delta{
                ImGui::GetIO().MousePos.x - canvas.entity_gizmo_start_mouse.x,
                ImGui::GetIO().MousePos.y - canvas.entity_gizmo_start_mouse.y};
            const auto scale = std::max(0.01F, pixels_per_unit);
            auto changed = node;
            changed.transform = canvas.entity_gizmo_start_transform;
            changed.transform.position.x += delta.x / scale;
            changed.transform.position.y -= delta.y / scale;
            if (editable_session->set_selected_entity_node(
                    canvas.selected_node, std::move(changed)))
                ImGui::SetTooltip("Move node · %.2f, %.2f",
                                  delta.x / scale, -delta.y / scale);
        }
    }
}

void draw_raster_crop_canvas(fabric::editor::ProjectSession& session,
                             const AssetPreview& preview,
                             CanvasUiState& canvas, const ImVec2 available,
                             std::string& status) {
    if (!session.imported_texture() || preview.texture == 0U) return;
    const auto& texture = session.imported_texture()->asset;
    if (canvas.crop_resource_id != texture.document.id.value) {
        canvas.crop_resource_id = texture.document.id.value;
        canvas.crop_drag.reset();
    }
    ImGui::InvisibleButton("Raster crop canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const float source_width = static_cast<float>(texture.width);
    const float source_height = static_cast<float>(texture.height);
    const float scale = std::max(
        0.001F, std::min((available.x - 32.0F) / source_width,
                         (available.y - 32.0F) / source_height));
    const ImVec2 image_size{source_width * scale, source_height * scale};
    const ImVec2 image_min{origin.x + (available.x - image_size.x) * 0.5F,
                           origin.y + (available.y - image_size.y) * 0.5F};
    const ImVec2 image_max{image_min.x + image_size.x,
                           image_min.y + image_size.y};
    auto view = texture.view.value_or(fabric::project::RasterView{
        .crop = {{0.0F, 0.0F}, {source_width, source_height}},
    });
    const auto crop_screen_rect = [&](const fabric::project::RasterView& value) {
        return std::pair{
            ImVec2{image_min.x + value.crop.origin.x * scale,
                   image_min.y + value.crop.origin.y * scale},
            ImVec2{image_min.x +
                       (value.crop.origin.x + value.crop.size.x) * scale,
                   image_min.y +
                       (value.crop.origin.y + value.crop.size.y) * scale}};
    };
    auto [crop_min, crop_max] = crop_screen_rect(view);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto near = [&](const ImVec2 point) {
        return std::hypot(mouse.x - point.x, mouse.y - point.y) <= 11.0F;
    };
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 top_right{crop_max.x, crop_min.y};
        const ImVec2 bottom_left{crop_min.x, crop_max.y};
        if (near(crop_min)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::top_left;
        } else if (near(top_right)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::top_right;
        } else if (near(bottom_left)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::bottom_left;
        } else if (near(crop_max)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::bottom_right;
        } else if (mouse.x >= crop_min.x && mouse.x <= crop_max.x &&
                   mouse.y >= crop_min.y && mouse.y <= crop_max.y) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::move;
        }
        if (canvas.crop_drag) {
            canvas.crop_start_mouse = mouse;
            canvas.crop_start_view = view;
        }
    }
    if (canvas.crop_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const fabric::core::Vec2 delta{
            (mouse.x - canvas.crop_start_mouse.x) / scale,
            (mouse.y - canvas.crop_start_mouse.y) / scale};
        const auto candidate = fabric::editor::drag_raster_crop(
            canvas.crop_start_view, *canvas.crop_drag, delta,
            texture.width, texture.height);
        if (candidate.crop != view.crop) {
            if (session.set_selected_texture_view(candidate)) {
                view = candidate;
                status = "Raster crop changed; source pixels are unchanged.";
            } else {
                status = "Raster crop rejected; inspect diagnostics.";
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        canvas.crop_drag.reset();
    }
    const auto updated_crop = crop_screen_rect(view);
    crop_min = updated_crop.first;
    crop_max = updated_crop.second;
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin,
                            {origin.x + available.x, origin.y + available.y},
                            true);
    draw_list->AddImage(
        ImTextureRef(static_cast<ImTextureID>(preview.texture)),
        image_min, image_max, {0.0F, 1.0F}, {1.0F, 0.0F});
    constexpr ImU32 shade = IM_COL32(8, 10, 14, 170);
    draw_list->AddRectFilled(image_min, {image_max.x, crop_min.y}, shade);
    draw_list->AddRectFilled({image_min.x, crop_max.y}, image_max, shade);
    draw_list->AddRectFilled({image_min.x, crop_min.y},
                             {crop_min.x, crop_max.y}, shade);
    draw_list->AddRectFilled({crop_max.x, crop_min.y},
                             {image_max.x, crop_max.y}, shade);
    draw_list->AddRect(crop_min, crop_max, IM_COL32(244, 190, 80, 255),
                       0.0F, ImDrawFlags_None, 2.0F);
    for (const ImVec2 handle : {
             crop_min, ImVec2{crop_max.x, crop_min.y},
             ImVec2{crop_min.x, crop_max.y}, crop_max}) {
        draw_list->AddRectFilled({handle.x - 5.0F, handle.y - 5.0F},
                                 {handle.x + 5.0F, handle.y + 5.0F},
                                 IM_COL32(250, 220, 145, 255));
    }
    draw_list->AddText(
        {image_min.x + 8.0F, image_min.y + 8.0F},
        IM_COL32(245, 245, 245, 255),
        (std::to_string(static_cast<int>(view.crop.size.x)) + " x " +
         std::to_string(static_cast<int>(view.crop.size.y)) + " px").c_str());
    draw_list->PopClipRect();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Drag inside to move the crop. Drag a corner to resize it. The source image is never rewritten.");
    }
}

void draw_workspace(fabric::editor::ProjectSession& session,
                    fabric::editor::BehaviorSession& behavior_session,
                    fabric::editor::TransformationSession& transformation_session,
                    SDL_Window* window,
                    std::array<char, 1024>& path_buffer,
                    CreationUiState& creation,
                    ImportUiState& imports,
                    AssetPreview& preview,
                    AssetPreview& pending_import_preview,
                    std::unordered_map<std::string, AssetPreview>& texture_cache,
                    CanvasUiState& canvas,
                    const EntityPreviewResult& entity_preview,
                    const fabric::render::VisualCompositionDrawResult&
                        visual_preview,
                    AnimationUiState& animation_ui,
                    TexturedPathUiState& path_ui,
                    ProjectSettingsUiState& project_settings,
                    std::optional<std::pair<std::size_t,
                                            fabric::project::EntityDrawableKind>>&
                        pending_drawable_kind,
                    bool& request_open,
                    bool& request_png,
                    bool& request_svg,
                    fabric::editor::SessionTransitionGuard& transition_guard,
                    bool& running,
                    std::string& status) {
    active_picker_session = &session;
    active_picker_texture_cache = &texture_cache;
    canvas.native_canvas = false;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menu_height = ImGui::GetFrameHeight();
    const float status_height = 34.0F;
    static float left_width = 280.0F;
    static float right_width = 320.0F;
    const float initial_left_width = std::clamp(viewport->Size.x * 0.22F, 240.0F, 330.0F);
    const float initial_right_width = std::clamp(viewport->Size.x * 0.24F, 270.0F, 360.0F);
    static bool panel_widths_initialized = false;
    if (!panel_widths_initialized) {
        left_width = initial_left_width;
        right_width = initial_right_width;
        panel_widths_initialized = true;
    }
    left_width = std::clamp(left_width, 240.0F,
                            std::max(240.0F, viewport->Size.x - right_width - 320.0F));
    right_width = std::clamp(right_width, 270.0F,
                             std::max(270.0F, viewport->Size.x - left_width - 320.0F));
    const float content_height = viewport->Size.y - menu_height - status_height;

    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - right_width,
                             viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({right_width, content_height});
    ImGui::Begin("Project", nullptr, fixed_panel_flags);
    draw_project_tree(session, preview, status);
    if (!session.has_project()) {
        ImGui::Spacing();
        if (ImGui::Button("Create project", {-1.0F, 0.0F})) {
            creation.request_project = true;
        }
        ImGui::SeparatorText("Open");
        if (ImGui::Button("Open project", {-1.0F, 0.0F})) {
            request_open = true;
        }
    } else {
        ImGui::Spacing();
        if (!session.selected_resource()) {
            ImGui::SeparatorText("Current state");
            ImGui::BulletText("Current project: %s", session.manifest()->name.c_str());
            ImGui::BulletText("Active resource: none");
            ImGui::TextWrapped("Next action: select a resource or add one from the menu.");
        }
        if (ImGui::Button("+ Add resource...", {-1.0F, 0.0F})) {
            ImGui::OpenPopup("Add resource");
        }
        if (ImGui::BeginPopup("Add resource")) {
            if (ImGui::MenuItem("New vector artwork...")) {
                creation.request_artwork = true;
            }
            if (ImGui::MenuItem("New visual preset...")) {
                creation.request_visual_preset = true;
            }
            if (ImGui::MenuItem("New visual composition...")) {
                creation.request_visual_composition = true;
            }
            if (ImGui::MenuItem("New visual component...")) {
                creation.request_visual_component = true;
            }
            if (ImGui::MenuItem("New material / fill...")) {
                creation.request_material = true;
            }
            if (ImGui::MenuItem("New entity...")) {
                creation.request_entity = true;
            }
            if (ImGui::MenuItem("New animation...")) {
                creation.request_animation = true;
            }
            if (ImGui::MenuItem("New input bindings...")) {
                creation.request_input = true;
            }
            if (ImGui::MenuItem("Add existing resource...")) {
                ImGui::OpenPopup("Add existing resource");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import PNG texture...")) {
                imports.png.attempted = false;
                request_png = true;
            }
            if (ImGui::MenuItem("Import linked SVG...")) {
                imports.svg.attempted = false;
                request_svg = true;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    draw_existing_resource_popup(session, preview, status);

    ImGui::SetNextWindowPos({viewport->Pos.x + left_width,
                             viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({viewport->Size.x - left_width - right_width,
                              content_height});
    ImGui::Begin("Preview", nullptr,
                 fixed_panel_flags | ImGuiWindowFlags_NoBackground);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw_list = ImGui::GetWindowDrawList();
    const bool native_selected = session.selected_resource() != nullptr &&
        session.selected_resource()->kind ==
        fabric::editor::StudioResourceKind::vector &&
        session.selected_resource()->native && session.created_vector();
    const bool entity_selected = session.selected_resource() != nullptr &&
        session.selected_resource()->kind ==
            fabric::editor::StudioResourceKind::entity &&
        session.selected_entity();
    const bool visual_selected = session.selected_resource() != nullptr &&
        (session.selected_resource()->kind ==
             fabric::editor::StudioResourceKind::textured_path ||
         session.selected_resource()->kind ==
             fabric::editor::StudioResourceKind::visual_composition ||
         session.selected_resource()->kind ==
             fabric::editor::StudioResourceKind::visual_component);
    const bool open_gl_canvas = native_selected || visual_selected ||
        entity_selected ||
        (session.selected_resource() != nullptr &&
         session.selected_resource()->kind ==
             fabric::editor::StudioResourceKind::animation &&
         session.selected_entity() && !entity_preview.packets.empty());
    if (!open_gl_canvas) {
        draw_list->AddRectFilled(
            origin, {origin.x + available.x, origin.y + available.y},
            IM_COL32(21, 24, 30, 255), 4.0F);
    }
    constexpr float grid = 32.0F;
    for (float x = origin.x; x < origin.x + available.x; x += grid) {
        draw_list->AddLine({x, origin.y}, {x, origin.y + available.y},
                           IM_COL32(43, 48, 58, 120));
    }
    for (float y = origin.y; y < origin.y + available.y; y += grid) {
        draw_list->AddLine({origin.x, y}, {origin.x + available.x, y},
                           IM_COL32(43, 48, 58, 120));
    }
    if (native_selected) {
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 8.0F});
        ImGui::TextUnformatted("Gizmo");
        ImGui::SameLine();
        if (ImGui::RadioButton("Move", canvas.tool == CanvasUiState::Tool::move)) {
            canvas.tool = CanvasUiState::Tool::move;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", canvas.tool == CanvasUiState::Tool::rotate)) {
            canvas.tool = CanvasUiState::Tool::rotate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", canvas.tool == CanvasUiState::Tool::scale)) {
            canvas.tool = CanvasUiState::Tool::scale;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Pivot", canvas.tool == CanvasUiState::Tool::pivot)) {
            canvas.tool = CanvasUiState::Tool::pivot;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Pen", canvas.tool == CanvasUiState::Tool::pen)) {
            canvas.tool = CanvasUiState::Tool::pen;
        }
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 34.0F});
        draw_native_vector_canvas(
            session, canvas,
            {std::max(1.0F, available.x - 16.0F),
             std::max(1.0F, available.y - 42.0F)});
    } else if (visual_selected) {
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 8.0F});
        if (visual_preview.errors.empty()) {
            ImGui::TextUnformatted("Resolved visual preview");
        } else {
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                               "Visual preview (%zu unresolved)",
                               visual_preview.errors.size());
        }
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 34.0F});
        draw_packet_preview_canvas(
            canvas, {std::max(1.0F, available.x - 16.0F),
                     std::max(1.0F, available.y - 42.0F)},
            "Visual component");
    } else if (entity_selected ||
               (session.selected_resource() != nullptr &&
                session.selected_resource()->kind ==
                    fabric::editor::StudioResourceKind::animation &&
                session.selected_entity() && !entity_preview.packets.empty())) {
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 8.0F});
        if (entity_preview.errors.empty()) {
            ImGui::TextUnformatted(entity_selected
                                        ? "Entity preview"
                                        : "Animated entity preview");
        } else {
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                               "Entity preview (%zu unresolved)",
                               entity_preview.errors.size());
        }
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 34.0F});
        draw_packet_preview_canvas(
            canvas, {std::max(1.0F, available.x - 16.0F),
                     std::max(1.0F, available.y - 42.0F)},
            "Entity preview", &session);
    } else if (preview.texture != 0U && session.imported_texture() &&
               session.selected_resource() != nullptr &&
               session.selected_resource()->kind ==
                   fabric::editor::StudioResourceKind::texture) {
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 8.0F});
        ImGui::TextUnformatted("Raster crop · non-destructive source");
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 34.0F});
        draw_raster_crop_canvas(
            session, preview, canvas,
            {std::max(1.0F, available.x - 16.0F),
             std::max(1.0F, available.y - 42.0F)},
            status);
    } else if (preview.texture != 0U) {
        const float image_width = static_cast<float>(preview.width);
        const float image_height = static_cast<float>(preview.height);
        const float scale = std::min((available.x - 40.0F) / image_width,
                                     (available.y - 40.0F) / image_height);
        const ImVec2 image_size{image_width * scale, image_height * scale};
        ImGui::SetCursorScreenPos({origin.x + (available.x - image_size.x) * 0.5F,
                                   origin.y + (available.y - image_size.y) * 0.5F});
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)),
                     image_size, {0.0F, 1.0F}, {1.0F, 0.0F});
    } else {
        const char* preview_message = session.has_project()
                                          ? "Import a PNG or SVG to begin"
                                          : "Open a project to begin";
        const ImVec2 text_size = ImGui::CalcTextSize(preview_message);
        draw_list->AddText({origin.x + (available.x - text_size.x) * 0.5F,
                            origin.y + (available.y - text_size.y) * 0.5F},
                           IM_COL32(158, 170, 180, 255), preview_message);
        ImGui::Dummy(available);
    }
    const auto draw_panel_splitter = [&](const char* id, const float x,
                                         float& panel_width, const float sign,
                                         const float minimum, const float maximum) {
        ImGui::SetCursorScreenPos({x - 3.0F, viewport->Pos.y + menu_height});
        ImGui::PushID(id);
        ImGui::InvisibleButton("##splitter", {6.0F, content_height});
        if (ImGui::IsItemActive()) {
            panel_width = std::clamp(panel_width + sign * ImGui::GetIO().MouseDelta.x,
                                     minimum, maximum);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::PopID();
    };
    draw_panel_splitter("left-panel-splitter", viewport->Pos.x + left_width,
                        left_width, 1.0F, 240.0F,
                        std::max(240.0F, viewport->Size.x - right_width - 320.0F));
    draw_panel_splitter("right-panel-splitter",
                        viewport->Pos.x + viewport->Size.x - right_width,
                        right_width, -1.0F, 270.0F,
                        std::max(270.0F, viewport->Size.x - left_width - 320.0F));
    ImGui::End();

    ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({left_width, content_height});
    ImGui::Begin("Inspector", nullptr, fixed_panel_flags);
    if (session.has_project()) {
        const auto* selected = session.selected_resource();
        if (selected != nullptr) {
            ImGui::TextUnformatted(selected->name.c_str());
            ImGui::TextDisabled("%s", selected->id.value.c_str());
            ImGui::TextDisabled("%s",
                                selected->document_path.generic_string().c_str());
        } else {
            ImGui::TextDisabled("Select a resource to inspect it.");
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::material &&
            session.selected_material()) {
            const auto& current = *session.selected_material();
            const auto commit_material = [&](fabric::project::MaterialDefinition value) {
                status = session.set_selected_material(std::move(value))
                    ? "Material changed."
                    : "Material change rejected; inspect diagnostics.";
            };
            ImGui::SeparatorText("Material properties");
            auto material = current;
            std::string name = material.document.name;
            if (draw_resource_name_field("Name##resource-edit", name)) {
                material.document.name = std::move(name);
                commit_material(std::move(material));
            }
            material = current;
            float color[]{material.color.red, material.color.green,
                          material.color.blue, material.color.alpha};
            if (ImGui::ColorEdit4("Color", color)) {
                material.color = {color[0], color[1], color[2], color[3]};
                commit_material(std::move(material));
            }
            material = current;
            float opacity = material.opacity;
            if (ImGui::SliderFloat("Opacity (0–1)", &opacity, 0.0F, 1.0F, "%.2f")) {
                material.opacity = opacity;
                commit_material(std::move(material));
            }
            draw_technical_tooltip("Opacity multiplier applied to the material color and texture.");
            material = current;
            const auto blend_label = std::string(
                fabric::project::to_string(material.blend));
            if (ImGui::BeginCombo("Blend mode", blend_label.c_str())) {
                for (const auto blend : {
                         fabric::project::MaterialBlendMode::normal,
                         fabric::project::MaterialBlendMode::additive,
                         fabric::project::MaterialBlendMode::multiply,
                         fabric::project::MaterialBlendMode::screen}) {
                    const auto label = std::string(fabric::project::to_string(blend));
                    if (ImGui::Selectable(label.c_str(), material.blend == blend)) {
                        material.blend = blend;
                        commit_material(std::move(material));
                    }
                }
                ImGui::EndCombo();
            }
            std::string texture_id = current.texture
                ? current.texture->id.value : std::string{};
            if (draw_project_resource_picker(
                    "Texture", session.resources(),
                    fabric::editor::StudioResourceKind::texture,
                    texture_id, true)) {
                material = current;
                material.texture = texture_id.empty()
                    ? std::optional<fabric::project::ResourceReference>{}
                    : std::optional<fabric::project::ResourceReference>{
                        fabric::project::ResourceReference{
                            {.value = texture_id}, "texture"}};
                commit_material(std::move(material));
            }
            std::string vector_id = current.vector_pattern
                ? current.vector_pattern->id.value : std::string{};
            if (draw_project_resource_picker(
                    "Vector pattern", session.resources(),
                    fabric::editor::StudioResourceKind::vector,
                    vector_id, true)) {
                material = current;
                material.vector_pattern = vector_id.empty()
                    ? std::optional<fabric::project::ResourceReference>{}
                    : std::optional<fabric::project::ResourceReference>{
                        fabric::project::ResourceReference{
                            {.value = vector_id}, "vector"}};
                commit_material(std::move(material));
            }
            material = current;
            float uv_offset[]{material.uv_transform.position.x,
                              material.uv_transform.position.y};
            if (ImGui::InputFloat2("UV offset (normalized)", uv_offset)) {
                material.uv_transform.position = {uv_offset[0], uv_offset[1]};
                commit_material(std::move(material));
            }
            draw_technical_tooltip("Normalized texture offset; values wrap with the selected texture.");
            material = current;
            float uv_scale[]{material.uv_transform.scale.x,
                             material.uv_transform.scale.y};
            if (ImGui::InputFloat2("UV scale (factor)", uv_scale)) {
                material.uv_transform.scale = {uv_scale[0], uv_scale[1]};
                commit_material(std::move(material));
            }
            draw_technical_tooltip("Texture repetition multiplier in each UV axis.");
            material = current;
            float uv_rotation = material.uv_transform.rotation_degrees;
            if (ImGui::InputFloat("UV rotation (degrees)", &uv_rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                material.uv_transform.rotation_degrees = uv_rotation;
                commit_material(std::move(material));
            }
            draw_technical_tooltip("Rotation applied around the UV pivot, in degrees.");
            material = current;
            float uv_pivot[]{material.uv_transform.pivot.x,
                             material.uv_transform.pivot.y};
            if (ImGui::InputFloat2("UV pivot (normalized)", uv_pivot)) {
                material.uv_transform.pivot = {uv_pivot[0], uv_pivot[1]};
                commit_material(std::move(material));
            }
            draw_technical_tooltip("Normalized center used by the UV rotation and scale.");
            const ImVec4 swatch{current.color.red, current.color.green,
                                current.color.blue,
                                current.color.alpha * current.opacity};
            ImGui::ColorButton("##material-preview", swatch,
                               ImGuiColorEditFlags_NoTooltip,
                               {ImGui::GetContentRegionAvail().x, 72.0F});
            ImGui::SeparatorText("Used by");
            bool used = false;
            for (const auto& resource : session.resources()) {
                if (resource.kind != fabric::editor::StudioResourceKind::entity)
                    continue;
                const auto loaded = fabric::project::load_entity(
                    session.project_root(), *session.manifest(),
                    resource.document_path);
                if (!loaded.ok() || !std::ranges::any_of(
                        loaded.entity->nodes, [&](const auto& node) {
                            return node.drawable.material &&
                                node.drawable.material->id == current.document.id;
                        })) continue;
                used = true;
                ImGui::BulletText("%s", resource.name.c_str());
            }
            if (!used) ImGui::TextDisabled("No entity reference.");
        }
        if (visual_selected) {
            ImGui::SeparatorText("Resolved visual");
            ImGui::Text("%zu draw packet(s)", visual_preview.packets.size());
            ImGui::Text("Bounds %.2f x %.2f", visual_preview.bounds.size.x,
                        visual_preview.bounds.size.y);
            if (session.selected_textured_path()) {
                const auto path = *session.selected_textured_path();
                static std::string selected_path_id;
                static std::size_t selected_path_command{};
                if (selected_path_id != path.document.id.value) {
                    selected_path_id = path.document.id.value;
                    selected_path_command = 0U;
                }
                ImGui::Text("%zu path command(s)", path.commands.size());
                ImGui::Text("Texture %s", path.texture.id.value.c_str());
                ImGui::SeparatorText("Pen and attachments");
                for (std::size_t index = 0; index < path.commands.size();
                     ++index) {
                    const auto& command = path.commands[index];
                    const char* kind = index == 0U ? "Start attachment" :
                        command.kind ==
                                fabric::project::TexturedPathCommandKind::line
                            ? "Line point" : "Bezier point";
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(kind,
                                          selected_path_command == index))
                        selected_path_command = index;
                    ImGui::PopID();
                }
                if (!path.commands.empty() &&
                    selected_path_command < path.commands.size()) {
                    auto command = path.commands[selected_path_command];
                    bool command_changed = ImGui::DragFloat2(
                        selected_path_command == 0U ? "Start (world units)" : "Endpoint (world units)",
                        &command.point.x, 0.05F);
                    draw_technical_tooltip("Position of the selected path command in world space.");
                    if (command.kind ==
                        fabric::project::TexturedPathCommandKind::cubic) {
                        command_changed |= ImGui::DragFloat2(
                            "Handle in (world units)", &command.control1.x, 0.05F);
                        draw_technical_tooltip("Incoming cubic handle position.");
                        command_changed |= ImGui::DragFloat2(
                            "Handle out (world units)", &command.control2.x, 0.05F);
                        draw_technical_tooltip("Outgoing cubic handle position.");
                    }
                    if (command_changed) {
                        auto candidate = path;
                        candidate.commands[selected_path_command] = command;
                        (void)session.set_selected_textured_path(
                            std::move(candidate));
                    }
                }
                if (ImGui::Button("Pen: add line")) {
                    auto candidate = path;
                    const auto endpoint = candidate.commands.back().point;
                    candidate.commands.push_back({
                        .kind = fabric::project::TexturedPathCommandKind::line,
                        .point = {endpoint.x + 1.0F, endpoint.y}});
                    if (session.set_selected_textured_path(
                            std::move(candidate)))
                        selected_path_command =
                            session.selected_textured_path()->commands.size() - 1U;
                }
                ImGui::SameLine();
                if (ImGui::Button("Pen: add Bezier")) {
                    auto candidate = path;
                    const auto endpoint = candidate.commands.back().point;
                    candidate.commands.push_back({
                        .kind = fabric::project::TexturedPathCommandKind::cubic,
                        .point = {endpoint.x + 1.0F, endpoint.y},
                        .control1 = {endpoint.x + 0.33F, endpoint.y},
                        .control2 = {endpoint.x + 0.67F, endpoint.y}});
                    if (session.set_selected_textured_path(
                            std::move(candidate)))
                        selected_path_command =
                            session.selected_textured_path()->commands.size() - 1U;
                }
                ImGui::BeginDisabled(path.commands.size() <=
                    (path.closed ? 3U : 2U));
                if (ImGui::Button("Remove last point")) {
                    auto candidate = path;
                    candidate.commands.pop_back();
                    if (session.set_selected_textured_path(
                            std::move(candidate)))
                        selected_path_command = std::min(
                            selected_path_command,
                            session.selected_textured_path()->commands.size() - 1U);
                }
                ImGui::EndDisabled();
                draw_disabled_reason(path.commands.size() <=
                                         (path.closed ? 3U : 2U),
                                     "Keep the minimum number of points for this path.");

                auto style = *session.selected_textured_path();
                bool style_changed = false;
                ImGui::SeparatorText("Ribbon and texture");
                style_changed |= ImGui::Checkbox("Closed", &style.closed);
                style_changed |= ImGui::DragFloat(
                    "Width (world units)", &style.width, 0.01F, 0.001F, 1000.0F);
                draw_technical_tooltip("Ribbon width rendered along the textured path.");
                style_changed |= ImGui::DragFloat2(
                    "Texture repeat (factor)", &style.uv_scale.x, 0.05F,
                    0.001F, 1000.0F);
                draw_technical_tooltip("Texture repetition multiplier along the path.");
                style_changed |= ImGui::DragFloat2(
                    "Texture offset (normalized)", &style.uv_offset.x, 0.01F);
                draw_technical_tooltip("Normalized offset applied to the path texture.");
                style_changed |= ImGui::ColorEdit4(
                    "Color", &style.color.red);
                style_changed |= ImGui::SliderFloat(
                    "Opacity (0–1)", &style.opacity, 0.0F, 1.0F);
                draw_technical_tooltip("Opacity applied to the textured path.");
                int uv_mode = style.uv_mode ==
                        fabric::project::TexturedPathUvMode::repeat ? 0 : 1;
                if (ImGui::Combo("UV mode", &uv_mode,
                                 "Repeat\0Stretch\0")) {
                    style.uv_mode = uv_mode == 0
                        ? fabric::project::TexturedPathUvMode::repeat
                        : fabric::project::TexturedPathUvMode::stretch;
                    style_changed = true;
                }
                if (style_changed)
                    (void)session.set_selected_textured_path(std::move(style));
                ImGui::SeparatorText("Texture animation preview");
                ImGui::Checkbox("Scroll texture", &path_ui.animate_texture);
                ImGui::DragFloat("Scroll speed (factor/s)", &path_ui.scroll_speed,
                                 0.05F, -100.0F, 100.0F);
                draw_technical_tooltip("Texture offset speed used by the preview animation.");
                if (ImGui::Button("Reset preview offset"))
                    path_ui.preview_offset = 0.0F;
            } else if (session.selected_visual_composition()) {
                const auto& composition =
                    *session.selected_visual_composition();
                static std::string selected_composition_id;
                static std::size_t selected_layer{};
                if (selected_composition_id != composition.document.id.value) {
                    selected_composition_id = composition.document.id.value;
                    selected_layer = 0U;
                }
                ImGui::SeparatorText("Layer tree");
                for (std::size_t index = 0; index < composition.layers.size();
                     ++index) {
                    const auto& layer = composition.layers[index];
                    ImGui::PushID(static_cast<int>(index));
                    auto flags = ImGuiTreeNodeFlags_Leaf |
                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (index == selected_layer)
                        flags |= ImGuiTreeNodeFlags_Selected;
                    ImGui::TreeNodeEx("layer", flags, "%s  ·  %s",
                                      layer.name.c_str(),
                                      std::string(fabric::project::to_string(
                                          layer.kind)).c_str());
                    if (ImGui::IsItemClicked()) selected_layer = index;
                    ImGui::PopID();
                }
                static std::string add_layer_composition_id;
                static fabric::project::VisualLayerKind add_layer_kind{
                    fabric::project::VisualLayerKind::raster};
                static std::string add_layer_resource_id;
                if (add_layer_composition_id != composition.document.id.value) {
                    add_layer_composition_id = composition.document.id.value;
                    add_layer_kind = fabric::project::VisualLayerKind::raster;
                    add_layer_resource_id.clear();
                }
                ImGui::SeparatorText("Add layer");
                const auto add_kind_label = std::string(
                    fabric::project::to_string(add_layer_kind));
                if (ImGui::BeginCombo("Layer type", add_kind_label.c_str())) {
                    for (const auto kind : {
                             fabric::project::VisualLayerKind::raster,
                             fabric::project::VisualLayerKind::vector,
                             fabric::project::VisualLayerKind::component,
                             fabric::project::VisualLayerKind::textured_path}) {
                        const bool selected = add_layer_kind == kind;
                        const auto label = std::string(
                            fabric::project::to_string(kind));
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            add_layer_kind = kind;
                            add_layer_resource_id.clear();
                        }
                    }
                    ImGui::EndCombo();
                }
                const auto accepts_kind = [&](const auto& resource) {
                    using Kind = fabric::editor::StudioResourceKind;
                    return (add_layer_kind ==
                                fabric::project::VisualLayerKind::raster &&
                            resource.kind == Kind::texture) ||
                        (add_layer_kind ==
                                fabric::project::VisualLayerKind::vector &&
                            resource.kind == Kind::vector) ||
                        (add_layer_kind ==
                                fabric::project::VisualLayerKind::component &&
                            resource.kind == Kind::visual_component) ||
                        (add_layer_kind ==
                                fabric::project::VisualLayerKind::textured_path &&
                            resource.kind == Kind::textured_path);
                };
                const auto add_resource = std::ranges::find_if(
                    session.resources(), [&](const auto& resource) {
                        return accepts_kind(resource) &&
                            resource.id.value == add_layer_resource_id;
                    });
                const char* add_resource_label =
                    add_resource == session.resources().end()
                    ? "Choose a resource..." : add_resource->name.c_str();
                if (ImGui::BeginCombo("Resource", add_resource_label)) {
                    for (const auto& resource : session.resources()) {
                        if (!accepts_kind(resource)) continue;
                        const bool selected =
                            resource.id.value == add_layer_resource_id;
                        if (ImGui::Selectable(resource.name.c_str(), selected))
                            add_layer_resource_id = resource.id.value;
                    }
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(add_resource == session.resources().end());
                if (ImGui::Button("Add selected resource")) {
                    auto candidate = composition;
                    auto id = add_resource->id.value;
                    const auto base = id;
                    std::size_t suffix = 2U;
                    while (std::ranges::any_of(
                        candidate.layers, [&](const auto& layer) {
                            return layer.id == id;
                        })) id = base + "-" + std::to_string(suffix++);
                    std::string expected_type;
                    if (add_layer_kind ==
                        fabric::project::VisualLayerKind::raster)
                        expected_type = "texture";
                    else if (add_layer_kind ==
                             fabric::project::VisualLayerKind::vector)
                        expected_type = "vector";
                    else if (add_layer_kind ==
                             fabric::project::VisualLayerKind::component)
                        expected_type = "visualComponent";
                    else expected_type = "texturedPath";
                    fabric::project::VisualCompositionLayer layer{
                        .id = id,
                        .name = add_resource->name,
                        .kind = add_layer_kind,
                        .resource = {add_resource->id, expected_type},
                        .z_order = static_cast<float>(candidate.layers.size())};
                    if (add_layer_kind ==
                        fabric::project::VisualLayerKind::component)
                        layer.component_instance =
                            fabric::project::VisualComponentInstance{};
                    candidate.layers.push_back(std::move(layer));
                    if (session.set_selected_visual_composition(
                            std::move(candidate))) {
                        selected_layer = session.selected_visual_composition()
                            ->layers.size() - 1U;
                    }
                }
                ImGui::EndDisabled();
                draw_disabled_reason(add_resource == session.resources().end(),
                                     "Choose a compatible indexed resource first.");
                if (!composition.layers.empty() &&
                    selected_layer < composition.layers.size()) {
                    if (ImGui::Button("Duplicate layer")) {
                        auto candidate = composition;
                        auto copy = candidate.layers[selected_layer];
                        const auto base = copy.id + "-copy";
                        copy.id = base;
                        std::size_t suffix = 2U;
                        while (std::ranges::any_of(
                            candidate.layers, [&](const auto& layer) {
                                return layer.id == copy.id;
                            })) {
                            copy.id = base + "-" +
                                std::to_string(suffix++);
                        }
                        copy.name += " copy";
                        candidate.layers.insert(
                            candidate.layers.begin() +
                                static_cast<std::ptrdiff_t>(selected_layer + 1U),
                            std::move(copy));
                        if (session.set_selected_visual_composition(
                                std::move(candidate))) ++selected_layer;
                    }
                    const auto& current =
                        session.selected_visual_composition()
                            ->layers[selected_layer];
                    auto layer = current;
                    bool changed = false;
                    ImGui::SeparatorText("Selected layer");
                    changed |= ImGui::Checkbox("Visible", &layer.visible);
                    changed |= ImGui::DragFloat("Z order (world units)", &layer.z_order,
                                                0.1F);
                    draw_technical_tooltip("Layer draw order; larger values render later.");
                    changed |= ImGui::SliderFloat("Opacity (0–1)", &layer.opacity,
                                                  0.0F, 1.0F);
                    draw_technical_tooltip("Layer opacity, from fully transparent to opaque.");
                    changed |= ImGui::SliderFloat2("Anchor (normalized)", &layer.anchor.x,
                                                   0.0F, 1.0F);
                    draw_technical_tooltip("Normalized anchor used to position the layer transform.");
                    changed |= ImGui::DragFloat2("Position (world units)",
                                                 &layer.transform.position.x,
                                                 0.05F);
                    draw_technical_tooltip("Layer translation in project world units.");
                    changed |= ImGui::DragFloat(
                        "Rotation (degrees)", &layer.transform.rotation_degrees, 0.5F);
                    draw_technical_tooltip("Layer rotation around its pivot, in degrees.");
                    changed |= ImGui::DragFloat2("Scale (factor)",
                                                 &layer.transform.scale.x,
                                                 0.01F, 0.001F, 100.0F);
                    draw_technical_tooltip("Layer scale multiplier on each axis.");
                    changed |= ImGui::DragFloat2("Pivot (world units)",
                                                 &layer.transform.pivot.x,
                                                 0.01F);
                    draw_technical_tooltip("Layer pivot in project world units.");
                    if (layer.kind ==
                        fabric::project::VisualLayerKind::raster) {
                        const auto texture = fabric::project::load_texture_asset(
                            session.project_root(), *session.manifest(),
                            fabric::project::texture_document_path(
                                *session.manifest(), layer.resource.id));
                        if (texture.ok()) {
                            ImGui::SeparatorText("Raster crop");
                            if (!layer.raster_view) {
                                if (ImGui::Button("Enable crop view")) {
                                    layer.raster_view =
                                        fabric::project::RasterView{
                                            .crop = {{0.0F, 0.0F},
                                                {static_cast<float>(
                                                     texture.asset->width),
                                                 static_cast<float>(
                                                     texture.asset->height)}}};
                                    changed = true;
                                }
                            } else {
                                auto& view = *layer.raster_view;
                                if (ImGui::DragFloat2(
                                        "Crop origin (pixels)", &view.crop.origin.x,
                                        1.0F, 0.0F,
                                        static_cast<float>(std::max(
                                            texture.asset->width,
                                            texture.asset->height)))) {
                                    view.crop.origin.x = std::clamp(
                                        view.crop.origin.x, 0.0F,
                                        static_cast<float>(texture.asset->width) -
                                            1.0F);
                                    view.crop.origin.y = std::clamp(
                                        view.crop.origin.y, 0.0F,
                                        static_cast<float>(texture.asset->height) -
                                            1.0F);
                                    changed = true;
                                }
                                if (ImGui::DragFloat2(
                                        "Crop size (pixels)", &view.crop.size.x, 1.0F,
                                        1.0F,
                                        static_cast<float>(std::max(
                                            texture.asset->width,
                                            texture.asset->height)))) {
                                    view.crop.size.x = std::clamp(
                                        view.crop.size.x, 1.0F,
                                        static_cast<float>(texture.asset->width) -
                                            view.crop.origin.x);
                                    view.crop.size.y = std::clamp(
                                        view.crop.size.y, 1.0F,
                                        static_cast<float>(texture.asset->height) -
                                            view.crop.origin.y);
                                    changed = true;
                                }
                                changed |= ImGui::SliderFloat2(
                                    "Crop pivot (normalized)", &view.pivot.x, 0.0F, 1.0F);
                                if (ImGui::Button("Reset full crop")) {
                                    view.crop = {{0.0F, 0.0F},
                                        {static_cast<float>(texture.asset->width),
                                         static_cast<float>(texture.asset->height)}};
                                    changed = true;
                                }
                            }
                        }
                    }
                    if (changed) {
                        auto candidate =
                            *session.selected_visual_composition();
                        candidate.layers[selected_layer] = std::move(layer);
                        (void)session.set_selected_visual_composition(
                            std::move(candidate));
                    }
                }
            } else if (session.selected_visual_component()) {
                const auto& component = *session.selected_visual_component();
                static std::string selected_component_id;
                static std::size_t selected_anchor{};
                static std::size_t selected_parameter{};
                if (selected_component_id != component.document.id.value) {
                    selected_component_id = component.document.id.value;
                    selected_anchor = 0U;
                    selected_parameter = 0U;
                }
                ImGui::SeparatorText("Anchors");
                for (std::size_t index = 0; index < component.anchors.size();
                     ++index) {
                    const auto& anchor = component.anchors[index];
                    if (ImGui::Selectable(anchor.name.c_str(),
                                          selected_anchor == index))
                        selected_anchor = index;
                }
                if (!component.anchors.empty() &&
                    selected_anchor < component.anchors.size()) {
                    if (ImGui::Button("Duplicate anchor")) {
                        auto candidate = component;
                        auto copy = candidate.anchors[selected_anchor];
                        const auto base = copy.id + "-copy";
                        copy.id = base;
                        std::size_t suffix = 2U;
                        while (std::ranges::any_of(
                            candidate.anchors, [&](const auto& anchor) {
                                return anchor.id == copy.id;
                            })) {
                            copy.id = base + "-" +
                                std::to_string(suffix++);
                        }
                        copy.name += " copy";
                        candidate.anchors.push_back(std::move(copy));
                        if (session.set_selected_visual_component(
                                std::move(candidate))) {
                            selected_anchor = session.selected_visual_component()
                                ->anchors.size() - 1U;
                        }
                    }
                    auto anchor = session.selected_visual_component()
                                      ->anchors[selected_anchor];
                    if (ImGui::DragFloat2("Anchor position (world units)",
                                          &anchor.position.x, 0.05F)) {
                        auto candidate =
                            *session.selected_visual_component();
                        candidate.anchors[selected_anchor] = std::move(anchor);
                        (void)session.set_selected_visual_component(
                            std::move(candidate));
                    }
                    ImGui::SetItemTooltip("Position of the visual component anchor in project world units.");
                }

                const auto& current_component =
                    *session.selected_visual_component();
                ImGui::SeparatorText("Parameters");
                for (std::size_t index = 0;
                     index < current_component.parameters.size(); ++index) {
                    const auto& parameter = current_component.parameters[index];
                    if (ImGui::Selectable(parameter.name.c_str(),
                                          selected_parameter == index))
                        selected_parameter = index;
                }
                if (!current_component.parameters.empty() &&
                    selected_parameter < current_component.parameters.size()) {
                    auto parameter =
                        current_component.parameters[selected_parameter];
                    bool changed = ImGui::Checkbox(
                        "Animatable", &parameter.animatable);
                    ImGui::TextDisabled("Target %s.%s.%s",
                        parameter.target.node_id.c_str(),
                        parameter.target.component_id.c_str(),
                        parameter.target.property_id.c_str());
                    if (auto* value = std::get_if<float>(
                            &parameter.default_value)) {
                        changed |= ImGui::DragFloat("Default", value, 0.05F);
                        ImGui::SetItemTooltip("Default value used when this component parameter is not overridden; its unit follows the parameter schema.");
                    } else if (auto* value = std::get_if<std::int64_t>(
                                   &parameter.default_value)) {
                        changed |= ImGui::InputScalar(
                            "Default", ImGuiDataType_S64, value);
                        ImGui::SetItemTooltip("Default integer used when this component parameter is not overridden; its unit follows the parameter schema.");
                    } else if (auto* value = std::get_if<bool>(
                                   &parameter.default_value)) {
                        changed |= ImGui::Checkbox("Default", value);
                    } else if (auto* value = std::get_if<std::string>(
                                   &parameter.default_value)) {
                        changed |= ImGui::InputText("Default", value);
                    } else if (auto* value = std::get_if<fabric::core::Vec2>(
                                   &parameter.default_value)) {
                        changed |= ImGui::DragFloat2("Default", &value->x,
                                                     0.05F);
                        ImGui::SetItemTooltip("Default vector used when this component parameter is not overridden; its unit follows the parameter schema.");
                    } else if (auto* value = std::get_if<fabric::core::Color>(
                                   &parameter.default_value)) {
                        changed |= ImGui::ColorEdit4("Default", &value->red);
                    } else if (const auto* value = std::get_if<
                                   fabric::project::ResourceReference>(
                                   &parameter.default_value)) {
                        ImGui::TextDisabled("Default %s (%s)",
                                            value->id.value.c_str(),
                                            value->expected_type.c_str());
                    }
                    if (changed) {
                        auto candidate =
                            *session.selected_visual_component();
                        candidate.parameters[selected_parameter] =
                            std::move(parameter);
                        (void)session.set_selected_visual_component(
                            std::move(candidate));
                    }
                }
                ImGui::TextDisabled("%zu variant(s)",
                                    current_component.variants.size());
            }
            for (const auto& error : visual_preview.errors) {
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s",
                                   error.c_str());
            }
        }
        if (preview.texture != 0U) {
            ImGui::SeparatorText(preview.kind == PreviewKind::vector
                                     ? "Imported vector"
                                     : "Imported texture");
            ImGui::Text("%u x %u RGBA8", preview.width, preview.height);
            if (preview.kind == PreviewKind::texture &&
                session.imported_texture()) {
                ImGui::TextUnformatted(
                    session.imported_texture()->asset.document.name.c_str());
                ImGui::TextDisabled("%s",
                    session.imported_texture()->asset.document.id.value.c_str());
            }
            if (preview.kind == PreviewKind::texture &&
                session.imported_texture()) {
                ImGui::TextWrapped("%s",
                    session.imported_texture()->asset.source.generic_string().c_str());
                if (const auto references = session.incoming_references(
                        fabric::editor::StudioResourceKind::texture,
                        session.imported_texture()->asset.document.id)) {
                    ImGui::Text("Used by: %zu resource(s)", references->size());
                    for (const auto& reference : *references) {
                        ImGui::BulletText("%s (%s)", reference.name.c_str(),
                                         reference.id.value.c_str());
                        ImGui::SameLine();
                        ImGui::PushID(reference.id.value.c_str());
                        if (ImGui::SmallButton("Open in Resource Explorer"))
                            static_cast<void>(session.select_resource(
                                reference.kind, reference.id));
                        ImGui::PopID();
                    }
                }
                ImGui::SeparatorText("Raster view (non-destructive)");
                static std::string raster_view_edit_id;
                static fabric::project::RasterView raster_view_edit;
                static std::optional<fabric::project::RasterView>
                    raster_view_source;
                const auto& texture = *session.imported_texture();
                if (raster_view_edit_id != texture.asset.document.id.value ||
                    (raster_view_source != texture.asset.view &&
                     !ImGui::IsAnyItemActive())) {
                    raster_view_edit_id = texture.asset.document.id.value;
                    raster_view_edit.crop = texture.asset.view
                        ? texture.asset.view->crop
                        : fabric::core::Rect{
                            {0.0F, 0.0F},
                            {static_cast<float>(texture.asset.width),
                             static_cast<float>(texture.asset.height)}};
                    raster_view_edit.pivot = texture.asset.view
                        ? texture.asset.view->pivot
                        : fabric::core::Vec2{0.5F, 0.5F};
                    raster_view_edit.transform = texture.asset.view
                        ? texture.asset.view->transform
                        : fabric::core::Transform{};
                    raster_view_edit.filter = texture.asset.view
                        ? texture.asset.view->filter
                        : fabric::project::RasterFilter::linear;
                    raster_view_source = texture.asset.view;
                }
                float crop_origin[2]{raster_view_edit.crop.origin.x,
                                     raster_view_edit.crop.origin.y};
                float crop_size[2]{raster_view_edit.crop.size.x,
                                   raster_view_edit.crop.size.y};
                float crop_pivot[2]{raster_view_edit.pivot.x,
                                    raster_view_edit.pivot.y};
                float view_position[2]{raster_view_edit.transform.position.x,
                                       raster_view_edit.transform.position.y};
                float view_scale[2]{raster_view_edit.transform.scale.x,
                                    raster_view_edit.transform.scale.y};
                float view_rotation = raster_view_edit.transform.rotation_degrees;
                ImGui::InputFloat2("Crop origin (pixels)", crop_origin);
                ImGui::SetItemTooltip("Top-left crop origin measured in source pixels.");
                ImGui::InputFloat2("Crop size (pixels)", crop_size);
                ImGui::SetItemTooltip("Crop width and height measured in source pixels.");
                ImGui::InputFloat2("Pivot (normalized)", crop_pivot);
                ImGui::SetItemTooltip("Normalized pivot used by the raster view transform.");
                ImGui::InputFloat2("View position (world units)", view_position);
                ImGui::SetItemTooltip("Raster view translation in project world units.");
                ImGui::InputFloat("View rotation (degrees)", &view_rotation);
                ImGui::SetItemTooltip("Raster view rotation around its normalized pivot.");
                ImGui::InputFloat2("View scale (factor)", view_scale);
                ImGui::SetItemTooltip("Raster view scale multiplier on each axis.");
                if (ImGui::Button("Apply crop/view")) {
                    raster_view_edit.crop.origin = {crop_origin[0], crop_origin[1]};
                    raster_view_edit.crop.size = {crop_size[0], crop_size[1]};
                    raster_view_edit.pivot = {crop_pivot[0], crop_pivot[1]};
                    raster_view_edit.transform.position =
                        {view_position[0], view_position[1]};
                    raster_view_edit.transform.rotation_degrees = view_rotation;
                    raster_view_edit.transform.scale =
                        {view_scale[0], view_scale[1]};
                    if (session.set_selected_texture_view(raster_view_edit)) {
                        raster_view_source = raster_view_edit;
                        status = "Raster view saved in the document.";
                    } else {
                        status = "Raster view rejected; inspect diagnostics.";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset full source")) {
                    if (session.reset_selected_texture_view()) {
                        raster_view_edit_id.clear();
                        raster_view_source.reset();
                        status = "Raster view reset to the full source.";
                    } else {
                        status = "Raster view reset failed; inspect diagnostics.";
                    }
                }
                const auto view = texture.asset.view
                    ? texture.asset.view->crop
                    : fabric::core::Rect{
                        {0.0F, 0.0F},
                        {static_cast<float>(texture.asset.width),
                         static_cast<float>(texture.asset.height)}};
                const ImVec2 cropped_size{220.0F,
                    220.0F * view.size.y / std::max(view.size.x, 1.0F)};
                const ImVec2 uv_min{
                    view.origin.x / static_cast<float>(texture.asset.width),
                    1.0F - (view.origin.y + view.size.y) /
                        static_cast<float>(texture.asset.height)};
                const ImVec2 uv_max{
                    (view.origin.x + view.size.x) /
                        static_cast<float>(texture.asset.width),
                    1.0F - view.origin.y /
                        static_cast<float>(texture.asset.height)};
                ImGui::TextUnformatted("Cropped preview");
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.texture)),
                             cropped_size, uv_min, uv_max);
            }
            if (preview.kind == PreviewKind::vector && session.imported_vector()) {
                ImGui::TextUnformatted(
                    session.imported_vector()->asset.document.name.c_str());
                ImGui::TextDisabled("%s",
                    session.imported_vector()->asset.document.id.value.c_str());
                ImGui::TextWrapped("%s",
                    session.imported_vector()->asset.source.generic_string().c_str());
                ImGui::SeparatorText("Authoring");
                ImGui::TextWrapped(
                    "Convert the linked SVG into editable native paths. The original SVG remains unchanged.");
                if (ImGui::Button("Convert to native artwork")) {
                    if (session.convert_selected_linked_svg_to_native()) {
                        status = "SVG converted to native artwork.";
                    } else {
                        status = "SVG conversion failed; inspect the diagnostics.";
                    }
                }
            }
        }
        if (creation.prepared_artwork && selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::vector &&
            selected->native) {
            ImGui::SeparatorText("Created native artwork");
            ImGui::TextUnformatted(creation.prepared_artwork->name.c_str());
            if (session.created_vector()) {
                ImGui::TextDisabled(
                    "%s", session.created_vector()->document.id.value.c_str());
            }
            ImGui::Text("%.2f x %.2f world units",
                        creation.prepared_artwork->width,
                        creation.prepared_artwork->height);
            ImGui::TextDisabled("Published as VectorAsset v2 native.");
        }
        if (selected != nullptr && selected->native && session.created_vector() &&
            session.created_vector()->native) {
            ImGui::SeparatorText("Nodes");
            const auto& nodes = session.created_vector()->native->nodes;
            const auto add_rectangle_node = [&] {
                std::string id = "node-" + std::to_string(nodes.size() + 1U);
                while (std::ranges::any_of(nodes, [&](const auto& candidate) {
                    return candidate.id == id;
                })) id += "-copy";
                const auto size = session.created_vector()->native->size;
                fabric::project::VectorNode node{
                    .id = id,
                    .name = "Rectangle " + std::to_string(nodes.size() + 1U),
                    .shape = {.id = id + "-shape",
                              .kind = fabric::project::VectorShapeKind::rectangle,
                              .bounds = {{-size.x * 0.125F, -size.y * 0.125F},
                                         {std::max(0.01F, size.x * 0.25F),
                                          std::max(0.01F, size.y * 0.25F)}}},
                    .fill = {.kind = fabric::project::VectorFillKind::solid,
                             .color = fabric::core::Color{
                                 1.0F, 1.0F, 1.0F, 1.0F}}};
                if (session.add_selected_vector_node(std::move(node))) {
                    canvas.selected_node = nodes.size();
                    status = "Vector node added.";
                } else {
                    status = "Vector node rejected; inspect diagnostics.";
                }
            };
            if (ImGui::Button("Add rectangle")) add_rectangle_node();
            if (nodes.empty()) {
                ImGui::TextDisabled("This artwork has no nodes.");
            } else {
            canvas.selected_node = std::min(canvas.selected_node,
                                            nodes.size() - 1U);
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") &&
                session.duplicate_selected_vector_node(canvas.selected_node)) {
                canvas.selected_node = nodes.size();
                status = "Vector node duplicated.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Up") && canvas.selected_node > 0U &&
                session.move_selected_vector_node(
                    canvas.selected_node, canvas.selected_node - 1U)) {
                --canvas.selected_node;
                status = "Vector node moved.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Down") &&
                canvas.selected_node + 1U < nodes.size() &&
                session.move_selected_vector_node(
                    canvas.selected_node, canvas.selected_node + 1U)) {
                ++canvas.selected_node;
                status = "Vector node moved.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete..."))
                ImGui::OpenPopup("Delete vector node?");
            if (ImGui::BeginPopupModal("Delete vector node?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto& pending = nodes[canvas.selected_node];
                const auto references = std::ranges::count_if(
                    nodes, [&](const auto& candidate) {
                        return candidate.parent_id == pending.id ||
                            candidate.clip_node_id == pending.id;
                    });
                ImGui::Text("Delete '%s'?", pending.name.c_str());
                ImGui::TextWrapped(
                    "This node has %zu child or clip reference(s). Referenced or locked nodes are protected.",
                    references);
                ImGui::BeginDisabled(references != 0U || pending.locked);
                ImGui::PushStyleColor(
                    ImGuiCol_Button, ImVec4{0.62F, 0.16F, 0.14F, 1.0F});
                if (ImGui::Button("Delete node") &&
                    session.remove_selected_vector_node(canvas.selected_node)) {
                    canvas.selected_node = canvas.selected_node == 0U
                        ? 0U : canvas.selected_node - 1U;
                    status = "Vector node deleted.";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
                draw_disabled_reason(references != 0U || pending.locked,
                                     "Remove child and clip references first, and unlock the node.");
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            canvas.selected_node = std::min(canvas.selected_node,
                                            nodes.size() - 1);
            const std::function<void(const std::optional<std::string>&)>
                draw_vector_children = [&](const auto& parent_id) {
                    for (std::size_t node_index = 0; node_index < nodes.size();
                         ++node_index) {
                        const auto& candidate = nodes[node_index];
                        if (candidate.parent_id != parent_id) continue;
                        const bool has_children = std::ranges::any_of(
                            nodes, [&](const auto& child) {
                                return child.parent_id == candidate.id;
                            });
                        auto flags = ImGuiTreeNodeFlags_OpenOnArrow |
                            ImGuiTreeNodeFlags_OpenOnDoubleClick |
                            ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf;
                        if (canvas.selected_node == node_index)
                            flags |= ImGuiTreeNodeFlags_Selected;
                        ImGui::PushID(candidate.id.c_str());
                        const bool open = ImGui::TreeNodeEx(
                            candidate.name.c_str(), flags);
                        if (ImGui::IsItemClicked() &&
                            !ImGui::IsItemToggledOpen())
                            canvas.selected_node = node_index;
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s%s%s", candidate.id.c_str(),
                            candidate.visible ? "" : " · hidden",
                            candidate.locked ? " · locked" : "");
                        if (open) {
                            draw_vector_children(candidate.id);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                };
            draw_vector_children(std::nullopt);
            ImGui::SeparatorText("Node properties");
            auto node = nodes[canvas.selected_node];
            const auto commit_node = [&](fabric::project::VectorNode changed) {
                if (session.set_selected_vector_node(
                        canvas.selected_node, std::move(changed))) {
                    status = "Vector node changed.";
                } else {
                    status = "Vector change rejected; inspect diagnostics.";
                }
            };
            bool locked = node.locked;
            if (ImGui::Checkbox("Locked", &locked)) {
                node.locked = locked;
                commit_node(node);
            }
            ImGui::BeginDisabled(node.locked);
            std::string node_name = node.name;
            if (draw_resource_name_field("Name", node_name, 360.0F)) {
                node.name = std::move(node_name);
                commit_node(node);
            }
            bool visible = node.visible;
            if (ImGui::Checkbox("Visible", &visible)) {
                node.visible = visible;
                commit_node(node);
            }
            const auto node_reference_label = [&](const std::optional<std::string>& reference,
                                                  const char* empty_label) {
                if (!reference.has_value()) return std::string{empty_label};
                for (const auto& candidate : nodes) {
                    if (candidate.id == *reference) return candidate.name;
                }
                return std::string{"Missing: "} + *reference;
            };
            if (ImGui::BeginCombo(
                    "Parent",
                    node_reference_label(node.parent_id, "None").c_str())) {
                if (ImGui::Selectable("None", !node.parent_id.has_value())) {
                    node.parent_id.reset();
                    commit_node(node);
                }
                for (const auto& candidate : nodes) {
                    if (candidate.id == node.id) continue;
                    const bool selected_parent =
                        node.parent_id.has_value() &&
                        *node.parent_id == candidate.id;
                    if (ImGui::Selectable(candidate.name.c_str(), selected_parent)) {
                        node.parent_id = candidate.id;
                        commit_node(node);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo(
                    "Clip",
                    node_reference_label(node.clip_node_id, "None").c_str())) {
                if (ImGui::Selectable("None", !node.clip_node_id.has_value())) {
                    node.clip_node_id.reset();
                    commit_node(node);
                }
                for (const auto& candidate : nodes) {
                    if (candidate.id == node.id) continue;
                    const bool selected_clip =
                        node.clip_node_id.has_value() &&
                        *node.clip_node_id == candidate.id;
                    if (ImGui::Selectable(candidate.name.c_str(), selected_clip)) {
                        node.clip_node_id = candidate.id;
                        commit_node(node);
                    }
                }
                ImGui::EndCombo();
            }
            float position[]{node.transform.position.x,
                             node.transform.position.y};
            if (ImGui::InputFloat2("Position (world units)", position)) {
                node.transform.position = {position[0], position[1]};
                commit_node(node);
            }
            draw_technical_tooltip("Node translation in world units.");
            float scale[]{node.transform.scale.x, node.transform.scale.y};
            if (ImGui::InputFloat2("Scale (factor)", scale)) {
                node.transform.scale = {scale[0], scale[1]};
                commit_node(node);
            }
            draw_technical_tooltip("Node scale multiplier.");
            float rotation = node.transform.rotation_degrees;
            if (ImGui::InputFloat("Rotation (degrees)", &rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                node.transform.rotation_degrees = rotation;
                commit_node(node);
            }
            draw_technical_tooltip("Node rotation in degrees.");
            float bounds_origin[]{node.shape.bounds.origin.x,
                                  node.shape.bounds.origin.y};
            if (ImGui::InputFloat2("Bounds origin (world units)", bounds_origin)) {
                node.shape.bounds.origin = {bounds_origin[0], bounds_origin[1]};
                commit_node(node);
            }
            draw_technical_tooltip("Shape bounds origin in world units.");
            float bounds_size[]{node.shape.bounds.size.x,
                                node.shape.bounds.size.y};
            if (ImGui::InputFloat2("Bounds size (world units)", bounds_size)) {
                node.shape.bounds.size = {bounds_size[0], bounds_size[1]};
                commit_node(node);
            }
            draw_technical_tooltip("Shape bounds size in world units.");
            const auto shape_label = std::string(
                fabric::project::to_string(node.shape.kind));
            if (ImGui::BeginCombo("Shape", shape_label.c_str())) {
                for (const auto shape_kind : {
                         fabric::project::VectorShapeKind::rectangle,
                         fabric::project::VectorShapeKind::ellipse,
                         fabric::project::VectorShapeKind::line,
                         fabric::project::VectorShapeKind::path}) {
                    const auto option = std::string(
                        fabric::project::to_string(shape_kind));
                    if (ImGui::Selectable(option.c_str(),
                                          node.shape.kind == shape_kind)) {
                        bool can_change = true;
                        if (shape_kind == fabric::project::VectorShapeKind::path &&
                            node.shape.kind != fabric::project::VectorShapeKind::path) {
                            const auto converted =
                                fabric::project::path_commands_from_shape(node.shape);
                            if (!converted) {
                                status = "This primitive cannot be converted to a path.";
                                can_change = false;
                            } else {
                                node.shape.path = *converted;
                            }
                        } else if (shape_kind != fabric::project::VectorShapeKind::path) {
                            node.shape.path.clear();
                        }
                        if (can_change) {
                            node.shape.kind = shape_kind;
                            commit_node(node);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            if (node.shape.kind == fabric::project::VectorShapeKind::line) {
                ImGui::TextDisabled("Line points: %zu", node.shape.points.size());
                if (ImGui::Button("Add line point")) {
                    node.shape.points.push_back(node.shape.points.empty()
                        ? node.shape.bounds.origin
                        : fabric::core::Vec2{node.shape.points.back().x + 1.0F,
                                              node.shape.points.back().y});
                    commit_node(node);
                }
                for (std::size_t point_index = 0;
                     point_index < node.shape.points.size(); ++point_index) {
                    ImGui::PushID(static_cast<int>(point_index));
                    float point[]{node.shape.points[point_index].x,
                                  node.shape.points[point_index].y};
            if (ImGui::InputFloat2("Point (world units)", point)) {
                node.shape.points[point_index] = {point[0], point[1]};
                commit_node(node);
            }
                    draw_technical_tooltip("Polygon vertex in project world units.");
                    ImGui::PopID();
                }
            } else if (node.shape.kind == fabric::project::VectorShapeKind::path) {
                ImGui::TextDisabled("Path commands: %zu", node.shape.path.size());
                if (canvas.selected_path_points.size() > 1U) {
                    ImGui::TextDisabled("Selected points: %zu",
                                       canvas.selected_path_points.size());
                    if (ImGui::Button("Move selection +1")) {
                        if (fabric::project::transform_path_points(
                                node.shape, canvas.selected_path_points,
                                {1.0F, 1.0F}, 0.0F, {1.0F, 1.0F}))
                            commit_node(node);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Rotate selection +15")) {
                        if (fabric::project::transform_path_points(
                                node.shape, canvas.selected_path_points,
                                {}, 15.0F, {1.0F, 1.0F}))
                            commit_node(node);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Scale selection 110%")) {
                        if (fabric::project::transform_path_points(
                                node.shape, canvas.selected_path_points,
                                {}, 0.0F, {1.1F, 1.1F}))
                            commit_node(node);
                    }
                }
                if (ImGui::Button("Add line command")) {
                    const auto point = node.shape.path.empty()
                        ? node.shape.bounds.origin
                        : fabric::core::Vec2{node.shape.path.back().point.x + 1.0F,
                                              node.shape.path.back().point.y};
                    if (fabric::project::insert_path_command(
                            node.shape, node.shape.path.size(),
                            {.kind = fabric::project::VectorPathCommandKind::line,
                             .point = point}))
                        commit_node(node);
                }
                ImGui::SameLine();
                if (ImGui::Button("Add move command") && node.shape.path.empty()) {
                    node.shape.path.push_back({
                        .kind = fabric::project::VectorPathCommandKind::move,
                        .point = node.shape.bounds.origin});
                    commit_node(node);
                }
                ImGui::SameLine();
                if (ImGui::Button("Close contour")) {
                    if (fabric::project::close_path(node.shape)) commit_node(node);
                    else status = "This path cannot be closed.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Open contour")) {
                    if (fabric::project::open_path(node.shape)) commit_node(node);
                    else status = "This path is already open.";
                }
                const std::array<std::pair<fabric::editor::BezierHandleMode,
                                           const char*>, 3> handle_modes{{
                    {fabric::editor::BezierHandleMode::linked, "Linked"},
                    {fabric::editor::BezierHandleMode::symmetric, "Symmetric"},
                    {fabric::editor::BezierHandleMode::free, "Free"}}};
                const auto handle_mode_label = std::ranges::find_if(
                    handle_modes, [&](const auto& mode) {
                        return mode.first == canvas.bezier_handle_mode;
                    });
                if (ImGui::BeginCombo(
                        "Bezier handle mode",
                        handle_mode_label == handle_modes.end()
                            ? "Linked" : handle_mode_label->second)) {
                    for (const auto [mode, label] : handle_modes) {
                        if (ImGui::Selectable(label,
                                              canvas.bezier_handle_mode == mode))
                            canvas.bezier_handle_mode = mode;
                    }
                    ImGui::EndCombo();
                }
                for (std::size_t command_index = 0;
                     command_index < node.shape.path.size(); ++command_index) {
                    auto& command = node.shape.path[command_index];
                    ImGui::PushID(static_cast<int>(command_index));
                    const auto command_label = std::string(
                        fabric::project::to_string(command.kind));
                    if (ImGui::BeginCombo("Command", command_label.c_str())) {
                        for (const auto command_kind : {
                                 fabric::project::VectorPathCommandKind::move,
                                 fabric::project::VectorPathCommandKind::line,
                                 fabric::project::VectorPathCommandKind::cubic,
                                 fabric::project::VectorPathCommandKind::close}) {
                            const auto option = std::string(
                                fabric::project::to_string(command_kind));
                            if (ImGui::Selectable(option.c_str(),
                                                  command.kind == command_kind)) {
                                const bool segment_conversion =
                                    (command.kind == fabric::project::VectorPathCommandKind::line ||
                                     command.kind == fabric::project::VectorPathCommandKind::cubic) &&
                                    (command_kind == fabric::project::VectorPathCommandKind::line ||
                                     command_kind == fabric::project::VectorPathCommandKind::cubic);
                                if (!segment_conversion ||
                                    fabric::project::convert_path_command(
                                        node.shape, command_index, command_kind)) {
                                    command.kind = command_kind;
                                    commit_node(node);
                                } else {
                                    status = "This path segment cannot be converted.";
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    float command_point[]{command.point.x, command.point.y};
                    if (ImGui::InputFloat2("Point (world units)", command_point)) {
                        command.point = {command_point[0], command_point[1]};
                        commit_node(node);
                    }
                    draw_technical_tooltip("Path point in project world units.");
                    if (command.kind == fabric::project::VectorPathCommandKind::cubic) {
                        float control1[]{command.control1.x, command.control1.y};
                        float control2[]{command.control2.x, command.control2.y};
                        if (ImGui::InputFloat2("Bezier handle 1 (world units)", control1)) {
                            if (fabric::editor::update_bezier_handle(
                                    node.shape, command_index, true,
                                    {control1[0], control1[1]},
                                    canvas.bezier_handle_mode))
                                commit_node(node);
                        }
                        draw_technical_tooltip("Incoming cubic handle in project world units.");
                        if (ImGui::InputFloat2("Bezier handle 2 (world units)", control2)) {
                            if (fabric::editor::update_bezier_handle(
                                    node.shape, command_index, false,
                                    {control2[0], control2[1]},
                                    canvas.bezier_handle_mode))
                                commit_node(node);
                        }
                        draw_technical_tooltip("Outgoing cubic handle in project world units.");
                    }
                    if (ImGui::SmallButton("Remove command")) {
                        if (fabric::project::remove_path_command(
                                node.shape, command_index))
                            commit_node(node);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
            }
            const auto fill_label = std::string(
                fabric::project::to_string(node.fill.kind));
            if (ImGui::BeginCombo("Fill type", fill_label.c_str())) {
                const auto first_texture = std::ranges::find_if(
                    session.resources(), [](const auto& resource) {
                        return resource.kind ==
                            fabric::editor::StudioResourceKind::texture;
                    });
                for (const auto fill_kind : {
                         fabric::project::VectorFillKind::none,
                         fabric::project::VectorFillKind::solid,
                         fabric::project::VectorFillKind::image}) {
                    const bool available = fill_kind !=
                            fabric::project::VectorFillKind::image ||
                        first_texture != session.resources().end();
                    ImGui::BeginDisabled(!available);
                    const auto option = std::string(
                        fabric::project::to_string(fill_kind));
                    if (ImGui::Selectable(option.c_str(),
                                          node.fill.kind == fill_kind)) {
                        node.fill.kind = fill_kind;
                        if (fill_kind == fabric::project::VectorFillKind::none) {
                            node.fill.color.reset();
                            node.fill.image.reset();
                        } else if (fill_kind ==
                                   fabric::project::VectorFillKind::solid) {
                            node.fill.color = node.fill.color.value_or(
                                fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F});
                            node.fill.image.reset();
                        } else {
                            node.fill.color.reset();
                            if (!node.fill.image)
                                node.fill.image = fabric::project::VectorImageFill{
                                    .texture = {first_texture->id, "texture"}};
                        }
                        commit_node(node);
                    }
                    ImGui::EndDisabled();
                    draw_disabled_reason(!available,
                                         "Add an indexed texture before choosing an image fill.");
                }
                ImGui::EndCombo();
            }
            if (node.fill.kind == fabric::project::VectorFillKind::solid &&
                node.fill.color) {
                float color[]{node.fill.color->red, node.fill.color->green,
                              node.fill.color->blue, node.fill.color->alpha};
                if (ImGui::ColorEdit4("Fill", color)) {
                    node.fill.color = fabric::core::Color{
                        color[0], color[1], color[2], color[3]};
                    commit_node(node);
                }
            } else if (node.fill.kind ==
                           fabric::project::VectorFillKind::image &&
                       node.fill.image) {
                std::string image_texture_id =
                    node.fill.image->texture.id.value;
                if (draw_project_resource_picker(
                        "Image texture", session.resources(),
                        fabric::editor::StudioResourceKind::texture,
                        image_texture_id, false)) {
                    node.fill.image->texture.id.value = image_texture_id;
                    commit_node(node);
                }
                auto fit = node.fill.image->fit;
                const auto fit_label =
                    std::string(fabric::project::to_string(fit));
                if (ImGui::BeginCombo("Fit", fit_label.c_str())) {
                    for (const auto option : {
                             fabric::project::VectorImageFit::contain,
                             fabric::project::VectorImageFit::cover,
                             fabric::project::VectorImageFit::stretch,
                             fabric::project::VectorImageFit::free}) {
                        const bool selected_fit = option == fit;
                        const auto label =
                            std::string(fabric::project::to_string(option));
                        if (ImGui::Selectable(label.c_str(), selected_fit)) {
                            node.fill.image->fit = option;
                            commit_node(node);
                        }
                    }
                    ImGui::EndCombo();
                }
                float image_offset[]{
                    node.fill.image->transform.position.x,
                    node.fill.image->transform.position.y};
                if (ImGui::InputFloat2("Image offset (world units)", image_offset)) {
                    node.fill.image->transform.position = {
                        image_offset[0], image_offset[1]};
                    commit_node(node);
                }
                ImGui::SetItemTooltip("Translation of the image inside the vector fill.");
                float image_scale[]{node.fill.image->transform.scale.x,
                                    node.fill.image->transform.scale.y};
                if (ImGui::InputFloat2("Image scale (factor)", image_scale)) {
                    node.fill.image->transform.scale = {
                        image_scale[0], image_scale[1]};
                    commit_node(node);
                }
                ImGui::SetItemTooltip("Scale multiplier applied to the image fill.");
                float image_rotation =
                    node.fill.image->transform.rotation_degrees;
                if (ImGui::InputFloat("Image rotation (degrees)", &image_rotation,
                                      1.0F, 10.0F, "%.2f deg")) {
                    node.fill.image->transform.rotation_degrees = image_rotation;
                    commit_node(node);
                }
                ImGui::SetItemTooltip("Rotation of the image fill around its pivot.");
                float image_pivot[]{node.fill.image->transform.pivot.x,
                                    node.fill.image->transform.pivot.y};
                if (ImGui::InputFloat2("Image pivot (world units)", image_pivot)) {
                    node.fill.image->transform.pivot = {
                        image_pivot[0], image_pivot[1]};
                    commit_node(node);
                }
                ImGui::SetItemTooltip("Pivot used by the image fill transform.");
                float opacity = node.fill.image->opacity;
                if (ImGui::SliderFloat("Image opacity (0–1)", &opacity, 0.0F, 1.0F,
                                       "%.2f")) {
                    node.fill.image->opacity = opacity;
                    commit_node(node);
                }
                ImGui::SetItemTooltip("Opacity multiplier applied after image sampling.");
                bool deform = node.fill.image->deform_with_shape;
                if (ImGui::Checkbox("Warp pixels with shape (advanced)",
                                    &deform)) {
                    node.fill.image->deform_with_shape = deform;
                    commit_node(node);
                }
            }
            bool has_stroke = node.stroke.has_value();
            if (ImGui::Checkbox("Stroke", &has_stroke)) {
                if (has_stroke) node.stroke = fabric::project::VectorStroke{};
                else node.stroke.reset();
                commit_node(node);
            }
            if (node.stroke) {
                float stroke_color[]{node.stroke->color.red,
                                     node.stroke->color.green,
                                     node.stroke->color.blue,
                                     node.stroke->color.alpha};
                if (ImGui::ColorEdit4("Stroke color", stroke_color)) {
                    node.stroke->color = {stroke_color[0], stroke_color[1],
                                          stroke_color[2], stroke_color[3]};
                    commit_node(node);
                }
                float stroke_width = node.stroke->width;
                if (ImGui::InputFloat("Stroke width (world units)", &stroke_width,
                                      0.1F, 1.0F)) {
                    node.stroke->width = stroke_width;
                    commit_node(node);
                }
                ImGui::SetItemTooltip("Width of the rendered stroke around the path.");
                const auto join_label = std::string(
                    fabric::project::to_string(node.stroke->join));
                if (ImGui::BeginCombo("Stroke join", join_label.c_str())) {
                    for (const auto join : {
                             fabric::project::VectorStrokeJoin::miter,
                             fabric::project::VectorStrokeJoin::round,
                             fabric::project::VectorStrokeJoin::bevel}) {
                        const auto option = std::string(
                            fabric::project::to_string(join));
                        if (ImGui::Selectable(option.c_str(),
                                              node.stroke->join == join)) {
                            node.stroke->join = join;
                            commit_node(node);
                        }
                    }
                    ImGui::EndCombo();
                }
                const auto cap_label = std::string(
                    fabric::project::to_string(node.stroke->cap));
                if (ImGui::BeginCombo("Stroke cap", cap_label.c_str())) {
                    for (const auto cap : {
                             fabric::project::VectorStrokeCap::butt,
                             fabric::project::VectorStrokeCap::round,
                             fabric::project::VectorStrokeCap::square}) {
                        const auto option = std::string(
                            fabric::project::to_string(cap));
                        if (ImGui::Selectable(option.c_str(),
                                              node.stroke->cap == cap)) {
                            node.stroke->cap = cap;
                            commit_node(node);
                        }
                    }
                    ImGui::EndCombo();
                }
                std::string stroke_texture_id = node.stroke->image
                    ? node.stroke->image->texture.id.value : std::string{};
                if (draw_project_resource_picker(
                        "Stroke image texture", session.resources(),
                        fabric::editor::StudioResourceKind::texture,
                        stroke_texture_id, true)) {
                    if (stroke_texture_id.empty()) {
                        node.stroke->image.reset();
                    } else {
                        node.stroke->image = fabric::project::VectorImageFill{
                            .texture = {{.value = stroke_texture_id}, "texture"}};
                    }
                    commit_node(node);
                }
                if (node.stroke->image) {
                    bool repeat_texture = node.stroke->repeat_texture_x;
                    if (ImGui::Checkbox("Repeat stroke texture horizontally",
                                        &repeat_texture)) {
                        node.stroke->repeat_texture_x = repeat_texture;
                        commit_node(node);
                    }
                    auto& image = *node.stroke->image;
                    float image_offset[]{image.transform.position.x,
                                        image.transform.position.y};
                    if (ImGui::InputFloat2("Stroke image offset (world units)", image_offset)) {
                        image.transform.position = {image_offset[0], image_offset[1]};
                        commit_node(node);
                    }
                    ImGui::SetItemTooltip("Translation of the repeated stroke texture.");
                    float image_scale[]{image.transform.scale.x,
                                       image.transform.scale.y};
                    if (ImGui::InputFloat2("Stroke image scale (factor)", image_scale)) {
                        image.transform.scale = {image_scale[0], image_scale[1]};
                        commit_node(node);
                    }
                    ImGui::SetItemTooltip("Scale multiplier for the stroke texture.");
                    float image_rotation = image.transform.rotation_degrees;
                    if (ImGui::InputFloat("Stroke image rotation (degrees)", &image_rotation,
                                          1.0F, 10.0F, "%.2f deg")) {
                        image.transform.rotation_degrees = image_rotation;
                        commit_node(node);
                    }
                    ImGui::SetItemTooltip("Rotation of the stroke texture around its pivot.");
                    float image_opacity = image.opacity;
                    if (ImGui::SliderFloat("Stroke image opacity (0–1)", &image_opacity,
                                           0.0F, 1.0F, "%.2f")) {
                        image.opacity = image_opacity;
                        commit_node(node);
                    }
                    ImGui::SetItemTooltip("Opacity multiplier applied to the stroke texture.");
                    bool deform = image.deform_with_shape;
                    if (ImGui::Checkbox("Deform stroke image with shape", &deform)) {
                        image.deform_with_shape = deform;
                        commit_node(node);
                    }
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(node.locked,
                                 "Unlock the node to edit its properties.");
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::entity &&
            session.selected_entity()) {
            const auto entity = *session.selected_entity();
            const auto commit_advanced_entity =
                [&](fabric::project::EntityDefinition next) {
                    if (!session.set_selected_entity_definition(std::move(next)))
                        status = "Advanced entity edit rejected; inspect diagnostics.";
                    else
                        status = "Advanced entity section saved.";
                };
            if (ImGui::CollapsingHeader("Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::size_t index = 0; index < entity.constraints.size(); ++index) {
                    auto constraint = entity.constraints[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::InputText("Id", &constraint.id);
                    static_cast<void>(draw_entity_node_picker(
                        "Target node", entity.nodes, constraint.target_node));
                    static_cast<void>(draw_entity_node_picker(
                        "Source node", entity.nodes, constraint.source_node));
                    ImGui::InputInt("Order (index)", &constraint.order);
                    draw_technical_tooltip("Evaluation order for this constraint in the entity.");
                    ImGui::Checkbox("Position", &constraint.constrain_position);
                    ImGui::Checkbox("Rotation", &constraint.constrain_rotation);
                    ImGui::Checkbox("Scale", &constraint.constrain_scale);
                    if (ImGui::Button("Save constraint")) {
                        auto next = entity;
                        next.constraints[index] = std::move(constraint);
                        commit_advanced_entity(std::move(next));
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add copy-transform constraint")) {
                    auto next = entity;
                    next.constraints.push_back({
                        .id = "constraint-" +
                             std::to_string(next.constraints.size() + 1U),
                        .kind = fabric::project::AnimationConstraintKind::copy_transform,
                        .target_node = next.nodes.empty() ? "" : next.nodes.front().id,
                        .source_node = next.nodes.empty() ? "" : next.nodes.front().id});
                    commit_advanced_entity(std::move(next));
                }
                if (entity.constraints.empty())
                    ImGui::TextDisabled("No constraints configured.");
            }
            if (ImGui::CollapsingHeader("IK chains")) {
                for (std::size_t index = 0; index < entity.ik_chains.size(); ++index) {
                    auto chain = entity.ik_chains[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::InputText("Id", &chain.id);
                    static_cast<void>(draw_entity_node_picker(
                        "Target node", entity.nodes, chain.target_node));
                    auto iterations = static_cast<int>(chain.max_iterations);
                    if (ImGui::InputInt("Max iterations (iterations)", &iterations))
                        chain.max_iterations = static_cast<std::size_t>(
                            std::max(1, iterations));
                    draw_technical_tooltip(
                        "Maximum number of solver iterations for this IK chain.");
                    ImGui::InputFloat("Tolerance (world units)", &chain.tolerance);
                    draw_technical_tooltip(
                        "Maximum IK solver error, measured in project world units.");
                    if (ImGui::Button("Save IK chain")) {
                        auto next = entity;
                        next.ik_chains[index] = std::move(chain);
                        commit_advanced_entity(std::move(next));
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add IK chain")) {
                    auto next = entity;
                    next.ik_chains.push_back({
                        .id = "ik-" + std::to_string(next.ik_chains.size() + 1U),
                        .target_node = next.nodes.empty() ? "" : next.nodes.back().id});
                    commit_advanced_entity(std::move(next));
                }
                if (entity.ik_chains.empty())
                    ImGui::TextDisabled("No IK chains configured.");
            }
            if (ImGui::CollapsingHeader("Deformation")) {
                if (entity.deformation_mesh) {
                    auto mesh = *entity.deformation_mesh;
                    ImGui::Text("%zu vertices, %zu triangles", mesh.vertices.size(),
                                mesh.triangles.size());
                    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::Text("Vertex %zu", index);
                        ImGui::InputFloat2("Rest position (world units)", &mesh.vertices[index]
                                                                  .rest_position.x);
                        ImGui::SetItemTooltip("Rest pose position of this deformation vertex in project world units.");
                        if (ImGui::Button("Save vertex")) {
                            auto next = entity;
                            *next.deformation_mesh = std::move(mesh);
                            commit_advanced_entity(std::move(next));
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Remove deformation mesh")) {
                        auto next = entity;
                        next.deformation_mesh.reset();
                        commit_advanced_entity(std::move(next));
                    }
                } else if (ImGui::Button("Create deformation mesh")) {
                    auto next = entity;
                    next.deformation_mesh = fabric::project::DeformationMesh{};
                    commit_advanced_entity(std::move(next));
                }
            }
            if (ImGui::CollapsingHeader("XPBD")) {
                if (entity.xpbd) {
                    auto xpbd = *entity.xpbd;
                    ImGui::Text("%zu particles", xpbd.particles.size());
                    for (std::size_t index = 0; index < xpbd.particles.size(); ++index) {
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::InputFloat2("Position (world units)", &xpbd.particles[index].position.x);
                        ImGui::SetItemTooltip("Current XPBD particle position in project world units.");
                        ImGui::InputFloat("Inverse mass (1/kg)", &xpbd.particles[index].inverse_mass);
                        ImGui::SetItemTooltip("Inverse particle mass; zero makes the particle static.");
                        if (ImGui::Button("Save particle")) {
                            auto next = entity;
                            *next.xpbd = std::move(xpbd);
                            commit_advanced_entity(std::move(next));
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Add particle")) {
                        xpbd.particles.push_back({});
                        auto next = entity;
                        next.xpbd = std::move(xpbd);
                        commit_advanced_entity(std::move(next));
                    }
                    if (ImGui::Button("Remove XPBD system")) {
                        auto next = entity;
                        next.xpbd.reset();
                        commit_advanced_entity(std::move(next));
                    }
                } else if (ImGui::Button("Create XPBD system")) {
                    auto next = entity;
                    next.xpbd = fabric::project::XpbdSystem{};
                    commit_advanced_entity(std::move(next));
                }
            }
            if (ImGui::CollapsingHeader("Animation state machine")) {
                if (entity.animation_state_machine) {
                    auto machine = *entity.animation_state_machine;
                    const auto initial_state = std::ranges::find_if(
                        machine.states, [&](const auto& state) {
                            return state.id == machine.initial_state;
                        });
                    const std::string initial_state_label =
                        initial_state == machine.states.end()
                        ? machine.initial_state.empty()
                            ? std::string{"Choose an initial state..."}
                            : std::string{"Missing: "} + machine.initial_state
                        : initial_state->id;
                    if (ImGui::BeginCombo("Initial state", initial_state_label.c_str())) {
                        if (ImGui::Selectable("No initial state", machine.initial_state.empty()))
                            machine.initial_state.clear();
                        for (const auto& state : machine.states) {
                            const bool selected = state.id == machine.initial_state;
                            if (ImGui::Selectable(state.id.c_str(), selected))
                                machine.initial_state = state.id;
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        if (initial_state == machine.states.end() && !machine.initial_state.empty())
                            ImGui::TextDisabled("Missing state: %s", machine.initial_state.c_str());
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip("Select an existing state-machine state; new state IDs are authored below.");
                    for (std::size_t index = 0; index < machine.states.size(); ++index) {
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::InputText("State id", &machine.states[index].id);
                        if (ImGui::Button("Save state")) {
                            auto next = entity;
                            *next.animation_state_machine = std::move(machine);
                            commit_advanced_entity(std::move(next));
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Add state")) {
                        machine.states.push_back({
                            .id = "state-" + std::to_string(machine.states.size() + 1U),
                            .clip = {{.value = ""}, "animation"}});
                        auto next = entity;
                        *next.animation_state_machine = std::move(machine);
                        commit_advanced_entity(std::move(next));
                    }
                    if (ImGui::Button("Remove state machine")) {
                        auto next = entity;
                        next.animation_state_machine.reset();
                        commit_advanced_entity(std::move(next));
                    }
                } else if (ImGui::Button("Create state machine")) {
                    auto next = entity;
                    next.animation_state_machine =
                        fabric::project::AnimationStateMachine{};
                    commit_advanced_entity(std::move(next));
                }
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::entity &&
            session.selected_entity()) {
            const auto entity = *session.selected_entity();
            const auto drawable_from_payload =
                [&](const ResourceDragPayload& payload)
                    -> std::optional<std::pair<fabric::project::EntityDrawableKind,
                                                const char*>> {
                    switch (static_cast<fabric::editor::StudioResourceKind>(payload.kind)) {
                    case fabric::editor::StudioResourceKind::texture:
                        return std::pair{fabric::project::EntityDrawableKind::texture,
                                          "texture"};
                    case fabric::editor::StudioResourceKind::vector:
                        return std::pair{fabric::project::EntityDrawableKind::vector,
                                          "vector"};
                    case fabric::editor::StudioResourceKind::visual_component:
                        return std::pair{fabric::project::EntityDrawableKind::visual_component,
                                          "visualComponent"};
                    default:
                        return std::nullopt;
                    }
                };
            const auto apply_resource_to_node =
                [&](const std::size_t node_index,
                    const ResourceDragPayload& payload) {
                    if (node_index >= session.selected_entity()->nodes.size())
                        return false;
                    const auto drawable = drawable_from_payload(payload);
                    if (!drawable) return false;
                    auto changed = session.selected_entity()->nodes[node_index];
                    if (changed.drawable.component_instance &&
                        !changed.drawable.component_instance->overrides.empty())
                        return false;
                    changed.drawable.kind = drawable->first;
                    changed.drawable.resource =
                        fabric::project::ResourceReference{
                            {.value = payload.id}, drawable->second};
                    changed.drawable.material.reset();
                    changed.drawable.component_instance.reset();
                    if (drawable->first ==
                        fabric::project::EntityDrawableKind::visual_component)
                        changed.drawable.component_instance =
                            fabric::project::VisualComponentInstance{};
                    return session.set_selected_entity_node(
                        node_index, std::move(changed));
                };
            const auto add_dropped_node =
                [&](const std::optional<std::string>& parent,
                    const ResourceDragPayload& payload) {
                    const auto drawable = drawable_from_payload(payload);
                    if (!drawable) return false;
                    fabric::project::EntityNode added{
                        .id = "node-" + std::to_string(entity.nodes.size() + 1U),
                        .name = "Node " + std::to_string(entity.nodes.size() + 1U),
                        .parent = parent};
                    while (std::ranges::any_of(
                        entity.nodes, [&](const auto& candidate) {
                            return candidate.id == added.id;
                        }))
                        added.id += "-copy";
                    added.drawable.kind = drawable->first;
                    added.drawable.resource =
                        fabric::project::ResourceReference{
                            {.value = payload.id}, drawable->second};
                    if (drawable->first ==
                        fabric::project::EntityDrawableKind::visual_component)
                        added.drawable.component_instance =
                            fabric::project::VisualComponentInstance{};
                    const auto added_ok = session.add_selected_entity_node(
                        std::move(added));
                    if (added_ok) canvas.selected_node = entity.nodes.size();
                    return added_ok;
                };
            ImGui::SeparatorText("Entity behavior");
            if (ImGui::Button("Add animation clip...")) {
                creation.request_animation = true;
                status = "Create a clip targeted at this entity.";
            }
            std::string behavior_id = entity.behavior
                ? entity.behavior->id.value : std::string{};
            if (draw_project_resource_picker(
                    "Behavior", session.resources(),
                    fabric::editor::StudioResourceKind::behavior,
                    behavior_id, true)) {
                const auto reference = behavior_id.empty()
                    ? std::optional<fabric::project::ResourceReference>{}
                    : std::optional<fabric::project::ResourceReference>{
                        fabric::project::ResourceReference{
                            {.value = behavior_id}, "behavior"}};
                status = session.set_selected_entity_behavior(reference)
                    ? "Entity behavior changed."
                    : "Behavior attachment rejected; inspect diagnostics.";
            }
            ImGui::SeparatorText("Entity hierarchy");
            if (!entity.nodes.empty()) {
                canvas.selected_node = std::min(canvas.selected_node,
                                                entity.nodes.size() - 1);
            }
            if (entity.nodes.empty()) {
                if (ImGui::Button("Add root node")) {
                    fabric::project::EntityNode new_node{
                        .id = "node-1", .name = "Node 1"};
                    if (session.add_selected_entity_node(std::move(new_node))) {
                        canvas.selected_node = 0U;
                        status = "Entity root node added.";
                    } else {
                        status = "Entity node rejected; inspect diagnostics.";
                    }
                }
                ImGui::SameLine();
                ImGui::Button("Drop artwork as root");
                ImGui::SameLine();
                ImGui::TextDisabled("Textures, vectors or visual components");
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(
                            "VERTEX_LOOM_RESOURCE");
                        payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                        if (add_dropped_node(std::nullopt,
                                             *static_cast<const ResourceDragPayload*>(
                                                 payload->Data)))
                            status = "Artwork dropped on new root node.";
                        else
                            status = "Artwork kind cannot be used on an entity node.";
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::TextDisabled("This entity has no nodes.");
            } else {
            if (ImGui::Button("Add child")) {
                const auto& parent = entity.nodes[canvas.selected_node];
                fabric::project::EntityNode new_node{
                    .id = "node-" + std::to_string(entity.nodes.size() + 1U),
                    .name = "Node " + std::to_string(entity.nodes.size() + 1U),
                    .parent = parent.id};
                while (std::ranges::any_of(
                    entity.nodes, [&](const auto& candidate) {
                        return candidate.id == new_node.id;
                    })) {
                    new_node.id += "-copy";
                }
                if (session.add_selected_entity_node(std::move(new_node))) {
                    canvas.selected_node = entity.nodes.size();
                    status = "Entity child added.";
                } else {
                    status = "Entity node rejected; inspect diagnostics.";
                }
            }
            ImGui::SameLine();
            ImGui::Button("Drop artwork as child");
            ImGui::SameLine();
            ImGui::TextDisabled("Textures, vectors or visual components");
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(
                        "VERTEX_LOOM_RESOURCE");
                    payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                    if (add_dropped_node(entity.nodes[canvas.selected_node].id,
                                         *static_cast<const ResourceDragPayload*>(
                                             payload->Data)))
                        status = "Artwork dropped on new child node.";
                    else
                        status = "Artwork kind cannot be used on an entity node.";
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate")) {
                if (session.duplicate_selected_entity_node(canvas.selected_node)) {
                    canvas.selected_node = entity.nodes.size();
                    status = "Entity node duplicated.";
                } else {
                    status = "Entity node rejected; inspect diagnostics.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Move up") && canvas.selected_node > 0U &&
                session.move_selected_entity_node(
                    canvas.selected_node, canvas.selected_node - 1U)) {
                --canvas.selected_node;
                status = "Entity node moved.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Move down") &&
                canvas.selected_node + 1U < entity.nodes.size() &&
                session.move_selected_entity_node(
                    canvas.selected_node, canvas.selected_node + 1U)) {
                ++canvas.selected_node;
                status = "Entity node moved.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete..."))
                ImGui::OpenPopup("Delete entity node?");
            if (ImGui::BeginPopupModal("Delete entity node?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto& pending = entity.nodes[canvas.selected_node];
                const auto child_count = std::ranges::count_if(
                    entity.nodes, [&](const auto& candidate) {
                        return candidate.parent && *candidate.parent == pending.id;
                    });
                ImGui::Text("Delete '%s'?", pending.name.c_str());
                ImGui::TextWrapped("This node has %zu direct child(ren). Nodes with children are protected until they are reparented.",
                                   child_count);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4{0.62F, 0.16F, 0.14F, 1.0F});
                if (ImGui::Button("Delete node") &&
                    session.remove_selected_entity_node(canvas.selected_node)) {
                    canvas.selected_node = canvas.selected_node == 0U
                        ? 0U : canvas.selected_node - 1U;
                    status = "Entity node deleted.";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            for (std::size_t node_index = 0; node_index < entity.nodes.size();
                 ++node_index) {
                ImGui::PushID(entity.nodes[node_index].id.c_str());
                const auto label = entity.nodes[node_index].name + "##entity-node-" +
                    entity.nodes[node_index].id;
                if (ImGui::Selectable(label.c_str(),
                                      canvas.selected_node == node_index)) {
                    canvas.selected_node = node_index;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(
                            "VERTEX_LOOM_RESOURCE");
                        payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                        if (apply_resource_to_node(
                                node_index,
                                *static_cast<const ResourceDragPayload*>(payload->Data))) {
                            canvas.selected_node = node_index;
                            status = "Artwork dropped on existing node.";
                        } else if (session.selected_entity()->nodes[node_index]
                                       .drawable.component_instance &&
                                   !session.selected_entity()
                                        ->nodes[node_index]
                                        .drawable.component_instance->overrides.empty()) {
                            status = "Drop rejected: confirm incompatible overrides first.";
                        } else {
                            status = "Artwork kind cannot be used on an entity node.";
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
            auto node = entity.nodes[canvas.selected_node];
            const auto commit_entity_node =
                [&](fabric::project::EntityNode changed) {
                    if (session.set_selected_entity_node(
                            canvas.selected_node, std::move(changed))) {
                        status = "Entity node changed.";
                    } else {
                        status = "Entity change rejected; inspect diagnostics.";
                    }
                };
            ImGui::SeparatorText("Entity node properties");
            bool locked = node.locked;
            if (ImGui::Checkbox("Locked", &locked)) {
                node.locked = locked;
                commit_entity_node(node);
            }
            ImGui::BeginDisabled(node.locked);
            bool visible = node.visible;
            if (ImGui::Checkbox("Visible", &visible)) {
                node.visible = visible;
                commit_entity_node(node);
            }
            std::string node_name = node.name;
            if (draw_resource_name_field("Node name", node_name, 360.0F)) {
                node.name = std::move(node_name);
                commit_entity_node(node);
            }
            const auto parent_label = [&](const std::optional<std::string>& parent) {
                if (!parent) return std::string{"None"};
                for (const auto& candidate : entity.nodes) {
                    if (candidate.id == *parent) return candidate.name;
                }
                return std::string{"Missing: "} + *parent;
            };
            if (ImGui::BeginCombo("Parent", parent_label(node.parent).c_str())) {
                if (ImGui::Selectable("None", !node.parent.has_value())) {
                    node.parent.reset();
                    commit_entity_node(node);
                }
                for (const auto& candidate : entity.nodes) {
                    if (candidate.id == node.id) continue;
                    const bool selected_parent = node.parent.has_value() &&
                        *node.parent == candidate.id;
                    if (ImGui::Selectable(candidate.name.c_str(), selected_parent)) {
                        node.parent = candidate.id;
                        commit_entity_node(node);
                    }
                }
                ImGui::EndCombo();
            }
            float position[]{node.transform.position.x, node.transform.position.y};
            if (ImGui::InputFloat2("Entity position (world units)", position)) {
                node.transform.position = {position[0], position[1]};
                commit_entity_node(node);
            }
            draw_technical_tooltip("Entity node translation in project world units.");
            float scale[]{node.transform.scale.x, node.transform.scale.y};
            if (ImGui::InputFloat2("Entity scale (factor)", scale)) {
                node.transform.scale = {scale[0], scale[1]};
                commit_entity_node(node);
            }
            draw_technical_tooltip("Entity node scale multiplier.");
            float pivot[]{node.transform.pivot.x, node.transform.pivot.y};
            if (ImGui::InputFloat2("Entity pivot (world units)", pivot)) {
                node.transform.pivot = {pivot[0], pivot[1]};
                commit_entity_node(node);
            }
            draw_technical_tooltip("Entity node pivot in project world units.");
            float rotation = node.transform.rotation_degrees;
            if (ImGui::InputFloat("Entity rotation (degrees)", &rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                node.transform.rotation_degrees = rotation;
                commit_entity_node(node);
            }
            draw_technical_tooltip("Entity node rotation around its pivot, in degrees.");
            float z_order = node.z_order;
            if (ImGui::InputFloat("Z order (world units)", &z_order, 0.1F, 1.0F, "%.2f")) {
                node.z_order = z_order;
                commit_entity_node(node);
            }
            draw_technical_tooltip("Draw order; larger values render later.");
            ImGui::SeparatorText("Drawable");
            const auto drawable_label = std::string(
                fabric::project::to_string(node.drawable.kind));
            const auto apply_drawable_kind =
                [&](fabric::project::EntityNode& changed,
                    const fabric::project::EntityDrawableKind kind) {
                    changed.drawable.kind = kind;
                    if (kind == fabric::project::EntityDrawableKind::none) {
                        changed.drawable.resource.reset();
                        changed.drawable.material.reset();
                        changed.drawable.component_instance.reset();
                        return true;
                    }
                    const auto resource_kind = kind ==
                            fabric::project::EntityDrawableKind::texture
                        ? fabric::editor::StudioResourceKind::texture
                        : kind == fabric::project::EntityDrawableKind::vector
                        ? fabric::editor::StudioResourceKind::vector
                        : fabric::editor::StudioResourceKind::visual_component;
                    const auto first = std::ranges::find_if(
                        session.resources(), [&](const auto& resource) {
                            return resource.kind == resource_kind;
                        });
                    if (first == session.resources().end()) return false;
                    const char* expected = kind ==
                            fabric::project::EntityDrawableKind::texture
                        ? "texture"
                        : kind == fabric::project::EntityDrawableKind::vector
                        ? "vector" : "visualComponent";
                    if (!changed.drawable.resource ||
                        changed.drawable.resource->expected_type != expected)
                        changed.drawable.resource =
                            fabric::project::ResourceReference{first->id, expected};
                    if (kind == fabric::project::EntityDrawableKind::visual_component) {
                        changed.drawable.material.reset();
                        if (!changed.drawable.component_instance)
                            changed.drawable.component_instance =
                                fabric::project::VisualComponentInstance{};
                    } else {
                        changed.drawable.component_instance.reset();
                    }
                    return true;
                };
            if (ImGui::BeginCombo("Kind", drawable_label.c_str())) {
                for (const auto kind : {
                         fabric::project::EntityDrawableKind::none,
                         fabric::project::EntityDrawableKind::texture,
                         fabric::project::EntityDrawableKind::vector,
                         fabric::project::EntityDrawableKind::visual_component}) {
                    const auto label = std::string(fabric::project::to_string(kind));
                    const auto resource_kind = kind ==
                            fabric::project::EntityDrawableKind::texture
                        ? fabric::editor::StudioResourceKind::texture
                        : kind == fabric::project::EntityDrawableKind::vector
                        ? fabric::editor::StudioResourceKind::vector
                        : fabric::editor::StudioResourceKind::visual_component;
                    const auto first = std::ranges::find_if(
                        session.resources(), [&](const auto& resource) {
                            return resource.kind == resource_kind;
                        });
                    const bool available = kind ==
                            fabric::project::EntityDrawableKind::none ||
                        first != session.resources().end();
                    ImGui::BeginDisabled(!available);
                    if (ImGui::Selectable(label.c_str(),
                                          node.drawable.kind == kind)) {
                        const auto has_overrides =
                            node.drawable.component_instance &&
                            !node.drawable.component_instance->overrides.empty();
                        if (has_overrides &&
                            kind != fabric::project::EntityDrawableKind::visual_component) {
                            pending_drawable_kind =
                                std::pair{canvas.selected_node, kind};
                            ImGui::OpenPopup("Discard incompatible overrides?");
                        } else {
                            if (!apply_drawable_kind(node, kind))
                                status = "Drawable kind unavailable; inspect diagnostics.";
                            else
                                commit_entity_node(node);
                        }
                    }
                    ImGui::EndDisabled();
                    draw_disabled_reason(!available,
                                         "Add an indexed resource of this drawable kind first.");
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginPopupModal("Discard incompatible overrides?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                const auto valid = pending_drawable_kind.has_value() &&
                    pending_drawable_kind->first < entity.nodes.size();
                std::size_t override_count = 0U;
                if (valid) {
                    const auto& pending_node =
                        entity.nodes[pending_drawable_kind->first];
                    if (pending_node.drawable.component_instance)
                        override_count = pending_node.drawable.component_instance->overrides.size();
                    ImGui::Text("Change drawable kind and discard %zu override(s)?",
                                override_count);
                    ImGui::TextDisabled(
                        "Overrides belong to the current visual component and cannot be applied to the new kind.");
                }
                ImGui::BeginDisabled(!valid);
                if (ImGui::Button("Discard overrides and change")) {
                    auto changed = entity.nodes[pending_drawable_kind->first];
                    if (apply_drawable_kind(changed, pending_drawable_kind->second) &&
                        session.set_selected_entity_node(
                            pending_drawable_kind->first, std::move(changed))) {
                        status = "Drawable changed; incompatible overrides discarded.";
                        pending_drawable_kind.reset();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndDisabled();
                draw_disabled_reason(!valid,
                                     "Select a valid drawable kind before discarding overrides.");
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    pending_drawable_kind.reset();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (node.drawable.kind !=
                fabric::project::EntityDrawableKind::none) {
                const auto resource_kind = node.drawable.kind ==
                        fabric::project::EntityDrawableKind::texture
                    ? fabric::editor::StudioResourceKind::texture
                    : node.drawable.kind ==
                            fabric::project::EntityDrawableKind::vector
                    ? fabric::editor::StudioResourceKind::vector
                    : fabric::editor::StudioResourceKind::visual_component;
                const char* expected = node.drawable.kind ==
                        fabric::project::EntityDrawableKind::texture
                    ? "texture"
                    : node.drawable.kind ==
                            fabric::project::EntityDrawableKind::vector
                    ? "vector" : "visualComponent";
                std::string artwork_id = node.drawable.resource
                    ? node.drawable.resource->id.value : std::string{};
                if (draw_project_resource_picker(
                        "Artwork", session.resources(), resource_kind,
                        artwork_id, false)) {
                    node.drawable.resource = fabric::project::ResourceReference{
                        {.value = artwork_id}, expected};
                    if (node.drawable.kind ==
                        fabric::project::EntityDrawableKind::visual_component)
                        node.drawable.component_instance =
                            fabric::project::VisualComponentInstance{};
                    commit_entity_node(node);
                }
                const auto artwork = std::ranges::find_if(
                    session.resources(), [&](const auto& resource) {
                        return resource.kind == resource_kind &&
                            resource.id.value == artwork_id;
                    });
                ImGui::BeginDisabled(artwork == session.resources().end());
                if (ImGui::Button("Open artwork") &&
                    artwork != session.resources().end())
                    select_and_preview_resource(
                        session, *artwork, preview, status, "Opened artwork: ");
                ImGui::EndDisabled();
                draw_disabled_reason(artwork == session.resources().end(),
                                     "Choose an existing artwork resource first.");
                ImGui::SameLine();
                if (ImGui::Button("Clear drawable")) {
                    node.drawable = {};
                    commit_entity_node(node);
                }
            }
            if (node.drawable.kind ==
                    fabric::project::EntityDrawableKind::texture ||
                node.drawable.kind ==
                    fabric::project::EntityDrawableKind::vector) {
                std::string material_id = node.drawable.material
                    ? node.drawable.material->id.value : std::string{};
                if (draw_project_resource_picker(
                        "Material", session.resources(),
                        fabric::editor::StudioResourceKind::material,
                        material_id, true)) {
                    node.drawable.material = material_id.empty()
                        ? std::optional<fabric::project::ResourceReference>{}
                        : std::optional<fabric::project::ResourceReference>{
                            fabric::project::ResourceReference{
                                {.value = material_id}, "material"}};
                    commit_entity_node(node);
                }
            }
            if (node.drawable.kind ==
                    fabric::project::EntityDrawableKind::visual_component &&
                node.drawable.resource) {
                const auto component = fabric::project::load_visual_component(
                    session.project_root(), *session.manifest(),
                    fabric::project::visual_component_document_path(
                        *session.manifest(), node.drawable.resource->id));
                if (component.ok()) {
                    auto instance = node.drawable.component_instance.value_or(
                        fabric::project::VisualComponentInstance{});
                    const auto variant_name = [&] {
                        if (!instance.variant_id) return std::string{"Default"};
                        const auto found = std::ranges::find(
                            component.asset->variants, *instance.variant_id,
                            &fabric::project::VisualComponentVariant::id);
                        return found == component.asset->variants.end()
                            ? std::string{"Missing: "} + *instance.variant_id
                            : found->name;
                    }();
                    if (ImGui::BeginCombo("Variant", variant_name.c_str())) {
                        if (ImGui::Selectable("Default", !instance.variant_id)) {
                            instance.variant_id.reset();
                            node.drawable.component_instance = instance;
                            commit_entity_node(node);
                        }
                        for (const auto& variant : component.asset->variants) {
                            if (ImGui::Selectable(variant.name.c_str(),
                                    instance.variant_id == variant.id)) {
                                instance.variant_id = variant.id;
                                node.drawable.component_instance = instance;
                                commit_entity_node(node);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    const auto anchor_name = [&] {
                        if (!instance.anchor_id) return std::string{"Default"};
                        const auto found = std::ranges::find(
                            component.asset->anchors, *instance.anchor_id,
                            &fabric::project::VisualComponentAnchor::id);
                        return found == component.asset->anchors.end()
                            ? std::string{"Missing: "} + *instance.anchor_id
                            : found->name;
                    }();
                    if (ImGui::BeginCombo("Anchor", anchor_name.c_str())) {
                        if (ImGui::Selectable("Default", !instance.anchor_id)) {
                            instance.anchor_id.reset();
                            node.drawable.component_instance = instance;
                            commit_entity_node(node);
                        }
                        for (const auto& anchor : component.asset->anchors) {
                            if (ImGui::Selectable(anchor.name.c_str(),
                                    instance.anchor_id == anchor.id)) {
                                instance.anchor_id = anchor.id;
                                node.drawable.component_instance = instance;
                                commit_entity_node(node);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::Text("%zu override(s)", instance.overrides.size());
                    for (std::size_t override_index = 0;
                         override_index < instance.overrides.size();
                         ++override_index) {
                        ImGui::PushID(static_cast<int>(override_index));
                        ImGui::TextUnformatted(
                            instance.overrides[override_index].parameter_id.c_str());
                        auto override = instance.overrides[override_index];
                        bool override_changed = false;
                        if (auto* value = std::get_if<float>(&override.value))
                            override_changed = ImGui::InputFloat("Value", value);
                        else if (auto* value = std::get_if<std::int64_t>(
                                     &override.value))
                            override_changed = ImGui::InputScalar(
                                "Value", ImGuiDataType_S64, value);
                        else if (auto* value = std::get_if<bool>(&override.value))
                            override_changed = ImGui::Checkbox("Value", value);
                        else if (auto* value = std::get_if<std::string>(
                                     &override.value))
                            override_changed = ImGui::InputText("Value", value);
                        else if (auto* value = std::get_if<fabric::core::Vec2>(
                                     &override.value))
                            override_changed = ImGui::InputFloat2(
                                "Value", &value->x);
                        else if (auto* value = std::get_if<fabric::core::Color>(
                                     &override.value))
                            override_changed = ImGui::ColorEdit4(
                                "Value", &value->red);
                        else if (auto* value = std::get_if<
                                     fabric::project::ResourceReference>(
                                     &override.value)) {
                            if (const auto kind = resource_kind_for_contract(
                                    value->expected_type)) {
                                auto id = value->id.value;
                                if (draw_project_resource_picker(
                                        "Value", session.resources(), *kind, id, false)) {
                                    value->id = {.value = id};
                                    override_changed = true;
                                }
                            } else {
                                ImGui::TextDisabled(
                                    "Unsupported resource contract: %s",
                                    value->expected_type.c_str());
                            }
                        }
                        if (override_changed) {
                            instance.overrides[override_index] = std::move(override);
                            node.drawable.component_instance = instance;
                            commit_entity_node(node);
                        }
                        if (ImGui::SmallButton("Remove override")) {
                            instance.overrides.erase(
                                instance.overrides.begin() +
                                static_cast<std::ptrdiff_t>(override_index));
                            node.drawable.component_instance = instance;
                            commit_entity_node(node);
                            ImGui::PopID();
                            break;
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::BeginCombo("Add override", "Choose parameter...")) {
                        for (const auto& parameter : component.asset->parameters) {
                            const bool exists = std::ranges::any_of(
                                instance.overrides, [&](const auto& value) {
                                    return value.parameter_id == parameter.id;
                                });
                            ImGui::BeginDisabled(exists);
                            if (ImGui::Selectable(parameter.name.c_str())) {
                                instance.overrides.push_back(
                                    {parameter.id, parameter.default_value});
                                node.drawable.component_instance = instance;
                                commit_entity_node(node);
                            }
                            ImGui::EndDisabled();
                            draw_disabled_reason(exists,
                                                 "This component parameter already has an override.");
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(node.locked,
                                 "Unlock the node to edit its properties.");
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::audio) {
            ImGui::SeparatorText("Audio events");
            const auto loaded = fabric::project::load_audio(
                session.project_root(), *session.manifest(), selected->document_path);
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors)
                    ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s: %s",
                                       error.field.c_str(), error.message.c_str());
            } else {
                for (std::size_t event_index = 0; event_index < loaded.audio->events.size(); ++event_index) {
                    auto event = loaded.audio->events[event_index];
                    ImGui::PushID(static_cast<int>(event_index));
                    ImGui::InputText("Event id", &event.id);
                    ImGui::InputText("Source", &event.source);
                    ImGui::SliderFloat("Volume (0–1)", &event.volume, 0.0F, 1.0F);
                    draw_technical_tooltip(
                        "Playback gain for this audio event; 1.0 is the source level.");
                    ImGui::Checkbox("Loop", &event.loop);
                    draw_technical_tooltip(
                        "Restart this event automatically when playback reaches its end.");
                    if (ImGui::Button("Save event") &&
                        !session.set_selected_audio_event(event_index, std::move(event)))
                        status = "Audio event rejected; inspect diagnostics.";
                    ImGui::PopID();
                }
                if (loaded.audio->events.empty())
                    ImGui::TextDisabled("No audio events defined.");
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::input &&
            session.selected_input()) {
            const auto& input = *session.selected_input();
            ImGui::SeparatorText("Input bindings");
            for (std::size_t action_index = 0;
                 action_index < input.actions.size(); ++action_index) {
                const auto& action = input.actions[action_index];
                ImGui::PushID(static_cast<int>(action_index));
                if (const auto consumers = session.behavior_consumers(action.id)) {
                    ImGui::TextDisabled("BehaviorGraph consumers: %zu",
                                       consumers->size());
                    for (const auto& consumer : *consumers)
                        ImGui::BulletText("%s (%s)", consumer.name.c_str(),
                                         consumer.id.value.c_str());
                }
                bool action_removed = false;
                std::string action_id = action.id;
                if (ImGui::InputText("Action id", &action_id) &&
                    action_id != action.id &&
                    !session.set_selected_input_action_id(action_index, action_id)) {
                    status = "Input action rejected; inspect diagnostics.";
                }
                for (std::size_t binding_index = 0;
                     binding_index < action.bindings.size(); ++binding_index) {
                    const auto& binding = action.bindings[binding_index];
                    ImGui::PushID(static_cast<int>(binding_index));
                    int device = static_cast<int>(binding.device);
                    int code = binding.code;
                    auto edited_binding = binding;
                    const char* devices[] = {"keyboard", "gamepad"};
                    bool changed = false;
                    ImGui::SetNextItemWidth(130.0F);
                    if (ImGui::Combo("Device", &device, devices, 2)) changed = true;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(130.0F);
                    if (ImGui::InputInt("Code (platform)", &code)) changed = true;
                    draw_technical_tooltip("Keyboard or gamepad code used by this binding.");
                    edited_binding.device = static_cast<fabric::project::InputDevice>(device);
                    edited_binding.code = code;
                    int binding_kind = static_cast<int>(binding.kind);
                    const char* binding_kinds[] = {"button", "axis"};
                    if (ImGui::Combo("Kind", &binding_kind, binding_kinds, 2)) changed = true;
                    edited_binding.kind = static_cast<fabric::project::InputBindingKind>(binding_kind);
                    if (edited_binding.kind == fabric::project::InputBindingKind::axis) {
                        if (ImGui::InputFloat("Threshold (normalized)", &edited_binding.threshold, 0.05F, 0.1F, "%.2f")) changed = true;
                        draw_technical_tooltip("Activation threshold for the analog binding.");
                        if (ImGui::InputFloat("Dead zone (normalized)", &edited_binding.dead_zone, 0.05F, 0.1F, "%.2f")) changed = true;
                        draw_technical_tooltip("Input range ignored around the analog neutral point.");
                    }
                    if (ImGui::Checkbox("Ctrl", &edited_binding.ctrl)) changed = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Shift", &edited_binding.shift)) changed = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Alt", &edited_binding.alt)) changed = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Super", &edited_binding.super)) changed = true;
                    if (changed && !session.set_selected_input_binding(
                            action_index, binding_index,
                            edited_binding)) {
                        status = "Input binding rejected; inspect diagnostics.";
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", input_binding_label(binding).c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Capture next")) {
                        creation.input_capture = true;
                        creation.input_capture_existing = true;
                        creation.input_capture_action = action_index;
                        creation.input_capture_binding = binding_index;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove binding")) {
                        action_removed = session.remove_selected_input_binding(
                            action_index, binding_index);
                        status = action_removed
                            ? "Input binding removed."
                            : "Input binding could not be removed; inspect diagnostics.";
                    }
                    ImGui::PopID();
                    if (action_removed) break;
                }
                if (!action_removed && ImGui::Button("Add binding") &&
                    !session.add_selected_input_binding(
                        action_index, {fabric::project::InputDevice::keyboard, 0})) {
                    status = "Input binding could not be added; inspect diagnostics.";
                }
                ImGui::SameLine();
                if (!action_removed && ImGui::SmallButton("Remove action")) {
                    action_removed = session.remove_selected_input_action(action_index);
                    status = action_removed
                        ? "Input action removed."
                        : "Input action could not be removed; inspect diagnostics.";
                }
                ImGui::PopID();
                if (action_removed) break;
            }
            if (input.actions.empty())
                ImGui::TextDisabled("No actions defined.");
            if (ImGui::Button("Add action") &&
                !session.add_selected_input_action({"action", {}})) {
                status = "Input action could not be added; inspect diagnostics.";
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::animation &&
            session.selected_animation()) {
            auto& clip = *session.selected_animation();
            ImGui::SeparatorText("Animation timeline");
            std::string preview_entity_id = clip.preview_entity
                ? clip.preview_entity->id.value : std::string{};
            if (draw_project_resource_picker(
                    "Preview entity (empty = generic)", session.resources(),
                    fabric::editor::StudioResourceKind::entity,
                    preview_entity_id, true)) {
                const auto target = preview_entity_id.empty()
                    ? std::optional<fabric::project::ResourceReference>{}
                    : std::optional<fabric::project::ResourceReference>{
                        fabric::project::ResourceReference{
                            {.value = preview_entity_id}, "entity"}};
                status = session.set_selected_animation_preview_entity(target)
                    ? "Animation preview target changed."
                    : "Animation target rejected; inspect diagnostics.";
            }
            if (!clip.preview_entity)
                ImGui::TextDisabled("Generic clip: no entity preview.");
            float duration = clip.duration;
            if (ImGui::InputFloat("Duration (seconds)", &duration, 0.1F, 1.0F, "%.2f s")) {
                if (!session.set_selected_animation_duration(duration)) {
                    status = "Animation duration rejected; inspect diagnostics.";
                }
            }
            ImGui::SetItemTooltip("Total clip duration; key times are constrained to this range.");
            bool loop = clip.loop;
            if (ImGui::Checkbox("Loop", &loop)) {
                if (!session.set_selected_animation_loop(loop)) {
                    status = "Animation loop rejected; inspect diagnostics.";
                }
            }
            animation_ui.scrub_time = std::clamp(animation_ui.scrub_time, 0.0F,
                                                  std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Scrub (seconds)", &animation_ui.scrub_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            ImGui::SetItemTooltip("Preview time used to evaluate the animation clip.");
            ImGui::Checkbox("Auto-key at scrub time", &animation_ui.auto_key);
            ImGui::Checkbox("Snap key times", &animation_ui.snap_keys);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0F);
            ImGui::InputFloat("Snap interval (seconds)", &animation_ui.key_snap_interval,
                              0.05F, 0.5F, "%.2f s");
            draw_technical_tooltip(
                "Key times are rounded to this interval when snapping is enabled.");
            animation_ui.key_snap_interval = std::max(0.01F,
                                                      animation_ui.key_snap_interval);
            const auto snap_key_time = [&](const float time) {
                if (!animation_ui.snap_keys) return time;
                return std::round(time / animation_ui.key_snap_interval) *
                    animation_ui.key_snap_interval;
            };
            const auto evaluated = fabric::project::evaluate_animation(
                clip, animation_ui.scrub_time);
            ImGui::TextDisabled("Evaluated properties: %zu",
                                evaluated.properties.size());
            for (const auto& property : evaluated.properties) {
                const bool target_node_missing = session.selected_entity().has_value() &&
                    std::ranges::none_of(session.selected_entity()->nodes,
                        [&](const auto& node) { return node.id == property.binding.node_id; });
                if (target_node_missing) {
                    ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                       "Invalid animation binding: missing node '%s'",
                                       property.binding.node_id.c_str());
                    if (ImGui::SmallButton("Repair to first target node") &&
                        session.selected_entity() && !session.selected_entity()->nodes.empty()) {
                        auto repaired = property.binding;
                        repaired.node_id = session.selected_entity()->nodes.front().id;
                        status = session.replace_selected_animation_binding(
                                     property.binding, repaired)
                            ? "Animation binding repaired."
                            : "Animation binding repair failed; inspect diagnostics.";
                    }
                }
                const auto value_label = std::visit(
                    [](const auto& value) {
                        using Value = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<Value, float>) {
                            return std::to_string(value);
                        } else if constexpr (std::is_same_v<Value, fabric::core::Vec2>) {
                            return "(" + std::to_string(value.x) + ", " +
                                std::to_string(value.y) + ")";
                        } else if constexpr (std::is_same_v<Value, fabric::core::Color>) {
                            return "rgba(" + std::to_string(value.red) + ", " +
                                std::to_string(value.green) + ", " +
                                std::to_string(value.blue) + ", " +
                                std::to_string(value.alpha) + ")";
                        } else if constexpr (std::is_same_v<Value, bool>) {
                            return value ? std::string{"true"} : std::string{"false"};
                        } else {
                            return value.id.value;
                        }
                    }, property.value);
                ImGui::BulletText("%s / %s / %s = %s [%s]",
                                  property.binding.node_id.c_str(),
                                  property.binding.component_id.c_str(),
                                  property.binding.property_id.c_str(),
                                  value_label.c_str(),
                                  fabric::project::to_string(property.composition).data());
            }
            ImGui::SeparatorText("Markers");
            ImGui::InputText("Marker id", &animation_ui.marker_id);
            animation_ui.marker_time = std::clamp(animation_ui.marker_time, 0.0F,
                                                   std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Marker time (seconds)", &animation_ui.marker_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            draw_technical_tooltip("Timeline position at which the marker is stored.");
            ImGui::BeginDisabled(animation_ui.marker_id.empty());
            if (ImGui::Button("Add marker")) {
                if (session.insert_selected_animation_marker(
                        animation_ui.marker_id, animation_ui.marker_time)) {
                    status = "Animation marker added.";
                } else {
                    status = "Animation marker rejected; inspect diagnostics.";
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(animation_ui.marker_id.empty(),
                                 "Enter a marker id before adding a marker.");
            bool marker_removed = false;
            for (const auto& marker : clip.markers) {
                ImGui::BulletText("%s · %.2f s", marker.id.c_str(), marker.time);
                ImGui::SameLine();
                const auto marker_button = "Remove##animation-marker-" + marker.id;
                if (ImGui::SmallButton(marker_button.c_str())) {
                    marker_removed = session.remove_selected_animation_marker(marker.id);
                    status = marker_removed
                        ? "Animation marker removed."
                        : "Animation marker could not be removed; inspect diagnostics.";
                    break;
                }
            }
            ImGui::SeparatorText("Set key");
            const std::vector<fabric::project::EntityNode>* target_nodes = nullptr;
            const fabric::project::EntityNode* selected_node = nullptr;
            if (session.selected_entity() &&
                !session.selected_entity()->nodes.empty()) {
                target_nodes = &session.selected_entity()->nodes;
                const auto selected_iterator = std::ranges::find(
                    *target_nodes, animation_ui.node_id,
                    &fabric::project::EntityNode::id);
                selected_node = selected_iterator == target_nodes->end()
                    ? nullptr : &*selected_iterator;
                const char* node_label = selected_node == nullptr
                    ? "Choose target node..." : selected_node->name.c_str();
                if (ImGui::BeginCombo("Target node", node_label)) {
                    for (const auto& target_node : *target_nodes) {
                        if (ImGui::Selectable(target_node.name.c_str(),
                                target_node.id == animation_ui.node_id))
                            animation_ui.node_id = target_node.id;
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", target_node.id.c_str());
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled(
                    "Choose a preview entity to bind one of its nodes.");
                animation_ui.node_id.clear();
            }
            if (selected_node != nullptr) {
                fabric::project::PropertyDescriptorRegistry entity_registry;
                const auto add_entity_property =
                    [&](const char* component, const char* property,
                        const char* path, fabric::project::PropertyValueKind kind,
                        const char* unit = "") {
                        (void)entity_registry.register_descriptor({
                            .component_id = component,
                            .property_id = property,
                            .display_path = path,
                            .value_kind = kind,
                            .readable = true,
                            .writable = true,
                            .animatable = true,
                            .minimum = property == std::string_view{"rotationDegrees"}
                                ? -360.0F : 0.0F,
                            .maximum = property == std::string_view{"rotationDegrees"}
                                ? 360.0F : 1.0F,
                            .unit = unit});
                    };
                using PropertyKind = fabric::project::PropertyValueKind;
                add_entity_property("transform", "position", "Transform / Position",
                                    PropertyKind::vec2, "px");
                add_entity_property("transform", "scale", "Transform / Scale",
                                    PropertyKind::vec2, "×");
                add_entity_property("transform", "rotationDegrees",
                                    "Transform / Rotation", PropertyKind::angle, "°");
                add_entity_property("transform", "pivot", "Transform / Pivot",
                                    PropertyKind::vec2, "px");
                if (selected_node->drawable.material) {
                    add_entity_property("material", "color", "Material / Color",
                                        PropertyKind::color);
                    add_entity_property("material", "opacity", "Material / Opacity",
                                        PropertyKind::scalar, "%");
                }
                if (selected_node->drawable.kind ==
                    fabric::project::EntityDrawableKind::vector) {
                    add_entity_property("fill", "color", "Fill / Color",
                                        PropertyKind::color);
                    add_entity_property("imageFill", "opacity", "Image fill / Opacity",
                                        PropertyKind::scalar, "%");
                    add_entity_property("imageFill", "position", "Image fill / Position",
                                        PropertyKind::vec2, "px");
                    add_entity_property("imageFill", "scale", "Image fill / Scale",
                                        PropertyKind::vec2, "×");
                    add_entity_property("imageFill", "rotationDegrees",
                                        "Image fill / Rotation", PropertyKind::angle, "°");
                    add_entity_property("imageFill", "pivot", "Image fill / Pivot",
                                        PropertyKind::vec2, "px");
                }
                const auto descriptors = entity_registry.animatable();
                const auto current = std::ranges::find_if(
                    descriptors, [&](const auto* descriptor) {
                        return descriptor->component_id == animation_ui.component_id &&
                            descriptor->property_id == animation_ui.property_id;
                    });
                const char* current_label = current == descriptors.end()
                    ? "Choose an entity property..." : (*current)->display_path.c_str();
                if (ImGui::BeginCombo("Entity property", current_label)) {
                    for (const auto* descriptor : descriptors) {
                        if (ImGui::Selectable(descriptor->display_path.c_str(),
                                              descriptor == (current == descriptors.end()
                                                  ? nullptr : *current))) {
                            animation_ui.component_id = descriptor->component_id;
                            animation_ui.property_id = descriptor->property_id;
                            if (descriptor->value_kind == PropertyKind::vec2)
                                animation_ui.key_kind = 0;
                            else if (descriptor->value_kind == PropertyKind::color)
                                animation_ui.key_kind = 2;
                            else animation_ui.key_kind = 1;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            const char* binding_presets[] = {
                "Custom", "Transform / Position", "Transform / Rotation",
                "Transform / Scale", "Material / Opacity", "Material / Color",
                "Fill / Color", "Image fill / Opacity", "Image fill / Position",
                "Image fill / Scale", "Image fill / Rotation",
                "Image fill / Pivot"};
            if (ImGui::Combo("Binding preset", &animation_ui.binding_preset,
                             binding_presets,
                             static_cast<int>(std::size(binding_presets)))) {
                switch (animation_ui.binding_preset) {
                case 1:
                    animation_ui.component_id = "transform";
                    animation_ui.property_id = "position";
                    animation_ui.key_kind = 0;
                    break;
                case 2:
                    animation_ui.component_id = "transform";
                    animation_ui.property_id = "rotationDegrees";
                    animation_ui.key_kind = 1;
                    break;
                case 3:
                    animation_ui.component_id = "transform";
                    animation_ui.property_id = "scale";
                    animation_ui.key_kind = 0;
                    break;
                case 4:
                    animation_ui.component_id = "material";
                    animation_ui.property_id = "opacity";
                    animation_ui.key_kind = 1;
                    break;
                case 5:
                    animation_ui.component_id = "material";
                    animation_ui.property_id = "color";
                    animation_ui.key_kind = 2;
                    break;
                case 6:
                    animation_ui.component_id = "fill";
                    animation_ui.property_id = "color";
                    animation_ui.key_kind = 2;
                    break;
                case 7:
                    animation_ui.component_id = "imageFill";
                    animation_ui.property_id = "opacity";
                    animation_ui.key_kind = 1;
                    break;
                case 8:
                    animation_ui.component_id = "imageFill";
                    animation_ui.property_id = "position";
                    animation_ui.key_kind = 0;
                    break;
                case 9:
                    animation_ui.component_id = "imageFill";
                    animation_ui.property_id = "scale";
                    animation_ui.key_kind = 0;
                    break;
                case 10:
                    animation_ui.component_id = "imageFill";
                    animation_ui.property_id = "rotationDegrees";
                    animation_ui.key_kind = 1;
                    break;
                case 11:
                    animation_ui.component_id = "imageFill";
                    animation_ui.property_id = "pivot";
                    animation_ui.key_kind = 0;
                    break;
                default:
                    break;
                }
            }
            ImGui::SeparatorText("Visual component properties");
            const auto selected_component_resource = std::ranges::find_if(
                session.resources(), [&](const auto& resource) {
                    return resource.kind ==
                            fabric::editor::StudioResourceKind::visual_component &&
                        resource.id.value == animation_ui.visual_component_id;
                });
            const char* selected_component_label =
                selected_component_resource == session.resources().end()
                ? "Choose a visual component..."
                : selected_component_resource->name.c_str();
            if (ImGui::BeginCombo("Component resource",
                                  selected_component_label)) {
                for (const auto& resource : session.resources()) {
                    if (resource.kind !=
                        fabric::editor::StudioResourceKind::visual_component)
                        continue;
                    const bool selected_component =
                        resource.id.value == animation_ui.visual_component_id;
                    if (ImGui::Selectable(resource.name.c_str(),
                                          selected_component))
                        animation_ui.visual_component_id = resource.id.value;
                }
                ImGui::EndCombo();
            }
            if (selected_component_resource != session.resources().end()) {
                const auto component = fabric::project::load_visual_component(
                    session.project_root(), *session.manifest(),
                    selected_component_resource->document_path);
                if (component.ok()) {
                    fabric::project::PropertyDescriptorRegistry registry;
                    for (auto descriptor :
                         fabric::project::visual_component_property_descriptors(
                             *component.asset))
                        (void)registry.register_descriptor(
                            std::move(descriptor));
                    const auto descriptors = registry.animatable();
                    const auto current_descriptor = std::ranges::find_if(
                        descriptors, [&](const auto* descriptor) {
                            return descriptor->component_id ==
                                    animation_ui.component_id &&
                                descriptor->property_id ==
                                    animation_ui.property_id;
                        });
                    const char* descriptor_label =
                        current_descriptor == descriptors.end()
                        ? "Choose an animatable property..."
                        : (*current_descriptor)->display_path.c_str();
                    if (ImGui::BeginCombo("Animatable property",
                                          descriptor_label)) {
                        for (const auto* descriptor : descriptors) {
                            const bool selected_descriptor =
                                descriptor == (current_descriptor ==
                                    descriptors.end() ? nullptr
                                                      : *current_descriptor);
                            if (ImGui::Selectable(
                                    descriptor->display_path.c_str(),
                                    selected_descriptor)) {
                                animation_ui.component_id =
                                    descriptor->component_id;
                                animation_ui.property_id =
                                    descriptor->property_id;
                                using Kind =
                                    fabric::project::PropertyValueKind;
                                if (descriptor->value_kind == Kind::vec2)
                                    animation_ui.key_kind = 0;
                                else if (descriptor->value_kind == Kind::color)
                                    animation_ui.key_kind = 2;
                                else if (descriptor->value_kind == Kind::boolean)
                                    animation_ui.key_kind = 3;
                                else if (descriptor->value_kind == Kind::resource)
                                    animation_ui.key_kind = 4;
                                else animation_ui.key_kind = 1;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            animation_ui.key_time = std::clamp(animation_ui.key_time, 0.0F,
                                                std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Key time (seconds)", &animation_ui.key_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            ImGui::SetItemTooltip("Time position at which the new key is inserted.");
            ImGui::Combo("Key type", &animation_ui.key_kind,
                         "Vec2\0Scalar\0Color\0Boolean\0Resource\0");
            bool auto_key_changed = false;
            if (animation_ui.key_kind == 0) {
                auto_key_changed = ImGui::InputFloat2("Vec2 value (property units)",
                                                      animation_ui.key_value);
                draw_technical_tooltip(
                    "Vector value written to the selected animatable property.");
            } else if (animation_ui.key_kind == 1) {
                auto_key_changed = ImGui::InputFloat("Scalar value (property units)",
                                                     &animation_ui.key_scalar);
                draw_technical_tooltip(
                    "Scalar value written to the selected animatable property.");
            } else if (animation_ui.key_kind == 2) {
                auto_key_changed = ImGui::ColorEdit4("Color value",
                                                     animation_ui.key_color);
            } else if (animation_ui.key_kind == 3) {
                auto_key_changed = ImGui::Checkbox("Boolean value",
                                                    &animation_ui.key_boolean);
            } else {
                const auto selected_resource = std::ranges::find_if(
                    session.resources(), [&](const auto& resource) {
                        return resource.id.value == animation_ui.key_resource_id;
                    });
                const char* resource_label =
                    selected_resource == session.resources().end()
                    ? (animation_ui.key_resource_id.empty()
                        ? "Choose a resource..." : "Missing resource")
                    : selected_resource->name.c_str();
                if (ImGui::BeginCombo("Resource value", resource_label)) {
                    for (const auto& resource : session.resources()) {
                        const bool selected =
                            resource.id.value == animation_ui.key_resource_id;
                        if (ImGui::Selectable(resource.name.c_str(), selected)) {
                            animation_ui.key_resource_id = resource.id.value;
                            auto_key_changed = true;
                        }
                        ImGui::SameLine();
                        const auto kind_label = std::string(
                            studio_resource_kind_label(resource.kind));
                        ImGui::TextDisabled("%s · %s",
                                           kind_label.c_str(),
                                           resource.id.value.c_str());
                    }
                    ImGui::EndCombo();
                }
            }
            if (animation_ui.interpolation ==
                    fabric::project::AnimationInterpolation::cubic &&
                animation_ui.key_kind != 3 && animation_ui.key_kind != 4) {
                ImGui::Checkbox("Custom tangents", &animation_ui.tangents_enabled);
                if (animation_ui.tangents_enabled) {
                    if (animation_ui.key_kind == 0) {
                        ImGui::InputFloat2("In tangent (property units)",
                                           animation_ui.key_in_tangent);
                        draw_technical_tooltip(
                            "Incoming cubic tangent in the selected property units.");
                        ImGui::InputFloat2("Out tangent (property units)",
                                           animation_ui.key_out_tangent);
                        draw_technical_tooltip(
                            "Outgoing cubic tangent in the selected property units.");
                    } else if (animation_ui.key_kind == 1) {
                        ImGui::InputFloat("In tangent (property units)",
                                          &animation_ui.key_in_tangent_scalar);
                        draw_technical_tooltip(
                            "Incoming cubic tangent in the selected property units.");
                        ImGui::InputFloat("Out tangent (property units)",
                                          &animation_ui.key_out_tangent_scalar);
                        draw_technical_tooltip(
                            "Outgoing cubic tangent in the selected property units.");
                    } else {
                        ImGui::InputFloat4("In tangent (color channels)",
                                           animation_ui.key_in_tangent_color);
                        draw_technical_tooltip(
                            "Incoming cubic tangent for the color channels.");
                        ImGui::InputFloat4("Out tangent (color channels)",
                                           animation_ui.key_out_tangent_color);
                        draw_technical_tooltip(
                            "Outgoing cubic tangent for the color channels.");
                    }
                }
            }
            ImGui::SeparatorText("A → B segment");
            animation_ui.segment_start_time = std::clamp(
                animation_ui.segment_start_time, 0.0F,
                std::max(0.0F, clip.duration));
            animation_ui.segment_end_time = std::clamp(
                animation_ui.segment_end_time, 0.0F,
                std::max(0.0F, clip.duration));
            ImGui::InputFloat("A time (seconds)", &animation_ui.segment_start_time,
                              0.1F, 1.0F, "%.2f s");
            draw_technical_tooltip("Start time of the source segment.");
            ImGui::InputFloat("B time (seconds)", &animation_ui.segment_end_time,
                              0.1F, 1.0F, "%.2f s");
            draw_technical_tooltip("End time of the destination segment.");
            if (animation_ui.key_kind == 0) {
                ImGui::InputFloat2("A value (world units)", animation_ui.segment_start_value);
                ImGui::InputFloat2("B value (world units)", animation_ui.segment_end_value);
            } else if (animation_ui.key_kind == 1) {
                ImGui::InputFloat("A value (scalar)", &animation_ui.segment_start_scalar);
                ImGui::InputFloat("B value (scalar)", &animation_ui.segment_end_scalar);
            } else if (animation_ui.key_kind == 2) {
                ImGui::ColorEdit4("A value", animation_ui.segment_start_color);
                ImGui::ColorEdit4("B value", animation_ui.segment_end_color);
            } else if (animation_ui.key_kind == 3) {
                ImGui::Checkbox("A value", &animation_ui.segment_start_boolean);
                ImGui::Checkbox("B value", &animation_ui.segment_end_boolean);
            } else {
                const auto draw_segment_resource = [&](const char* label,
                                                       std::string& value) {
                    const auto selected = std::ranges::find_if(
                        session.resources(), [&](const auto& resource) {
                            return resource.id.value == value;
                        });
                    const char* preview = selected == session.resources().end()
                        ? (value.empty() ? "Choose a resource..." : "Missing resource")
                        : selected->name.c_str();
                    if (ImGui::BeginCombo(label, preview)) {
                        for (const auto& resource : session.resources()) {
                            if (ImGui::Selectable(resource.name.c_str(),
                                                  resource.id.value == value))
                                value = resource.id.value;
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", resource.id.value.c_str());
                        }
                        ImGui::EndCombo();
                    }
                };
                draw_segment_resource("A resource", animation_ui.segment_start_resource_id);
                draw_segment_resource("B resource", animation_ui.segment_end_resource_id);
            }
            ImGui::BeginDisabled(animation_ui.node_id.empty() ||
                                 animation_ui.component_id.empty() ||
                                 animation_ui.property_id.empty() ||
                                 animation_ui.segment_start_time >=
                                     animation_ui.segment_end_time ||
                                 (animation_ui.key_kind == 4 &&
                                  (animation_ui.segment_start_resource_id.empty() ||
                                   animation_ui.segment_end_resource_id.empty())));
            if (ImGui::Button("Create A → B keys")) {
                const auto segment_value = [&](const bool start) {
                    if (animation_ui.key_kind == 0)
                        return fabric::project::AnimationValue{
                            fabric::core::Vec2{
                                (start ? animation_ui.segment_start_value
                                       : animation_ui.segment_end_value)[0],
                                (start ? animation_ui.segment_start_value
                                       : animation_ui.segment_end_value)[1]}};
                    if (animation_ui.key_kind == 1)
                        return fabric::project::AnimationValue{
                            start ? animation_ui.segment_start_scalar
                                  : animation_ui.segment_end_scalar};
                    if (animation_ui.key_kind == 2)
                        return fabric::project::AnimationValue{
                            fabric::core::Color{
                                (start ? animation_ui.segment_start_color
                                       : animation_ui.segment_end_color)[0],
                                (start ? animation_ui.segment_start_color
                                       : animation_ui.segment_end_color)[1],
                                (start ? animation_ui.segment_start_color
                                       : animation_ui.segment_end_color)[2],
                                (start ? animation_ui.segment_start_color
                                       : animation_ui.segment_end_color)[3]}};
                    if (animation_ui.key_kind == 3)
                        return fabric::project::AnimationValue{
                            start ? animation_ui.segment_start_boolean
                                  : animation_ui.segment_end_boolean};
                    return fabric::project::AnimationValue{
                        fabric::project::ResourceReference{
                            {.value = start
                                ? animation_ui.segment_start_resource_id
                                : animation_ui.segment_end_resource_id},
                            "resource"}};
                };
                const bool created = session.set_selected_animation_segment(
                    {.node_id = animation_ui.node_id,
                     .component_id = animation_ui.component_id,
                     .property_id = animation_ui.property_id},
                    animation_ui.segment_start_time,
                    segment_value(true), animation_ui.segment_end_time,
                    segment_value(false), animation_ui.interpolation,
                    fabric::editor::AutosaveScheduler::Clock::now(),
                    animation_ui.composition, animation_ui.easing);
                status = created ? "Animation A → B segment created."
                                 : "Animation segment rejected; inspect diagnostics.";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(
                animation_ui.node_id.empty() ||
                    animation_ui.component_id.empty() ||
                    animation_ui.property_id.empty() ||
                    animation_ui.segment_start_time >= animation_ui.segment_end_time ||
                    (animation_ui.key_kind == 4 &&
                     (animation_ui.segment_start_resource_id.empty() ||
                      animation_ui.segment_end_resource_id.empty())),
                "Choose a target node, component, property, an increasing A/B time range and resource values when required.");
            const auto interpolation_label = std::string(
                fabric::project::to_string(animation_ui.interpolation));
            if (ImGui::BeginCombo("Interpolation", interpolation_label.c_str())) {
                for (const auto option : {
                         fabric::project::AnimationInterpolation::step,
                         fabric::project::AnimationInterpolation::linear,
                         fabric::project::AnimationInterpolation::cubic}) {
                    const bool selected_option = option == animation_ui.interpolation;
                    const auto label = std::string(fabric::project::to_string(option));
                    if (ImGui::Selectable(label.c_str(), selected_option)) {
                        animation_ui.interpolation = option;
                    }
                }
                ImGui::EndCombo();
            }
            const auto easing_label = std::string(
                fabric::project::to_string(animation_ui.easing));
            if (ImGui::BeginCombo("Easing", easing_label.c_str())) {
                for (const auto option : {
                         fabric::project::AnimationEasing::linear,
                         fabric::project::AnimationEasing::ease_in,
                         fabric::project::AnimationEasing::ease_out,
                         fabric::project::AnimationEasing::ease_in_out}) {
                    const bool selected_option = option == animation_ui.easing;
                    const auto label = std::string(
                        fabric::project::to_string(option));
                    if (ImGui::Selectable(label.c_str(), selected_option))
                        animation_ui.easing = option;
                }
                ImGui::EndCombo();
            }
            const auto composition_label = std::string(
                fabric::project::to_string(animation_ui.composition));
            if (ImGui::BeginCombo("Composition", composition_label.c_str())) {
                for (const auto option : {
                         fabric::project::AnimationComposition::replace,
                         fabric::project::AnimationComposition::additive}) {
                    const bool selected_option = option == animation_ui.composition;
                    const auto label = std::string(fabric::project::to_string(option));
                    if (ImGui::Selectable(label.c_str(), selected_option)) {
                        animation_ui.composition = option;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(animation_ui.node_id.empty() ||
                                 animation_ui.component_id.empty() ||
                                 animation_ui.property_id.empty() ||
                                  (animation_ui.key_kind == 4 &&
                                  animation_ui.key_resource_id.empty()));
            const auto set_key = [&]() {
                fabric::project::AnimationValue value;
                if (animation_ui.key_kind == 0) {
                    value = fabric::core::Vec2{animation_ui.key_value[0],
                                               animation_ui.key_value[1]};
                } else if (animation_ui.key_kind == 1) {
                    value = animation_ui.key_scalar;
                } else if (animation_ui.key_kind == 2) {
                    value = fabric::core::Color{animation_ui.key_color[0],
                                                animation_ui.key_color[1],
                                                animation_ui.key_color[2],
                                                animation_ui.key_color[3]};
                } else if (animation_ui.key_kind == 3) {
                    value = animation_ui.key_boolean;
                } else {
                    value = fabric::project::ResourceReference{
                        {.value = animation_ui.key_resource_id}, "resource"};
                }
                std::optional<fabric::project::AnimationValue> in_tangent;
                std::optional<fabric::project::AnimationValue> out_tangent;
                if (animation_ui.tangents_enabled && animation_ui.key_kind == 0) {
                    in_tangent = fabric::core::Vec2{animation_ui.key_in_tangent[0],
                                                   animation_ui.key_in_tangent[1]};
                    out_tangent = fabric::core::Vec2{animation_ui.key_out_tangent[0],
                                                    animation_ui.key_out_tangent[1]};
                } else if (animation_ui.tangents_enabled && animation_ui.key_kind == 1) {
                    in_tangent = animation_ui.key_in_tangent_scalar;
                    out_tangent = animation_ui.key_out_tangent_scalar;
                } else if (animation_ui.tangents_enabled && animation_ui.key_kind == 2) {
                    in_tangent = fabric::core::Color{animation_ui.key_in_tangent_color[0],
                                                     animation_ui.key_in_tangent_color[1],
                                                     animation_ui.key_in_tangent_color[2],
                                                     animation_ui.key_in_tangent_color[3]};
                    out_tangent = fabric::core::Color{animation_ui.key_out_tangent_color[0],
                                                      animation_ui.key_out_tangent_color[1],
                                                      animation_ui.key_out_tangent_color[2],
                                                      animation_ui.key_out_tangent_color[3]};
                }
                if (session.set_selected_animation_key(
                        {.node_id = animation_ui.node_id,
                         .component_id = animation_ui.component_id,
                         .property_id = animation_ui.property_id},
                        animation_ui.auto_key
                            ? animation_ui.scrub_time
                            : animation_ui.key_time,
                        std::move(value),
                        animation_ui.interpolation,
                        fabric::editor::AutosaveScheduler::Clock::now(),
                        animation_ui.composition, animation_ui.easing,
                        std::move(in_tangent), std::move(out_tangent))) {
                    status = "Animation key set.";
                } else {
                    status = "Animation key rejected; inspect diagnostics.";
                }
            };
            if (ImGui::Button("Set key") ||
                (animation_ui.auto_key && auto_key_changed)) {
                set_key();
            }
            ImGui::EndDisabled();
            draw_disabled_reason(animation_ui.node_id.empty() ||
                                     animation_ui.component_id.empty() ||
                                     animation_ui.property_id.empty() ||
                                     (animation_ui.key_kind == 4 &&
                                      animation_ui.key_resource_id.empty()),
                                 "Choose a target node, component, property and resource value when required.");
            ImGui::SeparatorText("Tracks");
            if (ImGui::Button("Select all keys")) {
                animation_ui.selected_keys.clear();
                for (const auto& track : clip.tracks)
                    for (std::size_t key_index = 0;
                         key_index < track.keys.size(); ++key_index)
                        animation_ui.selected_keys.push_back(
                            {track.binding, key_index});
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear key selection"))
                animation_ui.selected_keys.clear();
            ImGui::SameLine();
            ImGui::BeginDisabled(animation_ui.selected_keys.empty());
            if (ImGui::Button("Copy selected keys")) {
                animation_ui.key_clipboard.clear();
                for (const auto& selected : animation_ui.selected_keys) {
                    const auto track = std::ranges::find(
                        clip.tracks, selected.binding,
                        &fabric::project::AnimationTrack::binding);
                    if (track == clip.tracks.end() ||
                        selected.index >= track->keys.size()) continue;
                    animation_ui.key_clipboard.push_back({
                        selected.binding, track->keys[selected.index],
                        track->interpolation, track->composition, track->easing});
                }
                status = animation_ui.key_clipboard.empty()
                    ? "No valid animation keys selected."
                    : "Animation keys copied.";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(animation_ui.selected_keys.empty(),
                                 "Select at least one valid animation key to copy.");
            ImGui::SameLine();
            ImGui::BeginDisabled(animation_ui.key_clipboard.empty());
            if (ImGui::Button("Paste at key time")) {
                const auto first = std::ranges::min_element(
                    animation_ui.key_clipboard, {},
                    [](const auto& entry) { return entry.key.time; });
                const auto first_time = first->key.time;
                bool pasted = false;
                for (const auto& entry : animation_ui.key_clipboard) {
                    const auto time = snap_key_time(
                        animation_ui.key_time + entry.key.time - first_time);
                    pasted = session.set_selected_animation_key(
                                 entry.binding, time, entry.key.value,
                                 entry.interpolation,
                                 fabric::editor::AutosaveScheduler::Clock::now(),
                                 entry.composition, entry.easing,
                                 entry.key.in_tangent,
                                 entry.key.out_tangent) || pasted;
                }
                status = pasted ? "Animation keys pasted."
                                : "Animation keys could not be pasted; inspect diagnostics.";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(animation_ui.key_clipboard.empty(),
                                 "Copy animation keys before pasting them.");
            if (!animation_ui.key_clipboard.empty())
                ImGui::SameLine(), ImGui::TextDisabled(
                    "%zu copied", animation_ui.key_clipboard.size());
            if (clip.tracks.empty()) {
                ImGui::TextDisabled("No tracks yet.");
            }
            bool key_removed = false;
            for (std::size_t track_index = 0;
                 track_index < clip.tracks.size() && !key_removed;
                 ++track_index) {
                const auto& track = clip.tracks[track_index];
                ImGui::TextWrapped("%s / %s / %s (%zu keys)",
                                   track.binding.node_id.c_str(),
                                   track.binding.component_id.c_str(),
                                   track.binding.property_id.c_str(),
                                   track.keys.size());
                for (std::size_t key_index = 0;
                    key_index < track.keys.size(); ++key_index) {
                    const auto& key = track.keys[key_index];
                    const auto key_scope = "animation-key-" +
                        std::to_string(track_index) + "-" +
                        std::to_string(key_index);
                    ImGui::PushID(key_scope.c_str());
                    bool selected = std::ranges::any_of(
                        animation_ui.selected_keys, [&](const auto& candidate) {
                            return candidate.binding == track.binding &&
                                   candidate.index == key_index;
                        });
                    if (ImGui::Checkbox("##selected", &selected)) {
                        const auto found = std::ranges::find_if(
                            animation_ui.selected_keys,
                            [&](const auto& candidate) {
                                return candidate.binding == track.binding &&
                                       candidate.index == key_index;
                            });
                        if (selected && found == animation_ui.selected_keys.end())
                            animation_ui.selected_keys.push_back(
                                {track.binding, key_index});
                        else if (!selected &&
                                 found != animation_ui.selected_keys.end())
                            animation_ui.selected_keys.erase(found);
                    }
                    ImGui::SameLine();
                    ImGui::BulletText("key %zu", key_index);
                    ImGui::SameLine();
                    float key_time = key.time;
                    ImGui::SetNextItemWidth(120.0F);
                    if (ImGui::SliderFloat("##key-time", &key_time, 0.0F,
                                           std::max(0.01F, clip.duration),
                                           "%.2f s")) {
                        key_time = snap_key_time(key_time);
                        if (!session.move_selected_animation_key(
                                track.binding, key_index, key_time)) {
                            status = "Key move rejected; inspect diagnostics.";
                        } else {
                            animation_ui.selected_keys.clear();
                        }
                    }
                    ImGui::SameLine();
                    const auto button_id = "Remove##animation-key-" +
                        std::to_string(track_index) + "-" +
                        std::to_string(key_index);
                    if (ImGui::SmallButton(button_id.c_str())) {
                        key_removed = session.remove_selected_animation_key(
                            track.binding, key_index);
                        status = key_removed
                            ? "Animation key removed."
                            : "Animation key could not be removed; inspect diagnostics.";
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
            }
        }
    } else {
        ImGui::TextDisabled("No selection");
    }
    if (!session.errors().empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Diagnostics");
        draw_diagnostics(session);
    }
    ImGui::End();

    if (project_settings.request && session.has_project()) {
        project_settings.name = session.manifest()->name;
        project_settings.pixels_per_unit = session.manifest()->pixels_per_unit;
        project_settings.runtime_enabled = false;
        project_settings.spawn_x = 0.0F;
        project_settings.spawn_y = 0.0F;
        project_settings.runtime_actions = {};
        project_settings.camera_follow_character = false;
        project_settings.camera_limits_enabled = false;
        project_settings.camera_x = 0.0F;
        project_settings.camera_y = 0.0F;
        project_settings.camera_width = 100.0F;
        project_settings.camera_height = 100.0F;
        project_settings.audio_id.clear();
        if (session.manifest()->runtime) {
            const auto& runtime = *session.manifest()->runtime;
            project_settings.runtime_enabled = runtime.character.enabled;
            if (runtime.character.spawn) {
                project_settings.spawn_x = runtime.character.spawn->x;
                project_settings.spawn_y = runtime.character.spawn->y;
            }
            project_settings.runtime_actions = runtime.character.actions;
            project_settings.camera_follow_character =
                runtime.camera.follow_character;
            if (runtime.camera.limits) {
                project_settings.camera_limits_enabled = true;
                project_settings.camera_x = runtime.camera.limits->origin.x;
                project_settings.camera_y = runtime.camera.limits->origin.y;
                project_settings.camera_width = runtime.camera.limits->size.x;
                project_settings.camera_height = runtime.camera.limits->size.y;
            }
            if (runtime.audio) project_settings.audio_id = runtime.audio->value;
        }
        ImGui::OpenPopup("Project settings");
        project_settings.request = false;
    }
    if (ImGui::BeginPopupModal("Project settings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        draw_resource_name_field("Project name", project_settings.name, 420.0F);
        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputDouble("Pixels per unit",
                           &project_settings.pixels_per_unit, 1.0, 10.0,
                           "%.2f");
        ImGui::SetItemTooltip("Pixels represented by one project world unit; this controls raster import and preview scaling.");
        ImGui::SeparatorText("Runtime preview");
        ImGui::Checkbox("Enable character", &project_settings.runtime_enabled);
        ImGui::InputFloat2("Character spawn (world units)", &project_settings.spawn_x);
        ImGui::SetItemTooltip("Initial character position in project world units.");
        ImGui::InputText("Left action", &project_settings.runtime_actions[0]);
        ImGui::InputText("Right action", &project_settings.runtime_actions[1]);
        ImGui::InputText("Jump action", &project_settings.runtime_actions[2]);
        ImGui::Checkbox("Follow character", &project_settings.camera_follow_character);
        ImGui::Checkbox("Camera limits", &project_settings.camera_limits_enabled);
        if (project_settings.camera_limits_enabled) {
            ImGui::InputFloat2("Camera origin (world units)", &project_settings.camera_x);
            ImGui::SetItemTooltip("Camera limits origin in project world units.");
            ImGui::InputFloat2("Camera size (world units)", &project_settings.camera_width);
            ImGui::SetItemTooltip("Camera limits size in project world units.");
        }
        draw_project_resource_picker(
            "Audio document", session.resources(),
            fabric::editor::StudioResourceKind::audio,
            project_settings.audio_id, true);
        ImGui::TextDisabled("%s", session.project_root().string().c_str());
        const bool valid = !project_settings.name.empty() &&
            project_settings.name.size() <= 255 &&
            std::isfinite(project_settings.pixels_per_unit) &&
            project_settings.pixels_per_unit > 0.0 &&
            project_settings.pixels_per_unit <= 1'000'000.0 &&
            (!project_settings.camera_limits_enabled ||
             (std::isfinite(project_settings.camera_width) &&
              std::isfinite(project_settings.camera_height) &&
              project_settings.camera_width >= 0.0F &&
              project_settings.camera_height >= 0.0F));
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Apply", {110.0F, 0.0F})) {
            const bool name_updated =
                session.set_project_name(project_settings.name);
            const bool units_updated = session.set_pixels_per_unit(
                project_settings.pixels_per_unit);
            fabric::project::RuntimeSettings runtime;
            runtime.character.enabled = project_settings.runtime_enabled;
            runtime.character.spawn = fabric::core::Vec2{
                project_settings.spawn_x, project_settings.spawn_y};
            runtime.character.actions = project_settings.runtime_actions;
            runtime.camera.follow_character =
                project_settings.camera_follow_character;
            if (project_settings.camera_limits_enabled) {
                runtime.camera.limits = fabric::core::Rect{
                    {project_settings.camera_x, project_settings.camera_y},
                    {project_settings.camera_width, project_settings.camera_height}};
            }
            if (!project_settings.audio_id.empty()) {
                runtime.audio = fabric::core::ResourceId{
                    .value = project_settings.audio_id};
            }
            const bool runtime_updated = session.set_runtime_settings(runtime);
            if (name_updated && units_updated && runtime_updated) {
                status = "Project settings changed.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Invalid project settings; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Enter a valid project name, positive pixels-per-unit and valid camera limits.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos({viewport->Pos.x,
                             viewport->Pos.y + viewport->Size.y - status_height});
    ImGui::SetNextWindowSize({viewport->Size.x, status_height});
    ImGui::Begin("Status", nullptr, fixed_panel_flags | ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(status.c_str());
    if (session.dirty() || behavior_session.dirty() ||
        transformation_session.dirty()) {
        ImGui::SameLine();
        ImGui::TextColored({0.89F, 0.68F, 0.34F, 1.0F}, "Unsaved changes");
    }
    ImGui::End();

    if (creation.request_project) {
        creation.project.reset();
        creation.project_publish_attempted = false;
        ImGui::OpenPopup("Create project");
        creation.request_project = false;
    }
    if (creation.request_artwork && session.has_project()) {
        creation.artwork.reset();
        ImGui::OpenPopup("Create vector artwork");
        creation.request_artwork = false;
    }
    if (creation.request_material && session.has_project()) {
        creation.material.reset();
        ImGui::OpenPopup("Create material / fill");
        creation.request_material = false;
    }
    if (creation.request_entity && session.has_project()) {
        creation.entity.reset();
        ImGui::OpenPopup("Create entity");
        creation.request_entity = false;
    }
    if (creation.request_animation && session.has_project()) {
        creation.animation.reset();
        if (session.selected_resource() &&
            session.selected_resource()->kind ==
                fabric::editor::StudioResourceKind::entity &&
            session.selected_entity())
            creation.animation.preview_entity_id =
                session.selected_entity()->document.id.value;
        ImGui::OpenPopup("Create animation");
        creation.request_animation = false;
    }
    if (creation.request_input && session.has_project()) {
        creation.input.reset();
        ImGui::OpenPopup("Create input bindings");
        creation.request_input = false;
    }
    if (creation.request_visual_preset && session.has_project()) {
        creation.visual_preset = {};
        ImGui::OpenPopup("Create visual preset");
        creation.request_visual_preset = false;
    }
    if (creation.request_visual_composition && session.has_project()) {
        creation.composition = {};
        ImGui::OpenPopup("Create visual composition");
        creation.request_visual_composition = false;
    }
    if (creation.request_visual_component && session.has_project()) {
        creation.component = {};
        ImGui::OpenPopup("Create visual component");
        creation.request_visual_component = false;
    }
    if (request_open) {
        if (choose_folder(window, path_buffer, status)) {
            if (session.open(path_buffer.data())) {
                clear_asset_preview(preview);
                creation.prepared_artwork.reset();
                status = "Project opened: " + session.manifest()->name;
            } else {
                status = "Project rejected; inspect the diagnostics.";
            }
        }
        request_open = false;
    }
    draw_import_workflow(session, window, imports, preview,
                         pending_import_preview, request_png, request_svg,
                         status);

    if (ImGui::BeginPopupModal("Create project", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a versioned Vertex Loom project");
        ImGui::Spacing();
        const auto validation = creation.project.validate();
        std::string destination = creation.project.parent_directory.string();
        ImGui::SetNextItemWidth(560.0F);
        if (ImGui::InputText("Parent folder", &destination)) {
            creation.project.parent_directory = destination;
        }
        focus_prompt_field(validation, "destination", "project-create");
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::array<char, 1024> selected{};
            if (choose_folder(window, selected, status)) {
                creation.project.parent_directory = selected.data();
            }
        }
        draw_resource_name_field("Name##project-name", creation.project.name);
        focus_prompt_field(validation, "name", "project-create");
        const auto preset_label = std::string(fabric::editor::label(
            creation.project.preset));
        ImGui::SetNextItemWidth(300.0F);
        if (ImGui::BeginCombo("Project preset", preset_label.c_str())) {
            for (const auto preset : {
                     fabric::editor::ProjectScalePreset::standard,
                     fabric::editor::ProjectScalePreset::compact,
                     fabric::editor::ProjectScalePreset::high_detail,
                     fabric::editor::ProjectScalePreset::custom}) {
                const bool selected = creation.project.preset == preset;
                const auto option = std::string(fabric::editor::label(preset));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.project.select_preset(preset);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted("Units: world units");
        ImGui::SetNextItemWidth(220.0F);
        if (ImGui::InputDouble("Pixels per unit",
                               &creation.project.pixels_per_unit, 1.0, 10.0,
                               "%.2f")) {
            creation.project.preset =
                fabric::editor::ProjectScalePreset::custom;
        }
        focus_prompt_field(validation, "pixelsPerUnit", "project-create");
        draw_prompt_error(validation, "destination");
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "pixelsPerUnit");
        draw_prompt_summary(validation);
        ImGui::Spacing();
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create project", {140.0F, 0.0F})) {
            creation.project_publish_attempted = true;
            if (session.create(creation.project.project_root(),
                               creation.project.manifest())) {
                clear_asset_preview(preview);
                creation.prepared_artwork.reset();
                status = "Project created: " + session.manifest()->name;
                copy_path_to_buffer(session.project_root(), path_buffer);
                ImGui::CloseCurrentPopup();
            } else {
                status = "Project creation failed; inspect the diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!validation.ok(),
                             "Complete the required project fields and resolve validation errors.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.project.reset();
            creation.project_publish_attempted = false;
            ImGui::CloseCurrentPopup();
        }
        if (creation.project_publish_attempted && !session.errors().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Creation failed");
            draw_diagnostics(session);
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints({640.0F, 300.0F},
                                        {700.0F, viewport->Size.y - 80.0F});
    if (ImGui::BeginPopupModal("Create vector artwork", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a native, editable vector artwork");
        ImGui::TextDisabled("The validated document is published atomically in the open project.");
        const auto validation = creation.artwork.validate(
            session.project_root(), *session.manifest());
        draw_resource_name_field("Name##artwork-name", creation.artwork.name);
        focus_prompt_field(validation, "name", "artwork-create");
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputDouble("Width (world units)", &creation.artwork.width, 1.0, 10.0,
                           "%.2f");
        focus_prompt_field(validation, "width", "artwork-create");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputDouble("Height (world units)", &creation.artwork.height, 1.0, 10.0,
                           "%.2f");
        focus_prompt_field(validation, "height", "artwork-create");
        ImGui::TextUnformatted("Units: project world units");
        const auto origin_label =
            std::string(fabric::editor::label(creation.artwork.origin));
        if (ImGui::BeginCombo("Origin", origin_label.c_str())) {
            for (const auto origin : {fabric::editor::ArtworkOrigin::center,
                                      fabric::editor::ArtworkOrigin::top_left}) {
                const bool selected = creation.artwork.origin == origin;
                const auto option = std::string(fabric::editor::label(origin));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.artwork.origin = origin;
                }
            }
            ImGui::EndCombo();
        }
        const auto shape_label =
            std::string(fabric::editor::label(creation.artwork.first_shape));
        if (ImGui::BeginCombo("First shape", shape_label.c_str())) {
            for (const auto shape : {fabric::editor::InitialShape::rectangle,
                                     fabric::editor::InitialShape::ellipse,
                                     fabric::editor::InitialShape::empty}) {
                const bool selected = creation.artwork.first_shape == shape;
                const auto option = std::string(fabric::editor::label(shape));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.artwork.first_shape = shape;
                }
            }
            ImGui::EndCombo();
        }
        const auto fill_label =
            std::string(fabric::editor::label(creation.artwork.initial_fill));
        if (ImGui::BeginCombo("Initial fill", fill_label.c_str())) {
            for (const auto fill : {fabric::editor::InitialFill::color,
                                    fabric::editor::InitialFill::image,
                                    fabric::editor::InitialFill::transparent}) {
                const bool selected = creation.artwork.initial_fill == fill;
                const auto option = std::string(fabric::editor::label(fill));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.artwork.initial_fill = fill;
                }
            }
            ImGui::EndCombo();
        }
        if (creation.artwork.initial_fill ==
            fabric::editor::InitialFill::color) {
            float color[] = {creation.artwork.initial_color.red,
                             creation.artwork.initial_color.green,
                             creation.artwork.initial_color.blue,
                             creation.artwork.initial_color.alpha};
            if (ImGui::ColorEdit4("Initial color", color)) {
                creation.artwork.initial_color = {
                    color[0], color[1], color[2], color[3]};
            }
        } else if (creation.artwork.initial_fill ==
                   fabric::editor::InitialFill::image) {
            static_cast<void>(draw_project_resource_picker(
                "Texture", session.resources(),
                fabric::editor::StudioResourceKind::texture,
                creation.artwork.initial_image_id, false));
            const auto fit_label = std::string(fabric::project::to_string(
                creation.artwork.image_fit));
            if (ImGui::BeginCombo("Image fit", fit_label.c_str())) {
                for (const auto fit : {
                         fabric::project::VectorImageFit::contain,
                         fabric::project::VectorImageFit::cover,
                         fabric::project::VectorImageFit::stretch,
                         fabric::project::VectorImageFit::free}) {
                    const bool selected = creation.artwork.image_fit == fit;
                    const auto option =
                        std::string(fabric::project::to_string(fit));
                    if (ImGui::Selectable(option.c_str(), selected)) {
                        creation.artwork.image_fit = fit;
                    }
                }
                ImGui::EndCombo();
            }
            float offset[] = {
                creation.artwork.image_transform.position.x,
                creation.artwork.image_transform.position.y};
            if (ImGui::InputFloat2("Image offset (world units)", offset)) {
                creation.artwork.image_transform.position = {
                    offset[0], offset[1]};
            }
            focus_prompt_field(validation, "imageTransform", "artwork-create");
            float scale[] = {creation.artwork.image_transform.scale.x,
                             creation.artwork.image_transform.scale.y};
            if (ImGui::InputFloat2("Image scale (factor)", scale)) {
                creation.artwork.image_transform.scale = {scale[0], scale[1]};
            }
            ImGui::SetItemTooltip("Scale multiplier applied to the selected image fill.");
            float pivot[] = {creation.artwork.image_transform.pivot.x,
                             creation.artwork.image_transform.pivot.y};
            if (ImGui::InputFloat2("Image pivot (world units)", pivot)) {
                creation.artwork.image_transform.pivot = {
                    pivot[0], pivot[1]};
            }
            ImGui::SetItemTooltip("Pivot of the selected image fill in project world units.");
            ImGui::InputFloat(
                "Image rotation (degrees)",
                &creation.artwork.image_transform.rotation_degrees,
                1.0F, 10.0F, "%.2f deg");
            float opacity = static_cast<float>(
                creation.artwork.image_opacity);
            if (ImGui::SliderFloat("Image opacity (0–1)", &opacity, 0.0F, 1.0F,
                                   "%.2f")) {
                creation.artwork.image_opacity = opacity;
            }
            focus_prompt_field(validation, "imageOpacity", "artwork-create");
            ImGui::Checkbox("Warp pixels with vector shape (advanced)",
                            &creation.artwork.deform_image_with_shape);
            ImGui::TextDisabled(
                "Off keeps image placement independent; crop the raster source in its own viewport.");
        }
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "id");
        draw_prompt_error(validation, "width");
        draw_prompt_error(validation, "height");
        draw_prompt_error(validation, "initialFill");
        draw_prompt_error(validation, "initialImage");
        draw_prompt_error(validation, "imageTransform");
        draw_prompt_error(validation, "imageOpacity");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create artwork", {140.0F, 0.0F})) {
            if (session.create_vector_artwork(creation.artwork)) {
                creation.prepared_artwork = creation.artwork;
                clear_asset_preview(preview);
                canvas = {};
                status = "Native artwork created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Native artwork creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!validation.ok(),
                             "Complete the artwork fields and resolve validation errors.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.artwork.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create visual preset", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& request = creation.visual_preset;
        ImGui::TextUnformatted("Create a reusable textile visual component");
        ImGui::TextDisabled(
            "The preset only assembles generic vectors, paths and layers.");
        const auto kind_label = std::string(fabric::editor::label(request.kind));
        ImGui::SetNextItemWidth(280.0F);
        if (ImGui::BeginCombo("Preset", kind_label.c_str())) {
            for (const auto kind : {fabric::editor::VisualPresetKind::eye,
                                    fabric::editor::VisualPresetKind::button,
                                    fabric::editor::VisualPresetKind::seam,
                                    fabric::editor::VisualPresetKind::zipper}) {
                const bool selected = request.kind == kind;
                const auto option = std::string(fabric::editor::label(kind));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    request.kind = kind;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        draw_resource_identity_fields(request.name, request.id.value);
        const bool uses_thread = request.kind ==
                fabric::editor::VisualPresetKind::seam ||
            request.kind == fabric::editor::VisualPresetKind::zipper;
        if (uses_thread) {
            const auto selected_texture = std::ranges::find_if(
                session.resources(), [&](const auto& resource) {
                    return resource.kind ==
                               fabric::editor::StudioResourceKind::texture &&
                        request.thread_texture &&
                        resource.id == request.thread_texture->id;
                });
            const char* texture_label =
                selected_texture == session.resources().end()
                ? "Choose a thread texture..."
                : selected_texture->name.c_str();
            ImGui::SetNextItemWidth(360.0F);
            if (ImGui::BeginCombo("Thread texture", texture_label)) {
                for (const auto& resource : session.resources()) {
                    if (resource.kind !=
                        fabric::editor::StudioResourceKind::texture) continue;
                    const bool selected = request.thread_texture &&
                        request.thread_texture->id == resource.id;
                    if (ImGui::Selectable(resource.name.c_str(), selected)) {
                        request.thread_texture = fabric::project::ResourceReference{
                            resource.id, "texture"};
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        if (request.kind == fabric::editor::VisualPresetKind::zipper) {
            int tooth_count = static_cast<int>(request.zipper_tooth_count);
            ImGui::SetNextItemWidth(180.0F);
            if (ImGui::InputInt("Teeth", &tooth_count)) {
                request.zipper_tooth_count = tooth_count < 0
                    ? 0U : static_cast<std::size_t>(tooth_count);
            }
        }
        const auto built = fabric::editor::build_visual_preset(
            *session.manifest(), request);
        if (built.ok()) {
            ImGui::SeparatorText("Resources created");
            ImGui::Text("%zu vector artwork(s)", built.bundle->vectors.size());
            ImGui::Text("%zu textured path(s)",
                        built.bundle->textured_paths.size());
            ImGui::TextUnformatted("1 composition");
            ImGui::TextUnformatted("1 reusable component");
        } else {
            for (const auto& error : built.errors) {
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s",
                                   error.message.c_str());
            }
        }
        ImGui::BeginDisabled(!built.ok());
        if (ImGui::Button("Create preset", {140.0F, 0.0F})) {
            if (session.create_visual_preset(request)) {
                clear_asset_preview(preview);
                status = "Visual preset created and selected.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Visual preset creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!built.ok(),
                             "Resolve the visual preset build errors before creating it.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create visual composition", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& fields = creation.composition;
        ImGui::TextUnformatted("Create an empty layered visual composition");
        draw_resource_identity_fields(fields.name, fields.id);
        ImGui::InputFloat2("Size (world units)", fields.size);
        ImGui::SetItemTooltip("Canvas size of the composition in project world units.");
        const bool valid = !fields.name.empty() &&
            fabric::core::ResourceId::is_valid(fields.id) &&
            std::isfinite(fields.size[0]) && std::isfinite(fields.size[1]) &&
            fields.size[0] > 0.0F && fields.size[1] > 0.0F;
        if (!valid) {
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                               "Name, id and positive finite size are required.");
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Create composition", {160.0F, 0.0F})) {
            if (session.create_visual_composition(
                    {.value = fields.id}, fields.name,
                    {fields.size[0], fields.size[1]})) {
                clear_asset_preview(preview);
                status = "Visual composition created and selected.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Visual composition creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Enter a valid name, id and positive finite composition size.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create visual component", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& fields = creation.component;
        ImGui::TextUnformatted("Wrap a composition as a reusable component");
        draw_resource_identity_fields(fields.name, fields.id);
        const auto selected_composition = std::ranges::find_if(
            session.resources(), [&](const auto& resource) {
                return resource.kind ==
                           fabric::editor::StudioResourceKind::visual_composition &&
                    resource.id.value == fields.composition_id;
            });
        const char* composition_label =
            selected_composition == session.resources().end()
            ? "Choose a composition..."
            : selected_composition->name.c_str();
        if (ImGui::BeginCombo("Composition", composition_label)) {
            for (const auto& resource : session.resources()) {
                if (resource.kind !=
                    fabric::editor::StudioResourceKind::visual_composition)
                    continue;
                const bool selected = resource.id.value == fields.composition_id;
                if (ImGui::Selectable(resource.name.c_str(), selected))
                    fields.composition_id = resource.id.value;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::InputFloat2("Bounds size (world units)", fields.size);
        ImGui::SetItemTooltip("Reusable component bounds in project world units.");
        const bool valid = !fields.name.empty() &&
            fabric::core::ResourceId::is_valid(fields.id) &&
            fabric::core::ResourceId::is_valid(fields.composition_id) &&
            std::isfinite(fields.size[0]) && std::isfinite(fields.size[1]) &&
            fields.size[0] > 0.0F && fields.size[1] > 0.0F;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Create component", {160.0F, 0.0F})) {
            if (session.create_visual_component(
                    {.value = fields.id}, fields.name,
                    {.value = fields.composition_id},
                    {{-fields.size[0] * 0.5F, -fields.size[1] * 0.5F},
                     {fields.size[0], fields.size[1]}})) {
                clear_asset_preview(preview);
                status = "Visual component created and selected.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Visual component creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Enter a valid component name, id and positive finite size.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create material / fill", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a reusable MaterialDefinition v1");
        ImGui::TextDisabled(
            "The validated material is published atomically in the open project.");
        const auto validation = creation.material.validate(
            session.project_root(), *session.manifest());
        draw_resource_name_field("Name##material-name", creation.material.name);
        focus_prompt_field(validation, "name", "material-create");
        float color[] = {creation.material.color.red,
                         creation.material.color.green,
                         creation.material.color.blue,
                         creation.material.color.alpha};
        if (ImGui::ColorEdit4("Color", color)) {
            creation.material.color = {color[0], color[1], color[2], color[3]};
        }
        float opacity = static_cast<float>(creation.material.opacity);
        if (ImGui::SliderFloat("Opacity (0–1)", &opacity, 0.0F, 1.0F, "%.2f")) {
            creation.material.opacity = opacity;
        }
        focus_prompt_field(validation, "opacity", "material-create");
        const auto blend_label = std::string(
            fabric::project::to_string(creation.material.blend));
        if (ImGui::BeginCombo("Blend", blend_label.c_str())) {
            for (const auto blend : {
                     fabric::project::MaterialBlendMode::normal,
                     fabric::project::MaterialBlendMode::additive,
                     fabric::project::MaterialBlendMode::multiply,
                     fabric::project::MaterialBlendMode::screen}) {
                const bool selected = creation.material.blend == blend;
                const auto option = std::string(fabric::project::to_string(blend));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.material.blend = blend;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        static_cast<void>(draw_project_resource_picker(
            "Texture (optional)##material-texture", session.resources(),
            fabric::editor::StudioResourceKind::texture,
            creation.material.texture_id, true));
        static_cast<void>(draw_project_resource_picker(
            "Vector pattern (optional)##material-vector", session.resources(),
            fabric::editor::StudioResourceKind::vector,
            creation.material.vector_pattern_id, true));
        float uv_position[] = {creation.material.uv_transform.position.x,
                               creation.material.uv_transform.position.y};
        if (ImGui::InputFloat2("UV offset (normalized)", uv_position)) {
            creation.material.uv_transform.position =
                {uv_position[0], uv_position[1]};
        }
        float uv_scale[] = {creation.material.uv_transform.scale.x,
                            creation.material.uv_transform.scale.y};
        if (ImGui::InputFloat2("UV scale (factor)", uv_scale)) {
            creation.material.uv_transform.scale = {uv_scale[0], uv_scale[1]};
        }
        ImGui::InputFloat("UV rotation (degrees)",
                          &creation.material.uv_transform.rotation_degrees,
                          1.0F, 10.0F, "%.2f deg");
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "id");
        draw_prompt_error(validation, "opacity");
        draw_prompt_error(validation, "texture");
        draw_prompt_error(validation, "vectorPattern");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create material", {140.0F, 0.0F})) {
            if (session.create_material(creation.material)) {
                clear_asset_preview(preview);
                status = "Material created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Material creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!validation.ok(),
                             "Complete the material fields and resolve validation errors.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.material.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create entity", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a reusable EntityDefinition v4");
        ImGui::TextDisabled(
            "The validated entity is published atomically in the open project.");
        const auto validation = creation.entity.validate(
            session.project_root(), *session.manifest());
        draw_resource_name_field("Name##entity-name", creation.entity.name);
        focus_prompt_field(validation, "name", "entity-create");
        draw_resource_name_field("Root node name", creation.entity.node_name,
                                 360.0F);
        focus_prompt_field(validation, "node_name", "entity-create");
        const auto drawable_label = std::string(
            fabric::project::to_string(creation.entity.drawable));
        if (ImGui::BeginCombo("Drawable", drawable_label.c_str())) {
            for (const auto drawable : {
                     fabric::project::EntityDrawableKind::none,
                     fabric::project::EntityDrawableKind::vector,
                     fabric::project::EntityDrawableKind::texture,
                     fabric::project::EntityDrawableKind::visual_component}) {
                const bool selected = creation.entity.drawable == drawable;
                const auto option = std::string(fabric::project::to_string(drawable));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    if (creation.entity.drawable != drawable) {
                        creation.entity.resource_id.clear();
                    }
                    creation.entity.drawable = drawable;
                    if (drawable ==
                        fabric::project::EntityDrawableKind::visual_component) {
                        creation.entity.material_id.clear();
                    }
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (creation.entity.drawable !=
            fabric::project::EntityDrawableKind::none) {
            const auto resource_kind = creation.entity.drawable ==
                    fabric::project::EntityDrawableKind::texture
                ? fabric::editor::StudioResourceKind::texture
                : creation.entity.drawable ==
                      fabric::project::EntityDrawableKind::visual_component
                ? fabric::editor::StudioResourceKind::visual_component
                : fabric::editor::StudioResourceKind::vector;
            static_cast<void>(draw_project_resource_picker(
                "Drawable resource", session.resources(), resource_kind,
                creation.entity.resource_id, false));
        } else {
            creation.entity.resource_id.clear();
            ImGui::TextDisabled(
                "Choose a drawable to attach an existing project resource.");
        }
        if (creation.entity.drawable !=
            fabric::project::EntityDrawableKind::visual_component) {
            static_cast<void>(draw_project_resource_picker(
                "Material (optional)", session.resources(),
                fabric::editor::StudioResourceKind::material,
                creation.entity.material_id, true));
        } else {
            ImGui::TextDisabled(
                "The selected visual component owns its composed materials.");
        }
        float position[] = {creation.entity.transform.position.x,
                            creation.entity.transform.position.y};
        if (ImGui::InputFloat2("Position (world units)", position)) {
            creation.entity.transform.position = {position[0], position[1]};
        }
        focus_prompt_field(validation, "transform", "entity-create");
        float scale[] = {creation.entity.transform.scale.x,
                         creation.entity.transform.scale.y};
        if (ImGui::InputFloat2("Scale (factor)", scale)) {
            creation.entity.transform.scale = {scale[0], scale[1]};
        }
        ImGui::SetItemTooltip("Entity scale multiplier on each axis.");
        ImGui::InputFloat("Rotation (degrees)",
                          &creation.entity.transform.rotation_degrees,
                          1.0F, 10.0F, "%.2f deg");
        ImGui::SetItemTooltip("Entity rotation around its pivot, in degrees.");
        ImGui::InputFloat("Z order (world units)", &creation.entity.z_order, 0.1F, 1.0F,
                          "%.2f");
        ImGui::SetItemTooltip("Entity draw order; larger values render later.");
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "node_name");
        draw_prompt_error(validation, "id");
        draw_prompt_error(validation, "resource");
        draw_prompt_error(validation, "material");
        draw_prompt_error(validation, "transform");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create entity", {140.0F, 0.0F})) {
            if (session.create_entity(creation.entity)) {
                clear_asset_preview(preview);
                status = "Entity created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Entity creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!validation.ok(),
                             "Complete the entity fields and resolve validation errors.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.entity.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create animation", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create an AnimationClip v3");
        ImGui::TextDisabled(
            "The validated clip is published atomically in the open project.");
        const auto validation = creation.animation.validate(
            session.project_root(), *session.manifest());
        draw_resource_name_field("Name##animation-name", creation.animation.name);
        focus_prompt_field(validation, "name", "animation-create");
        if (ImGui::Checkbox("Generic clip (no preview entity)",
                            &creation.animation.generic_preview) &&
            creation.animation.generic_preview)
            creation.animation.preview_entity_id.clear();
        if (!creation.animation.generic_preview)
            static_cast<void>(draw_project_resource_picker(
                "Preview entity", session.resources(),
                fabric::editor::StudioResourceKind::entity,
                creation.animation.preview_entity_id, false));
        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputDouble("Duration (seconds)", &creation.animation.duration,
                           0.1, 1.0, "%.2f");
        focus_prompt_field(validation, "duration", "animation-create");
        ImGui::Checkbox("Loop", &creation.animation.loop);
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Marker id (optional)", &creation.animation.marker_id);
        focus_prompt_field(validation, "marker", "animation-create");
        if (!creation.animation.marker_id.empty()) {
            ImGui::SetNextItemWidth(220.0F);
            ImGui::InputDouble("Marker time (seconds)", &creation.animation.marker_time,
                               0.1, 1.0, "%.2f");
            focus_prompt_field(validation, "markerTime", "animation-create");
        }
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "id");
        draw_prompt_error(validation, "duration");
        draw_prompt_error(validation, "previewEntity");
        draw_prompt_error(validation, "marker");
        draw_prompt_error(validation, "markerTime");
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create animation", {140.0F, 0.0F})) {
            if (session.create_animation(creation.animation)) {
                clear_asset_preview(preview);
                status = "Animation created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Animation creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!validation.ok(),
                             "Complete the animation fields and resolve validation errors.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.animation.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create input bindings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create an InputDocument v1");
        ImGui::TextDisabled(
            "Bindings are saved atomically and can be selected by Preview Runtime.");
        const auto validation = creation.input.validate(
            session.project_root(), *session.manifest());
        draw_resource_name_field("Name##input-name", creation.input.name);
        focus_prompt_field(validation, "name", "input-create");
        for (std::size_t action_index = 0;
             action_index < creation.input.actions.size(); ++action_index) {
            auto& action = creation.input.actions[action_index];
            ImGui::PushID(static_cast<int>(action_index));
            ImGui::SeparatorText(("Action " + std::to_string(action_index + 1)).c_str());
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText("Id", &action.id);
        focus_prompt_field(validation,
                          "actions[" + std::to_string(action_index) + "]",
                          "input-create");
            ImGui::SameLine();
            if (ImGui::SmallButton("Duplicate")) {
                auto copy = action;
                copy.id += "_copy";
                creation.input.actions.insert(creation.input.actions.begin() + static_cast<std::ptrdiff_t>(action_index + 1), std::move(copy));
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove") && creation.input.actions.size() > 1) {
                creation.input.actions.erase(creation.input.actions.begin() + static_cast<std::ptrdiff_t>(action_index));
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && action_index > 0) {
                std::swap(creation.input.actions[action_index], creation.input.actions[action_index - 1]);
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && action_index + 1 < creation.input.actions.size()) {
                std::swap(creation.input.actions[action_index], creation.input.actions[action_index + 1]);
                ImGui::PopID();
                break;
            }
            for (std::size_t binding_index = 0;
                 binding_index < action.bindings.size(); ++binding_index) {
                auto& binding = action.bindings[binding_index];
                ImGui::PushID(static_cast<int>(binding_index));
                int device = static_cast<int>(binding.device);
                const char* devices[] = {"keyboard", "gamepad"};
                ImGui::SetNextItemWidth(130.0F);
                if (ImGui::Combo("Device", &device, devices, 2))
                    binding.device = static_cast<fabric::project::InputDevice>(device);
                int binding_kind = static_cast<int>(binding.kind);
                const char* binding_kinds[] = {"button", "axis"};
                ImGui::SameLine();
                if (ImGui::Combo("Kind", &binding_kind, binding_kinds, 2))
                    binding.kind = static_cast<fabric::project::InputBindingKind>(binding_kind);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(130.0F);
                ImGui::InputInt("Code (platform)", &binding.code);
                if (binding.kind == fabric::project::InputBindingKind::axis) {
                    ImGui::InputFloat("Threshold (normalized)", &binding.threshold, 0.05F, 0.1F, "%.2f");
                    ImGui::InputFloat("Dead zone (normalized)", &binding.dead_zone, 0.05F, 0.1F, "%.2f");
                }
                ImGui::TextUnformatted("Modifiers");
                ImGui::SameLine();
                ImGui::Checkbox("Ctrl", &binding.ctrl);
                ImGui::SameLine();
                ImGui::Checkbox("Shift", &binding.shift);
                ImGui::SameLine();
                ImGui::Checkbox("Alt", &binding.alt);
                ImGui::SameLine();
                ImGui::Checkbox("Super", &binding.super);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", input_binding_label(binding).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Capture next")) {
                    creation.input_capture = true;
                    creation.input_capture_existing = false;
                    creation.input_capture_action = action_index;
                    creation.input_capture_binding = binding_index;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Add binding"))
                action.bindings.push_back({fabric::project::InputDevice::keyboard, 0});
            ImGui::PopID();
        }
        if (ImGui::Button("Add action")) {
            std::string id = "action";
            std::size_t suffix = 2;
            while (std::ranges::any_of(creation.input.actions, [&](const auto& item) { return item.id == id; }))
                id = "action_" + std::to_string(suffix++);
            creation.input.actions.push_back({std::move(id), {}});
        }
        if (creation.input_capture)
            ImGui::TextColored({1.0F, 0.8F, 0.2F, 1.0F}, "Press a key or gamepad button…");
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "id");
        for (std::size_t action_index = 0; action_index < creation.input.actions.size(); ++action_index) {
            draw_prompt_error(validation, "actions[" + std::to_string(action_index) + "]");
            draw_prompt_error(validation, "actions[" + std::to_string(action_index) + "].id");
        }
        draw_prompt_summary(validation);
        ImGui::BeginDisabled(!validation.ok());
        if (ImGui::Button("Create input bindings", {180.0F, 0.0F})) {
            if (session.create_input(creation.input)) {
                clear_asset_preview(preview);
                status = "Input bindings created and saved.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Input creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!validation.ok(),
                             "Complete the input binding fields and resolve validation errors.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.input.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (session.has_recovery()) {
        ImGui::OpenPopup("Recover autosave");
    }
    if (ImGui::BeginPopupModal("Recover autosave", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A newer valid autosave is available.");
        ImGui::TextWrapped(
            "Recover it in memory? The saved project will not be overwritten until Save.");
        if (ImGui::Button("Recover", {110.0F, 0.0F})) {
            if (session.accept_recovery()) {
                status = "Autosave recovered; save to keep it.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep saved", {110.0F, 0.0F})) {
            session.decline_recovery();
            status = "Saved project kept.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const auto continue_session_action = [&](const fabric::editor::SessionAction action) {
        switch (action) {
        case fabric::editor::SessionAction::create_project:
            creation.request_project = true;
            break;
        case fabric::editor::SessionAction::open_project:
            request_open = true;
            break;
        case fabric::editor::SessionAction::quit:
            running = false;
            break;
        }
    };
    if (const auto ready = transition_guard.take_ready()) {
        continue_session_action(*ready);
    }
    if (transition_guard.confirmation_required()) {
        ImGui::OpenPopup("Unsaved changes");
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current project has unsaved changes.");
        ImGui::TextWrapped(
            "Save them before replacing the project or closing Asset Studio?");
        if (ImGui::Button("Retry save and continue", {170.0F, 0.0F})) {
            if (session.save() &&
                (!behavior_session.dirty() || behavior_session.save()) &&
                (!transformation_session.dirty() ||
                 transformation_session.save())) {
                status = "Project saved.";
                ImGui::CloseCurrentPopup();
                if (const auto ready = transition_guard.resolve(
                        fabric::editor::UnsavedDecision::save)) {
                    continue_session_action(*ready);
                }
            } else {
                static_cast<void>(transition_guard.resolve(
                    fabric::editor::UnsavedDecision::save, false));
                status = "Save failed; inspect the diagnostics.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {110.0F, 0.0F})) {
            ImGui::CloseCurrentPopup();
            if (const auto ready = transition_guard.resolve(
                    fabric::editor::UnsavedDecision::discard)) {
                continue_session_action(*ready);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            static_cast<void>(transition_guard.resolve(
                fabric::editor::UnsavedDecision::cancel));
            ImGui::CloseCurrentPopup();
            status = "Action cancelled; unsaved changes kept.";
        }
        ImGui::EndPopup();
    }

}

int run_asset_studio(const std::filesystem::path& initial_project,
                     const bool behavior_e2e = false,
                     const bool transformation_e2e = false,
                     const bool entity_e2e = false,
                     const bool animation_e2e = false,
                     const bool texture_e2e = false,
                     const bool vector_e2e = false,
                     const bool vector_canvas_e2e = false,
                     const bool ui_test_mode = false,
                     const bool ui_min_window_test = false,
                     const bool ui_focus_test = false) {
    const bool graphical_test = behavior_e2e || transformation_e2e || entity_e2e ||
        animation_e2e || texture_e2e || vector_e2e || vector_canvas_e2e ||
        ui_test_mode || ui_min_window_test || ui_focus_test;
    const int graphical_failure = graphical_test ? 77 : 1;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return graphical_failure;
    }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const int window_width = ui_min_window_test ? 900 : 1440;
    const int window_height = ui_min_window_test ? 600 : 900;
    SDL_Window* window = SDL_CreateWindow(
        "Vertex Loom - Asset Studio", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, window_width, window_height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
            ((behavior_e2e || transformation_e2e || entity_e2e || animation_e2e ||
              texture_e2e || vector_e2e || vector_canvas_e2e || ui_test_mode ||
              ui_min_window_test || ui_focus_test)
                 ? SDL_WINDOW_HIDDEN : 0U));
    if (window == nullptr) {
        std::cerr << "window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return graphical_failure;
    }
    SDL_SetWindowMinimumSize(window, 900, 600);

    if (NFD_Init() != NFD_OKAY) {
        std::cerr << "native file dialog initialization failed: "
                  << (NFD_GetError() == nullptr ? "unknown error"
                                                : NFD_GetError())
                  << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        std::cerr << "OpenGL context creation failed: " << SDL_GetError() << '\n';
        NFD_Quit();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return graphical_failure;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(
        (behavior_e2e || transformation_e2e || entity_e2e || animation_e2e ||
         texture_e2e || vector_e2e || vector_canvas_e2e || ui_test_mode ||
         ui_min_window_test || ui_focus_test) ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    imgui_io.IniFilename = nullptr;
    apply_studio_style();
    const bool sdl_backend_ready = ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    const bool opengl_backend_ready =
        sdl_backend_ready && ImGui_ImplOpenGL3_Init(glsl_version);
    if (!opengl_backend_ready) {
        std::cerr << "Dear ImGui backend initialization failed\n";
        if (sdl_backend_ready) {
            ImGui_ImplSDL2_Shutdown();
        }
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(gl_context);
        NFD_Quit();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    fabric::editor::ProjectSession session;
    fabric::editor::BehaviorSession behavior_session;
    fabric::editor::TransformationSession transformation_session;
    fabric::render::OpenGLVectorRenderer native_renderer;
    std::unordered_map<std::string, AssetPreview> texture_cache;
    if (!native_renderer.initialize()) {
        std::cerr << "native OpenGL vector renderer initialization failed\n";
    }
    std::array<char, 1024> path_buffer{};
    CreationUiState creation;
    ImportUiState imports;
    AssetPreview preview;
    AssetPreview pending_import_preview;
    CanvasUiState canvas;
    AnimationUiState animation_ui;
    TexturedPathUiState textured_path_ui;
    ProjectSettingsUiState project_settings;
    std::optional<std::pair<std::size_t, fabric::project::EntityDrawableKind>>
        pending_drawable_kind;
    bool request_open = false;
    bool request_png = false;
    bool request_svg = false;
    fabric::editor::SessionTransitionGuard transition_guard;
    std::string status{"Ready"};
    if (!initial_project.empty()) {
        copy_path_to_buffer(initial_project, path_buffer);
        if (session.open(initial_project)) {
            status = "Project opened: " + session.manifest()->name;
        } else {
            status = "Project rejected; inspect the diagnostics.";
        }
    }
    if ((ui_test_mode || ui_min_window_test) && session.has_project()) {
        if (!session.selected_entity()) {
            const auto entity = std::ranges::find_if(
                session.resources(), [](const auto& resource) {
                    return resource.kind ==
                        fabric::editor::StudioResourceKind::entity;
                });
            if (entity != session.resources().end())
                static_cast<void>(session.select_resource(entity->kind,
                                                          entity->id));
        }
        write_ui_test_registry(initial_project, session);
    }
    if (ui_focus_test && session.has_project()) {
        creation.material.name.clear();
        creation.request_material = true;
        ui_focus_probe_enabled = true;
        ui_focus_probe_succeeded = false;
    }
    bool texture_e2e_complete = false;
    if (texture_e2e && session.has_project()) {
        const auto source = initial_project / "assets/textures/head-face.png";
        const fabric::core::ResourceId texture_id{.value = "texture-studio-e2e"};
        const bool imported = std::filesystem::is_regular_file(source) &&
            session.import_png(source, texture_id, "Texture Studio E2E") &&
            session.select_resource(
                fabric::editor::StudioResourceKind::texture, texture_id);
        bool cropped = false;
        if (imported && session.imported_texture()) {
            const auto& texture = session.imported_texture()->asset;
            fabric::project::RasterView view;
            view.crop = {{0.0F, 0.0F},
                         {static_cast<float>(texture.width) * 0.5F,
                          static_cast<float>(texture.height)}};
            view.pivot = {0.5F, 0.5F};
            cropped = session.set_selected_texture_view(view);
        }
        const auto autosave_time =
            fabric::editor::AutosaveScheduler::Clock::now();
        static_cast<void>(session.update_autosave(autosave_time));
        static_cast<void>(session.update_autosave(
            autosave_time + std::chrono::seconds{31}));
        fabric::editor::CreateMaterialPrompt material;
        material.name = "Texture Studio E2E Material";
        const bool created = cropped && session.create_material(material);
        const auto saved_texture = fabric::project::load_texture_asset(
            initial_project, *session.manifest(),
            fabric::project::texture_document_path(*session.manifest(), texture_id));
        const bool view_persisted = saved_texture.ok() &&
            saved_texture.asset->view.has_value() &&
            saved_texture.asset->view->crop.size.x > 0.0F &&
            saved_texture.asset->view->crop.size.x <
                static_cast<float>(saved_texture.asset->width);
        const auto material_resource = std::ranges::find_if(
            session.resources(), [](const auto& resource) {
                return resource.kind ==
                        fabric::editor::StudioResourceKind::material &&
                    resource.name == "Texture Studio E2E Material";
            });
        texture_e2e_complete = created && view_persisted &&
            material_resource != session.resources().end() && !session.dirty();
        if (!texture_e2e_complete)
            std::cerr << "Asset Studio Texture E2E failed\n";
    }
    bool behavior_e2e_complete = false;
    if (behavior_e2e && session.has_project()) {
        fabric::editor::CreateInputPrompt input_prompt;
        input_prompt.name = "Player and Monster Controls";
        input_prompt.actions = {
            {"move", {{fabric::project::InputDevice::keyboard, 65}}},
            {"attack", {{fabric::project::InputDevice::gamepad, 0}}}};
        bool input_authored = session.create_input(input_prompt) &&
            session.add_selected_input_binding(
                0U, {fabric::project::InputDevice::keyboard, 68}) &&
            session.add_selected_input_action(
                {"jump", {{fabric::project::InputDevice::keyboard, 32}}}) &&
            session.save();
        const auto input_resource = std::ranges::find_if(
            session.resources(), [](const auto& resource) {
                return resource.kind == fabric::editor::StudioResourceKind::input &&
                    resource.name == "Player and Monster Controls";
            });
        fabric::editor::ProjectSession input_reloaded;
        const bool input_reloaded_ok = input_authored &&
            input_resource != session.resources().end() &&
            input_reloaded.open(initial_project) &&
            input_reloaded.select_resource(
                fabric::editor::StudioResourceKind::input,
                input_resource->id);
        input_authored = input_reloaded_ok && input_reloaded.selected_input() &&
            input_reloaded.selected_input()->actions.size() == 3U &&
            input_reloaded.selected_input()->actions.front().bindings.size() == 2U;

        fabric::project::BehaviorGraph graph;
        graph.document.id = {.value = "behavior-studio-e2e"};
        graph.document.name = "Behavior Studio E2E";
        const bool authored = behavior_session.create(initial_project, graph) &&
            behavior_session.add_node("ai_source", "monster-ai") &&
            behavior_session.set_node_property(
                {.value = "monster-ai"}, "semantic_id", std::string{"attack"}) &&
            behavior_session.add_node("emit_event", "emit-attack") &&
            behavior_session.connect({.id = "attack-flow",
                .from_node = "monster-ai", .from_port = "out",
                .to_node = "emit-attack", .to_port = "in"}) &&
            behavior_session.save();
        const auto actions = authored ? behavior_session.preview(
            {fabric::runtime::BehaviorSignalSource::ai_decision, "attack", {}},
            1.0F / 60.0F) : std::vector<fabric::runtime::BehaviorAction>{};
        fabric::editor::BehaviorSession reloaded;
        const bool reloaded_ok = authored &&
            reloaded.open(initial_project, {.value = "behavior-studio-e2e"}) &&
            reloaded.graph()->nodes.size() == 2U &&
            reloaded.graph()->connections.size() == 1U;
        const auto entity_resource = std::ranges::find_if(
            session.resources(), [](const auto& resource) {
                return resource.kind == fabric::editor::StudioResourceKind::entity;
            });
        const bool attached = entity_resource != session.resources().end() &&
            session.select_resource(entity_resource->kind, entity_resource->id) &&
            session.set_selected_entity_behavior(
                fabric::project::ResourceReference{
                    {.value = "behavior-studio-e2e"}, "behavior"}) &&
            session.save();
        behavior_e2e_complete = input_authored && reloaded_ok && attached &&
            actions.size() == 1U;
        if (!behavior_e2e_complete)
            std::cerr << "Asset Studio Behavior E2E failed\n";
    }

    bool transformation_e2e_complete = false;
    if (transformation_e2e && session.has_project()) {
        fabric::project::EntityTransformation value;
        value.document.id = {.value = "transformation-studio-e2e"};
        value.document.name = "Transformation Studio E2E";
        value.source_entity = {
            {.value = "rotating-platform-entity"}, "entity"};
        value.destination_entity = {
            {.value = "textile-head-entity"}, "entity"};
        auto policy = value.policy;
        policy.properties = fabric::project::TransferMode::mapping;
        policy.mappings.push_back({fabric::project::TransferDomain::property,
                                   "health", "hit-points"});
        const bool authored = transformation_session.create(
                initial_project, value) &&
            transformation_session.set_policy(policy) &&
            transformation_session.save() && session.refresh_resources() &&
            session.select_resource(
                fabric::editor::StudioResourceKind::transformation,
                value.document.id);
        fabric::editor::TransformationSession reloaded;
        transformation_e2e_complete = authored &&
            reloaded.open(initial_project, value.document.id) &&
            reloaded.transformation()->source_entity == value.source_entity &&
            reloaded.transformation()->destination_entity ==
                value.destination_entity &&
            reloaded.transformation()->policy == policy;
        if (!transformation_e2e_complete)
            std::cerr << "Asset Studio Transformation E2E failed\n";
    }

    bool entity_e2e_complete = false;
    if (entity_e2e && session.has_project()) {
        const fabric::core::ResourceId entity_id{.value =
            "rotating-platform-entity"};
        const bool selected = session.select_resource(
            fabric::editor::StudioResourceKind::entity, entity_id);
        auto node = selected ? session.selected_entity()->nodes.front()
                             : fabric::project::EntityNode{};
        node.visible = false;
        node.locked = true;
        node.drawable = {
            .kind = fabric::project::EntityDrawableKind::texture,
            .resource = fabric::project::ResourceReference{
                {.value = "head-face"}, "texture"}};
        bool authored = selected && session.set_selected_entity_node(0U, node) &&
            session.add_selected_entity_node({
                .id = "studio-child", .name = "Studio Child",
                .parent = node.id});
        if (authored) {
            auto child = session.selected_entity()->nodes[1];
            child.drawable = {
                .kind = fabric::project::EntityDrawableKind::vector,
                .resource = fabric::project::ResourceReference{
                    {.value = "head-button-artwork"}, "vector"}};
            authored = session.set_selected_entity_node(1U, child) &&
                session.duplicate_selected_entity_node(1U);
        }
        if (authored) {
            auto component = session.selected_entity()->nodes[2];
            component.drawable = {
                .kind = fabric::project::EntityDrawableKind::visual_component,
                .resource = fabric::project::ResourceReference{
                    {.value = "beam"}, "visualComponent"},
                .component_instance =
                    fabric::project::VisualComponentInstance{}};
            authored = session.set_selected_entity_node(2U, component) &&
                session.move_selected_entity_node(2U, 1U) && session.save();
        }
        fabric::editor::ProjectSession reloaded;
        const bool reopened = authored && reloaded.open(initial_project) &&
            reloaded.select_resource(
                fabric::editor::StudioResourceKind::entity, entity_id);
        const auto visual = reopened
            ? build_entity_preview(initial_project, *reloaded.manifest(),
                                   *reloaded.selected_entity())
            : EntityPreviewResult{};
        entity_e2e_complete = reopened &&
            reloaded.selected_entity()->nodes.size() == 3U &&
            reloaded.selected_entity()->nodes.front().locked &&
            !reloaded.selected_entity()->nodes.front().visible &&
            reloaded.selected_entity()->nodes.front().drawable.kind ==
                fabric::project::EntityDrawableKind::texture &&
            reloaded.selected_entity()->nodes[1].drawable.kind ==
                fabric::project::EntityDrawableKind::visual_component &&
            reloaded.selected_entity()->nodes[2].drawable.kind ==
                fabric::project::EntityDrawableKind::vector &&
            !visual.packets.empty();
        if (!entity_e2e_complete)
            std::cerr << "Asset Studio Entity E2E failed\n";
    }
    bool entity_gizmo_e2e_active = false;
    std::size_t entity_gizmo_e2e_frame = 0U;
    fabric::core::Vec2 entity_gizmo_e2e_initial_position{};
    if (entity_e2e && entity_e2e_complete && session.selected_entity() &&
        session.selected_entity()->nodes.size() > 1U) {
        canvas.selected_node = 1U;
        entity_gizmo_e2e_initial_position =
            session.selected_entity()->nodes[1].transform.position;
        entity_gizmo_e2e_active = true;
    }

    bool animation_e2e_complete = false;
    if (animation_e2e && session.has_project()) {
        fabric::editor::CreateAnimationPrompt prompt;
        prompt.name = "Targeted Animation E2E";
        prompt.preview_entity_id = "textile-head-entity";
        prompt.duration = 2.0;
        const bool authored = session.create_animation(prompt) &&
            session.set_selected_animation_preview_entity(std::nullopt) &&
            session.undo() && session.save();
        fabric::editor::ProjectSession reloaded;
        const bool reopened = authored && reloaded.open(initial_project) &&
            reloaded.select_resource(
                fabric::editor::StudioResourceKind::animation,
                {.value = "targeted-animation-e2e"});
        const auto visual = reopened
            ? build_entity_preview(reloaded)
            : EntityPreviewResult{};
        animation_e2e_complete = reopened &&
            reloaded.selected_animation()->preview_entity &&
            reloaded.selected_animation()->preview_entity->id.value ==
                "textile-head-entity" &&
            reloaded.selected_entity() && !visual.packets.empty();
        if (!animation_e2e_complete)
            std::cerr << "Asset Studio Animation E2E failed\n";
    }

    bool vector_e2e_complete = false;
    if (vector_e2e && session.has_project()) {
        const fabric::core::ResourceId vector_id{.value =
            "head-button-artwork"};
        bool authored = session.select_resource(
            fabric::editor::StudioResourceKind::vector, vector_id) &&
            session.created_vector() && session.created_vector()->native &&
            !session.created_vector()->native->nodes.empty();
        if (authored) {
            auto node = session.created_vector()->native->nodes.front();
            const auto converted = fabric::project::path_commands_from_shape(
                node.shape);
            authored = converted.has_value() && converted->size() >= 2U;
            if (authored) {
                node.shape.kind = fabric::project::VectorShapeKind::path;
                node.shape.path = *converted;
                if (node.shape.path.size() > 1U) {
                    const auto inserted = fabric::project::insert_path_command(
                        node.shape, 1U,
                        {.kind = fabric::project::VectorPathCommandKind::line,
                         .point = {0.25F, 0.25F}});
                    authored = inserted &&
                        fabric::project::remove_path_command(node.shape, 1U);
                }
                if (authored && node.shape.path.size() > 1U) {
                    authored = fabric::project::convert_path_command(
                        node.shape, 1U,
                        fabric::project::VectorPathCommandKind::cubic);
                    if (authored)
                        authored = fabric::editor::update_bezier_handle(
                            node.shape, 1U, true, {0.15F, 0.15F},
                            fabric::editor::BezierHandleMode::linked);
                }
                authored = authored &&
                    session.set_selected_vector_node(0U, node) &&
                    session.undo() && session.redo() && session.save();
            }
        }
        fabric::editor::ProjectSession reloaded;
        const bool reopened = authored && reloaded.open(initial_project) &&
            reloaded.select_resource(
                fabric::editor::StudioResourceKind::vector, vector_id) &&
            reloaded.created_vector() && reloaded.created_vector()->native &&
            !reloaded.created_vector()->native->nodes.empty();
        vector_e2e_complete = reopened &&
            reloaded.created_vector()->native->nodes.front().shape.kind ==
                fabric::project::VectorShapeKind::path &&
            reloaded.created_vector()->native->nodes.front().shape.path.size() >=
                2U;
        if (!vector_e2e_complete)
            std::cerr << "Asset Studio Vector E2E failed\n";
    }
    bool vector_canvas_e2e_complete = false;
    std::size_t vector_canvas_e2e_frame = 0U;
    std::size_t vector_canvas_e2e_initial_path_size = 0U;
    fabric::core::Vec2 vector_canvas_e2e_initial_anchor{};
    fabric::core::Vec2 vector_canvas_e2e_initial_control1{};
    if (vector_canvas_e2e && session.has_project()) {
        const fabric::core::ResourceId vector_id{.value =
            "head-button-artwork"};
        const bool selected = session.select_resource(
            fabric::editor::StudioResourceKind::vector, vector_id);
        if (selected && session.created_vector() &&
            session.created_vector()->native &&
            !session.created_vector()->native->nodes.empty()) {
            auto node = session.created_vector()->native->nodes.front();
            const auto converted = fabric::project::path_commands_from_shape(
                node.shape);
            if (converted) {
                node.shape.kind = fabric::project::VectorShapeKind::path;
                node.shape.path = *converted;
                if (node.shape.path.size() > 1U)
                    static_cast<void>(fabric::project::convert_path_command(
                        node.shape, 1U,
                        fabric::project::VectorPathCommandKind::cubic));
                vector_canvas_e2e_initial_path_size = node.shape.path.size();
                if (node.shape.path.size() > 1U)
                    vector_canvas_e2e_initial_anchor = node.shape.path[1].point;
                if (node.shape.path.size() > 1U)
                    vector_canvas_e2e_initial_control1 =
                        node.shape.path[1].control1;
                vector_canvas_e2e_complete =
                    session.set_selected_vector_node(0U, std::move(node));
                canvas.selected_node = 0U;
                canvas.tool = CanvasUiState::Tool::pen;
            }
        }
    }

    bool running = true;
    std::size_t ui_test_frame = 0U;
    const auto dirty = [&] {
        return session.dirty() || behavior_session.dirty() ||
            transformation_session.dirty();
    };
    const auto save_all = [&] {
        return session.save() &&
            (!behavior_session.dirty() || behavior_session.save()) &&
            (!transformation_session.dirty() || transformation_session.save());
    };
    while (running) {
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 6U)
            canvas.tool = CanvasUiState::Tool::move;
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 9U) {
            canvas.tool = CanvasUiState::Tool::move;
            canvas.bezier_handle_mode = fabric::editor::BezierHandleMode::free;
        }
        const bool pen_click = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 2U && vector_canvas_e2e_frame < 6U;
        const bool move_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 6U && vector_canvas_e2e_frame < 9U;
        const bool handle_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 9U && vector_canvas_e2e_frame < 12U;
        if (pen_click || move_gesture || handle_gesture) {
            const auto frame = vector_canvas_e2e_frame;
            const bool button_event = pen_click || frame == 6U || frame == 8U ||
                frame == 9U || frame == 11U;
            const bool button_down = pen_click
                ? frame % 2U == 0U
                : (move_gesture ? frame == 6U : frame == 9U);
            const bool right_click = frame == 4U || frame == 5U;
            const auto canvas_point = [&](const fabric::core::Vec2 point) {
                const auto& node = session.created_vector()->native->nodes.front();
                const auto transformed = fabric::core::Transform{
                    .position = node.transform.position,
                    .rotation_degrees = node.transform.rotation_degrees,
                    .scale = node.transform.scale,
                    .pivot = node.transform.pivot};
                const auto local = fabric::core::Vec2{
                    (point.x - transformed.pivot.x) * transformed.scale.x,
                    (point.y - transformed.pivot.y) * transformed.scale.y};
                const float angle = transformed.rotation_degrees *
                    std::numbers::pi_v<float> / 180.0F;
                const fabric::core::Vec2 world{
                    transformed.position.x + transformed.pivot.x +
                        local.x * std::cos(angle) - local.y * std::sin(angle),
                    transformed.position.y + transformed.pivot.y +
                        local.x * std::sin(angle) + local.y * std::cos(angle)};
                const auto& native = *session.created_vector()->native;
                const float fit = std::min(
                    (canvas.native_size.x - 80.0F) / native.size.x,
                    (canvas.native_size.y - 80.0F) / native.size.y);
                const float pixels_per_unit = fit * canvas.zoom;
                const ImVec2 center{
                    canvas.native_origin.x + canvas.native_size.x * 0.5F,
                    canvas.native_origin.y + canvas.native_size.y * 0.5F};
                return ImVec2{
                    center.x + canvas.pan.x + world.x * pixels_per_unit,
                    center.y + canvas.pan.y - world.y * pixels_per_unit};
            };
            const auto& test_node = session.created_vector()->native->nodes.front();
            const auto& test_path = test_node.shape.path;
            const auto inserted_index = !canvas.selected_path_points.empty()
                ? canvas.selected_path_points.front() : 1U;
            const auto test_point = pen_click && test_path.size() > 1U
                ? (frame >= 4U && inserted_index < test_path.size()
                       ? test_path[inserted_index].point
                       : fabric::core::Vec2{
                             (test_path[0].point.x + test_path[1].point.x) *
                                 0.5F,
                             (test_path[0].point.y + test_path[1].point.y) *
                                 0.5F})
                : move_gesture && test_path.size() > 1U
                ? fabric::core::Vec2{
                      vector_canvas_e2e_initial_anchor.x +
                          (frame == 6U ? 0.0F : 0.12F),
                      vector_canvas_e2e_initial_anchor.y +
                          (frame == 6U ? 0.0F : 0.08F)}
                : handle_gesture && test_path.size() > 1U
                ? fabric::core::Vec2{
                      vector_canvas_e2e_initial_control1.x +
                          (frame == 9U ? 0.0F : 0.12F),
                      vector_canvas_e2e_initial_control1.y +
                          (frame == 9U ? 0.0F : 0.12F)}
                : fabric::core::Vec2{0.0F, 0.0F};
            const auto mouse = canvas_point(test_point);
            const int mouse_x = static_cast<int>(std::lround(mouse.x));
            const int mouse_y = static_cast<int>(std::lround(mouse.y));
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.state = move_gesture || handle_gesture
                ? SDL_BUTTON_LMASK : 0U;
            motion.motion.x = mouse_x;
            motion.motion.y = mouse_y;
            static_cast<void>(SDL_PushEvent(&motion));
            if (button_event) {
                SDL_Event event{};
                event.type = button_down ? SDL_MOUSEBUTTONDOWN
                                         : SDL_MOUSEBUTTONUP;
                event.button.button = right_click
                    ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                event.button.windowID = SDL_GetWindowID(window);
                event.button.x = mouse_x;
                event.button.y = mouse_y;
                static_cast<void>(SDL_PushEvent(&event));
            }
        }
        if (entity_gizmo_e2e_active && entity_gizmo_e2e_frame >= 1U &&
            entity_gizmo_e2e_frame < 4U) {
            const auto start = canvas.entity_gizmo_screen;
            const auto moved = entity_gizmo_e2e_frame >= 2U;
            const int mouse_x = static_cast<int>(start.x) + (moved ? 36 : 0);
            const int mouse_y = static_cast<int>(start.y);
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.x = mouse_x;
            motion.motion.y = mouse_y;
            static_cast<void>(SDL_PushEvent(&motion));
            SDL_Event button{};
            button.type = entity_gizmo_e2e_frame == 3U
                ? SDL_MOUSEBUTTONUP : SDL_MOUSEBUTTONDOWN;
            button.button.button = SDL_BUTTON_LEFT;
            button.button.windowID = SDL_GetWindowID(window);
            button.button.x = mouse_x;
            button.button.y = mouse_y;
            if (entity_gizmo_e2e_frame != 2U)
                static_cast<void>(SDL_PushEvent(&button));
        }
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (creation.input_capture) {
                const auto apply_capture = [&](const fabric::project::InputBinding binding) {
                    if (creation.input_capture_existing) {
                        if (session.selected_input())
                            static_cast<void>(session.set_selected_input_binding(
                                creation.input_capture_action,
                                creation.input_capture_binding, binding));
                    } else if (creation.input_capture_action < creation.input.actions.size() &&
                               creation.input_capture_binding < creation.input.actions[creation.input_capture_action].bindings.size()) {
                        creation.input.actions[creation.input_capture_action].bindings[creation.input_capture_binding] = binding;
                    }
                    creation.input_capture = false;
                };
                if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                    fabric::project::InputBinding binding{
                        fabric::project::InputDevice::keyboard,
                        static_cast<int>(event.key.keysym.sym)};
                    binding.ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
                    binding.shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                    binding.alt = (event.key.keysym.mod & KMOD_ALT) != 0;
                    binding.super = (event.key.keysym.mod & KMOD_GUI) != 0;
                    apply_capture(binding);
                    continue;
                }
                if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    apply_capture({fabric::project::InputDevice::gamepad, static_cast<int>(event.cbutton.button)});
                    continue;
                }
            }
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                transition_guard.request(fabric::editor::SessionAction::quit,
                                         dirty());
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New project...", new_shortcut)) {
                    transition_guard.request(
                        fabric::editor::SessionAction::create_project,
                        dirty());
                }
                if (ImGui::MenuItem("Open project...", open_shortcut)) {
                    if (session.has_project()) {
                        copy_path_to_buffer(session.project_root(), path_buffer);
                    }
                    transition_guard.request(
                        fabric::editor::SessionAction::open_project,
                        dirty());
                }
                if (ImGui::MenuItem("Save", save_shortcut, false,
                                    session.has_project())) {
                    status = save_all()
                        ? "Project saved."
                        : "Save failed; inspect the diagnostics.";
                }
                if (ImGui::MenuItem("Project settings...", nullptr, false,
                                    session.has_project())) {
                    project_settings.request = true;
                }
                if (ImGui::BeginMenu("Create", session.has_project())) {
                    if (ImGui::MenuItem("Vector artwork...")) {
                        creation.request_artwork = true;
                    }
                    if (ImGui::MenuItem("Behavior graph...")) {
                        creation.request_behavior = true;
                    }
                    if (ImGui::MenuItem("Entity transformation...")) {
                        creation.request_transformation = true;
                    }
                    if (ImGui::MenuItem("Visual preset...")) {
                        creation.request_visual_preset = true;
                    }
                    if (ImGui::MenuItem("Visual composition...")) {
                        creation.request_visual_composition = true;
                    }
                    if (ImGui::MenuItem("Visual component...")) {
                        creation.request_visual_component = true;
                    }
                    if (ImGui::MenuItem("Material / fill...")) {
                        creation.request_material = true;
                    }
                    if (ImGui::MenuItem("Entity...")) {
                        creation.request_entity = true;
                    }
                    if (ImGui::MenuItem("Animation...")) {
                        creation.request_animation = true;
                    }
                    if (ImGui::MenuItem("Input bindings...")) {
                        creation.request_input = true;
                    }
                    if (ImGui::MenuItem("Add existing resource...")) {
                        ImGui::OpenPopup("Add existing resource");
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Import", session.has_project())) {
                    if (ImGui::MenuItem("PNG image source...", import_shortcut)) {
                        request_png = true;
                    }
                    if (ImGui::MenuItem("Linked SVG source...",
                                        import_svg_shortcut)) {
                        request_svg = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", quit_shortcut)) {
                    transition_guard.request(fabric::editor::SessionAction::quit,
                                             dirty());
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", undo_shortcut, false,
                                    session.can_undo())) {
                    static_cast<void>(session.undo());
                    status = "Change undone.";
                }
                if (ImGui::MenuItem("Redo", redo_shortcut, false,
                                    session.can_redo())) {
                    static_cast<void>(session.redo());
                    status = "Change redone.";
                }
                ImGui::EndMenu();
            }
            ImGui::TextDisabled("Native vector resource workspace");
            ImGui::EndMainMenuBar();
        }
        const auto& io = ImGui::GetIO();
        #if defined(__APPLE__)
        const bool command_modifier = io.KeySuper;
        #else
        const bool command_modifier = io.KeyCtrl;
        #endif
        const bool shortcuts_enabled =
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            transition_guard.request(fabric::editor::SessionAction::open_project,
                                     dirty());
        }
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            transition_guard.request(
                fabric::editor::SessionAction::create_project,
                dirty());
        }
        if (shortcuts_enabled && command_modifier && session.has_project() &&
            ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            if (io.KeyShift) {
                request_svg = true;
            } else {
                request_png = true;
            }
        }
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            transition_guard.request(fabric::editor::SessionAction::quit,
                                     dirty());
        }
        if (shortcuts_enabled && command_modifier && session.has_project() &&
            ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            status = save_all()
                ? "Project saved."
                : "Save failed; inspect the diagnostics.";
        }
        if (shortcuts_enabled && command_modifier && !io.KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_Z, false) && session.can_undo()) {
            static_cast<void>(session.undo());
            status = "Change undone.";
        }
        if (shortcuts_enabled && command_modifier &&
            ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) ||
             ImGui::IsKeyPressed(ImGuiKey_Y, false)) && session.can_redo()) {
            static_cast<void>(session.redo());
            status = "Change redone.";
        }

        EntityPreviewResult entity_preview;
        const bool entity_selection = session.selected_resource() != nullptr &&
            (session.selected_resource()->kind ==
                 fabric::editor::StudioResourceKind::entity ||
             session.selected_resource()->kind ==
                 fabric::editor::StudioResourceKind::animation) &&
            session.selected_entity();
        if (entity_selection) {
            const auto* animation =
                session.selected_resource()->kind ==
                        fabric::editor::StudioResourceKind::animation &&
                    session.selected_animation()
                ? &*session.selected_animation()
                : nullptr;
            entity_preview = build_entity_preview(
                session, animation, animation_ui.scrub_time);
            canvas.entity_world_bounds = entity_preview.bounds;
        }
        if (textured_path_ui.animate_texture &&
            session.selected_textured_path()) {
            textured_path_ui.preview_offset +=
                ImGui::GetIO().DeltaTime * textured_path_ui.scroll_speed;
        }
        const auto visual_preview = build_visual_preview(
            session, textured_path_ui);
        const bool visual_selection = session.selected_resource() != nullptr &&
            (session.selected_resource()->kind ==
                 fabric::editor::StudioResourceKind::textured_path ||
             session.selected_resource()->kind ==
                 fabric::editor::StudioResourceKind::visual_composition ||
             session.selected_resource()->kind ==
                 fabric::editor::StudioResourceKind::visual_component);
        if (visual_selection) {
            canvas.entity_world_bounds = visual_preview.bounds;
        }

        draw_workspace(session, behavior_session, transformation_session,
                       window, path_buffer, creation, imports, preview,
                       pending_import_preview, texture_cache, canvas, entity_preview,
                       visual_preview,
                       animation_ui, textured_path_ui,
                       project_settings,
                       pending_drawable_kind,
                       request_open, request_png, request_svg,
                       transition_guard, running, status);
        draw_behavior_editor(session, behavior_session, creation, status);
        draw_transformation_editor(session, transformation_session, creation,
                                   status);

        const auto* active_resource = session.selected_resource();
        if (behavior_session.dirty() && behavior_session.graph() &&
            (!active_resource ||
             active_resource->kind != fabric::editor::StudioResourceKind::behavior ||
             active_resource->id != behavior_session.graph()->document.id)) {
            status = behavior_session.save()
                ? "Previous behavior saved automatically."
                : "Behavior autosave transition failed.";
        }
        if (transformation_session.dirty() &&
            transformation_session.transformation() &&
            (!active_resource ||
             active_resource->kind !=
                 fabric::editor::StudioResourceKind::transformation ||
             active_resource->id !=
                 transformation_session.transformation()->document.id)) {
            status = transformation_session.save()
                ? "Previous transformation saved automatically."
                : "Transformation autosave transition failed.";
        }

        const auto autosave_status = session.update_autosave();
        if (autosave_status == fabric::editor::AutosaveStatus::saved) {
            status = "Recovery autosave updated.";
        } else if (autosave_status == fabric::editor::AutosaveStatus::failed) {
            status = "Autosave failed; inspect the diagnostics.";
        }
        const auto behavior_autosave = behavior_session.update_autosave();
        if (behavior_autosave == fabric::editor::AutosaveStatus::saved)
            status = "Behavior recovery autosave updated.";
        else if (behavior_autosave == fabric::editor::AutosaveStatus::failed)
            status = "Behavior autosave failed; inspect diagnostics.";
        const auto transformation_autosave =
            transformation_session.update_autosave();
        if (transformation_autosave == fabric::editor::AutosaveStatus::saved)
            status = "Transformation recovery autosave updated.";
        else if (transformation_autosave ==
                 fabric::editor::AutosaveStatus::failed)
            status = "Transformation autosave failed; inspect diagnostics.";

        ImGui::Render();
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        glViewport(0, 0, drawable_width, drawable_height);
        glClearColor(0.035F, 0.041F, 0.052F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const bool render_native_vector = canvas.native_canvas &&
            session.created_vector() && session.created_vector()->native;
        const bool render_entity = canvas.native_canvas && entity_selection &&
            !entity_preview.packets.empty();
        const bool render_visual = canvas.native_canvas && visual_selection &&
            !visual_preview.packets.empty();
        if (render_native_vector || render_entity || render_visual) {
            const auto native_packets = render_native_vector
                ? fabric::render::build_native_draw_packets(*session.created_vector())
                : fabric::render::VectorGeometryResult{};
            const auto packets = render_native_vector
                ? std::span<const fabric::render::VectorDrawPacket>(native_packets.packets)
                : render_visual
                ? std::span<const fabric::render::VectorDrawPacket>(
                      visual_preview.packets)
                : std::span<const fabric::render::VectorDrawPacket>(
                      entity_preview.packets);
            const auto display_size = ImGui::GetIO().DisplaySize;
            const float scale_x = display_size.x > 0.0F
                ? static_cast<float>(drawable_width) / display_size.x
                : 1.0F;
            const float scale_y = display_size.y > 0.0F
                ? static_cast<float>(drawable_height) / display_size.y
                : 1.0F;
            const auto native_viewport = fabric::render::OpenGLVectorViewport{
                .width = std::max(1, static_cast<std::int32_t>(
                    canvas.native_size.x * scale_x)),
                .height = std::max(1, static_cast<std::int32_t>(
                    canvas.native_size.y * scale_y)),
                .world_bounds = canvas.native_world_bounds,
                .x = std::max(0, static_cast<std::int32_t>(
                    canvas.native_origin.x * scale_x)),
                .y = std::max(0, drawable_height - static_cast<std::int32_t>(
                    (canvas.native_origin.y + canvas.native_size.y) * scale_y)),
            };
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_BLEND);
            const fabric::render::OpenGLTextureResolver texture_resolver =
                [&](const fabric::core::ResourceId& id)
                -> std::optional<fabric::render::OpenGLTextureHandle> {
                const auto cached = texture_cache.find(id.value);
                if (cached != texture_cache.end() &&
                    cached->second.texture != 0U) {
                    return fabric::render::OpenGLTextureHandle{
                        .handle = cached->second.texture,
                        .width = cached->second.width,
                        .height = cached->second.height,
                    };
                }
                if (!session.manifest()) return std::nullopt;
                const auto loaded = fabric::project::load_texture_asset(
                    session.project_root(), *session.manifest(),
                    fabric::project::texture_document_path(*session.manifest(), id));
                if (!loaded.ok()) return std::nullopt;
                const auto decoded = fabric::render::load_png(
                    session.project_root() / loaded.asset->source);
                if (!decoded.ok()) return std::nullopt;
                AssetPreview preview_texture;
                upload_preview(preview_texture, *decoded.image);
                const auto [inserted, _] = texture_cache.emplace(
                    id.value, std::move(preview_texture));
                return fabric::render::OpenGLTextureHandle{
                    .handle = inserted->second.texture,
                    .width = inserted->second.width,
                    .height = inserted->second.height,
                };
            };
            static_cast<void>(native_renderer.draw(
                packets, native_viewport, texture_resolver));
        }
        glViewport(0, 0, drawable_width, drawable_height);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (ui_test_mode || ui_min_window_test || ui_focus_test)
            write_frame_capture(initial_project, window,
                                "asset_studio-ui-test.ppm");
        SDL_GL_SwapWindow(window);
        if ((ui_test_mode || ui_min_window_test) && ++ui_test_frame >= 1U)
            running = false;
        if (ui_focus_test && ++ui_test_frame >= 3U) {
            write_ui_focus_probe(initial_project, ui_focus_probe_succeeded);
            running = false;
        }
        if (vector_canvas_e2e) {
            ++vector_canvas_e2e_frame;
            if (vector_canvas_e2e_frame == 4U && session.created_vector()) {
                vector_canvas_e2e_complete =
                    vector_canvas_e2e_complete &&
                    session.created_vector()->native &&
                    session.created_vector()->native->nodes.front().shape.path.size() ==
                        vector_canvas_e2e_initial_path_size + 1U;
            } else if (vector_canvas_e2e_frame == 6U && session.created_vector()) {
                vector_canvas_e2e_complete =
                    vector_canvas_e2e_complete &&
                    session.created_vector()->native &&
                    session.created_vector()->native->nodes.front().shape.path.size() ==
                        vector_canvas_e2e_initial_path_size;
            } else if (vector_canvas_e2e_frame == 12U) {
                const bool saved = session.save();
                fabric::editor::ProjectSession reloaded;
                const auto reloaded_ok = saved && reloaded.open(initial_project) &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::vector,
                        {.value = "head-button-artwork"}) &&
                    reloaded.created_vector() && reloaded.created_vector()->native &&
                    !reloaded.created_vector()->native->nodes.empty();
                vector_canvas_e2e_complete = vector_canvas_e2e_complete &&
                    reloaded_ok &&
                    reloaded.created_vector()->native->nodes.front().shape.path.size() ==
                        vector_canvas_e2e_initial_path_size &&
                    reloaded.created_vector()->native->nodes.front().shape.path[1].point.x !=
                        vector_canvas_e2e_initial_anchor.x &&
                    reloaded.created_vector()->native->nodes.front().shape.path[1].control1.x !=
                        vector_canvas_e2e_initial_control1.x;
                if (!vector_canvas_e2e_complete) {
                    const auto current_size = session.created_vector() &&
                            session.created_vector()->native &&
                            !session.created_vector()->native->nodes.empty()
                        ? session.created_vector()->native->nodes.front().shape.path.size()
                        : 0U;
                    std::cerr << "Asset Studio Vector Canvas E2E failed: initial="
                              << vector_canvas_e2e_initial_path_size
                              << " current=" << current_size
                              << " frame=" << vector_canvas_e2e_frame << '\n';
                }
                running = false;
            }
        }
        if (entity_gizmo_e2e_active) {
            ++entity_gizmo_e2e_frame;
            if (entity_gizmo_e2e_frame == 4U) {
                const bool saved = session.save();
                fabric::editor::ProjectSession reloaded;
                const bool reopened = saved && reloaded.open(initial_project) &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::entity,
                        {.value = "rotating-platform-entity"});
                entity_e2e_complete = entity_e2e_complete && reopened &&
                    reloaded.selected_entity()->nodes.size() > 1U &&
                    reloaded.selected_entity()->nodes[1].transform.position.x !=
                        entity_gizmo_e2e_initial_position.x;
                if (!entity_e2e_complete)
                    std::cerr << "Asset Studio Entity Gizmo E2E failed: initial="
                              << entity_gizmo_e2e_initial_position.x << "\n";
                running = false;
            }
        }
        if (behavior_e2e || transformation_e2e || entity_e2e || animation_e2e ||
            texture_e2e || vector_e2e)
            running = running && entity_gizmo_e2e_active;
    }

    const bool e2e_failed =
        (behavior_e2e && !behavior_e2e_complete) ||
        (transformation_e2e && !transformation_e2e_complete) ||
        (entity_e2e && !entity_e2e_complete) ||
        (animation_e2e && !animation_e2e_complete) ||
        (texture_e2e && !texture_e2e_complete) ||
        (vector_e2e && !vector_e2e_complete) ||
        (vector_canvas_e2e && !vector_canvas_e2e_complete);
    if (e2e_failed)
        write_e2e_failure_artifacts(initial_project, window, status, session);

    if (preview.texture != 0U) {
        glDeleteTextures(1, &preview.texture);
    }
    if (pending_import_preview.texture != 0U) {
        glDeleteTextures(1, &pending_import_preview.texture);
    }
    for (auto& [_, texture] : texture_cache) {
        clear_asset_preview(texture);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    native_renderer.shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    NFD_Quit();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return e2e_failed ? 1 : 0;
}

} // namespace

int main(const int argument_count, char** arguments) {
    const bool behavior_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-behavior";
    const bool transformation_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-transformation";
    const bool entity_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-entity";
    const bool animation_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-animation";
    const bool texture_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-texture";
    const bool vector_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-vector";
    const bool vector_canvas_e2e = argument_count == 3 &&
        std::string_view{arguments[1]} == "--e2e-vector-canvas";
    const bool ui_test_mode = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-test";
    const bool ui_min_window_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-test-min-window";
    const bool ui_focus_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-focus-test";
    if (argument_count > 2 && !behavior_e2e && !transformation_e2e &&
        !entity_e2e && !animation_e2e && !texture_e2e && !vector_e2e &&
        !vector_canvas_e2e && !ui_test_mode && !ui_min_window_test &&
        !ui_focus_test) {
        std::cerr << "usage: asset_studio [project-directory]\n"
                     "       asset_studio --e2e-behavior project-directory\n"
                     "       asset_studio --e2e-transformation project-directory\n"
                     "       asset_studio --e2e-entity project-directory\n"
                     "       asset_studio --e2e-animation project-directory\n"
                     "       asset_studio --e2e-texture project-directory\n"
                     "       asset_studio --e2e-vector project-directory\n"
                     "       asset_studio --e2e-vector-canvas project-directory\n"
                     "       asset_studio --ui-test project-directory\n"
                     "       asset_studio --ui-test-min-window project-directory\n"
                     "       asset_studio --ui-focus-test project-directory\n";
        return 64;
    }
    const std::filesystem::path initial_project =
        (behavior_e2e || transformation_e2e || entity_e2e || animation_e2e ||
        texture_e2e || vector_e2e || vector_canvas_e2e || ui_test_mode ||
        ui_min_window_test || ui_focus_test)
        ? std::filesystem::path{arguments[2]}
        : argument_count == 2 ? std::filesystem::path{arguments[1]}
                            : std::filesystem::path{};
    return run_asset_studio(initial_project, behavior_e2e, transformation_e2e,
                            entity_e2e, animation_e2e, texture_e2e, vector_e2e,
                            vector_canvas_e2e, ui_test_mode, ui_min_window_test,
                            ui_focus_test);
}
