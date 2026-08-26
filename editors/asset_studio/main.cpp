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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <filesystem>
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
    bool request{};
};

struct CanvasUiState {
    enum class Tool {
        move,
        rotate,
        scale,
        pivot,
    };

    enum class DragOperation {
        none,
        move,
        rotate,
        scale,
        pivot,
    };

    float zoom{1.0F};
    ImVec2 pan{};
    std::size_t selected_node{};
    bool native_canvas{};
    Tool tool{Tool::move};
    bool dragging{};
    DragOperation drag_operation{DragOperation::none};
    ImVec2 drag_start_mouse{};
    fabric::core::Transform drag_start_transform;
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
    bool key_boolean{};
    bool auto_key{};
    std::string key_resource_id;
    fabric::project::AnimationInterpolation interpolation{
        fabric::project::AnimationInterpolation::linear};
    fabric::project::AnimationComposition composition{
        fabric::project::AnimationComposition::replace};
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

bool duplicate_project_resource(
    fabric::editor::ProjectSession& session,
    const fabric::editor::StudioResource& resource,
    AssetPreview& preview, std::string& status) {
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
                                    {.value = candidate}, copy_name)) {
        status = "Resource duplication failed; inspect diagnostics.";
        return false;
    }
    const auto* selected = session.selected_resource();
    return selected != nullptr &&
        select_and_preview_resource(session, *selected, preview, status,
                                    "Duplicated: ");
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
        ImGui::TextWrapped("Create or open a Vertex Loom project to begin.");
        return;
    }

    static ImGuiTextFilter filter;
    static std::optional<fabric::editor::StudioResource> delete_request;
    static std::vector<fabric::editor::StudioResource> delete_impact;
    static std::optional<fabric::editor::StudioResource> rename_request;
    static std::string rename_value;
    bool open_delete_popup = false;
    bool open_rename_popup = false;
    const auto request_delete = [&](const fabric::editor::StudioResource& resource) {
        const auto incoming = session.incoming_references(
            resource.kind, resource.id);
        if (!incoming) {
            status = "Reference analysis failed; inspect diagnostics.";
            return;
        }
        delete_request = resource;
        delete_impact = *incoming;
        open_delete_popup = true;
    };
    const auto request_rename = [&](const fabric::editor::StudioResource& resource) {
        rename_request = resource;
        rename_value = resource.name;
        open_rename_popup = true;
    };
    filter.Draw("Search", -1.0F);
    static int kind_filter{};
    const char* kind_filters[] = {
        "All types", "Textures", "Vector artworks", "Materials / fills",
        "Entities", "Animations", "Input bindings", "Behaviors",
        "Transformations", "Textured paths", "Visual compositions",
        "Visual components", "Maps", "Scenes", "Mechanics", "Replays"};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("##resource-kind-filter", &kind_filter, kind_filters,
                 static_cast<int>(std::size(kind_filters)));
    if (const auto* selected = session.selected_resource()) {
        if (ImGui::Button("Duplicate")) {
            const auto resource = *selected;
            duplicate_project_resource(session, resource, preview, status);
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename...")) request_rename(*selected);
        ImGui::SameLine();
        if (ImGui::Button("Copy ID")) {
            SDL_SetClipboardText(selected->id.value.c_str());
            status = "Resource ID copied.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy path")) {
            const auto path = selected->document_path.generic_string();
            SDL_SetClipboardText(path.c_str());
            status = "Resource path copied.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reveal"))
            reveal_project_resource(session, *selected, status);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.55F, 0.16F, 0.15F, 1.0F});
        if (ImGui::Button("Delete...")) request_delete(*selected);
        ImGui::PopStyleColor();
        if (session.can_restore_trashed_resource()) {
            ImGui::SameLine();
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
            const std::string item_label = resource.name + "##" +
                resource.id.value;
            if (ImGui::Selectable(item_label.c_str(), is_selected)) {
                select_and_preview_resource(session, resource, preview, status);
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Duplicate")) {
                    duplicate_request = resource;
                }
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
    draw_kind("Audio", fabric::editor::StudioResourceKind::audio, 17);
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

    if (open_delete_popup) ImGui::OpenPopup("Delete resource");
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
                    ImGui::CloseCurrentPopup();
                } else {
                    status = "Delete failed; inspect diagnostics.";
                }
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100.0F, 0.0F})) {
                delete_request.reset();
                delete_impact.clear();
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
            ImGui::SetNextItemWidth(420.0F);
            ImGui::InputText("Visible name", &rename_value);
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
        ImGui::TextWrapped("%s", error.message.c_str());
        ImGui::Separator();
    }
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
        ImGui::InputText("Name", &creation.behavior.name);
        ImGui::InputText("Resource id", &creation.behavior.id);
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
    ImGui::SameLine();
    ImGui::BeginDisabled(!behavior_session.can_redo());
    if (ImGui::Button("Redo##behavior")) static_cast<void>(behavior_session.redo());
    ImGui::EndDisabled();

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
                if (auto* typed = std::get_if<bool>(&value)) changed = ImGui::Checkbox(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<std::int64_t>(&value)) {
                    int visible = static_cast<int>(*typed);
                    changed = ImGui::InputInt(property.id.c_str(), &visible);
                    *typed = visible;
                } else if (auto* typed = std::get_if<float>(&value)) changed = ImGui::InputFloat(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<std::string>(&value)) changed = ImGui::InputText(property.id.c_str(), typed);
                else if (auto* typed = std::get_if<fabric::core::Vec2>(&value)) {
                    float values[2]{typed->x, typed->y}; changed = ImGui::InputFloat2(property.id.c_str(), values);
                    *typed = {values[0], values[1]};
                } else if (auto* typed = std::get_if<fabric::project::ResourceReference>(&value)) {
                    changed = ImGui::InputText(property.id.c_str(), &typed->id.value);
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
    ImGui::InputText("From node", &from_node); ImGui::SameLine();
    ImGui::InputText("From port", &from_port);
    ImGui::InputText("To node", &to_node); ImGui::SameLine();
    ImGui::InputText("To port", &to_port);
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
        ImGui::TextDisabled("%s",
                            selected->document_path.generic_string().c_str());
    }
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
        ImGui::InputText("Name", &creation.transformation.name);
        ImGui::InputText("Resource id", &creation.transformation.id);
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
    ImGui::BeginDisabled(!transformation_session.can_redo());
    if (ImGui::Button("Redo##transformation"))
        static_cast<void>(transformation_session.redo());
    ImGui::EndDisabled();

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
                               ImGuiButtonFlags_MouseButtonMiddle);
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
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = io.MousePos;
        const auto distance = [](const ImVec2 left, const ImVec2 right) {
            return std::hypot(left.x - right.x, left.y - right.y);
        };
        CanvasUiState::DragOperation operation =
            CanvasUiState::DragOperation::none;
        if (selected_node != nullptr && !selected_node->locked) {
            if (canvas.tool == CanvasUiState::Tool::rotate &&
                distance(mouse, rotate_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::rotate;
            } else if (canvas.tool == CanvasUiState::Tool::scale &&
                       distance(mouse, scale_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::scale;
            } else if (canvas.tool == CanvasUiState::Tool::pivot &&
                       distance(mouse, pivot_handle) <= 12.0F) {
                operation = CanvasUiState::DragOperation::pivot;
            }
        }
        if (operation == CanvasUiState::DragOperation::none) {
            const auto world = to_world(mouse);
            const auto hit_node = fabric::editor::topmost_vector_node_at(
                asset.native->nodes, world, 8.0F / pixels_per_unit);
            if (hit_node) {
                if (*hit_node == canvas.selected_node && selected_node != nullptr &&
                    !selected_node->locked &&
                    canvas.tool == CanvasUiState::Tool::move) {
                    operation = CanvasUiState::DragOperation::move;
                } else {
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
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        canvas.dragging = false;
        canvas.drag_operation = CanvasUiState::DragOperation::none;
    }
    if (hovered && canvas.dragging && selected_node != nullptr &&
        !selected_node->locked &&
        (io.MousePos.x != canvas.drag_start_mouse.x ||
         io.MousePos.y != canvas.drag_start_mouse.y)) {
        auto changed = *selected_node;
        const auto& start = canvas.drag_start_transform;
        const auto start_mouse = to_world(canvas.drag_start_mouse);
        const auto current_mouse = to_world(io.MousePos);
        if (canvas.drag_operation == CanvasUiState::DragOperation::move) {
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
        }
    }
    draw_list->PopClipRect();
    if (hovered) {
        ImGui::SetTooltip("Click a shape to select it. Move drags the selected shape; Rotate, Scale and Pivot drag only their active handle. Middle drag: pan | Wheel: zoom %.0f%%",
                          canvas.zoom * 100.0F);
    }
}

void draw_packet_preview_canvas(CanvasUiState& canvas,
                                const ImVec2 available,
                                const std::string_view label) {
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
                    CanvasUiState& canvas,
                    const EntityPreviewResult& entity_preview,
                    const fabric::render::VisualCompositionDrawResult&
                        visual_preview,
                    AnimationUiState& animation_ui,
                    TexturedPathUiState& path_ui,
                    ProjectSettingsUiState& project_settings,
                    bool& request_open,
                    bool& request_png,
                    bool& request_svg,
                    fabric::editor::SessionTransitionGuard& transition_guard,
                    bool& running,
                    std::string& status) {
    canvas.native_canvas = false;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float menu_height = ImGui::GetFrameHeight();
    const float status_height = 34.0F;
    const float left_width = std::clamp(viewport->Size.x * 0.22F, 240.0F, 330.0F);
    const float right_width = std::clamp(viewport->Size.x * 0.24F, 270.0F, 360.0F);
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
            "Entity preview");
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
            if (ImGui::InputText("Name", &name)) {
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
            if (ImGui::SliderFloat("Opacity", &opacity, 0.0F, 1.0F, "%.2f")) {
                material.opacity = opacity;
                commit_material(std::move(material));
            }
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
            if (ImGui::InputFloat2("UV offset", uv_offset)) {
                material.uv_transform.position = {uv_offset[0], uv_offset[1]};
                commit_material(std::move(material));
            }
            material = current;
            float uv_scale[]{material.uv_transform.scale.x,
                             material.uv_transform.scale.y};
            if (ImGui::InputFloat2("UV scale", uv_scale)) {
                material.uv_transform.scale = {uv_scale[0], uv_scale[1]};
                commit_material(std::move(material));
            }
            material = current;
            float uv_rotation = material.uv_transform.rotation_degrees;
            if (ImGui::InputFloat("UV rotation", &uv_rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                material.uv_transform.rotation_degrees = uv_rotation;
                commit_material(std::move(material));
            }
            material = current;
            float uv_pivot[]{material.uv_transform.pivot.x,
                             material.uv_transform.pivot.y};
            if (ImGui::InputFloat2("UV pivot", uv_pivot)) {
                material.uv_transform.pivot = {uv_pivot[0], uv_pivot[1]};
                commit_material(std::move(material));
            }
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
                        selected_path_command == 0U ? "Start" : "Endpoint",
                        &command.point.x, 0.05F);
                    if (command.kind ==
                        fabric::project::TexturedPathCommandKind::cubic) {
                        command_changed |= ImGui::DragFloat2(
                            "Handle in", &command.control1.x, 0.05F);
                        command_changed |= ImGui::DragFloat2(
                            "Handle out", &command.control2.x, 0.05F);
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

                auto style = *session.selected_textured_path();
                bool style_changed = false;
                ImGui::SeparatorText("Ribbon and texture");
                style_changed |= ImGui::Checkbox("Closed", &style.closed);
                style_changed |= ImGui::DragFloat(
                    "Width", &style.width, 0.01F, 0.001F, 1000.0F);
                style_changed |= ImGui::DragFloat2(
                    "Texture repeat", &style.uv_scale.x, 0.05F,
                    0.001F, 1000.0F);
                style_changed |= ImGui::DragFloat2(
                    "Texture offset", &style.uv_offset.x, 0.01F);
                style_changed |= ImGui::ColorEdit4(
                    "Color", &style.color.red);
                style_changed |= ImGui::SliderFloat(
                    "Opacity", &style.opacity, 0.0F, 1.0F);
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
                ImGui::DragFloat("Scroll speed", &path_ui.scroll_speed,
                                 0.05F, -100.0F, 100.0F);
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
                    changed |= ImGui::DragFloat("Z order", &layer.z_order,
                                                0.1F);
                    changed |= ImGui::SliderFloat("Opacity", &layer.opacity,
                                                  0.0F, 1.0F);
                    changed |= ImGui::SliderFloat2("Anchor", &layer.anchor.x,
                                                   0.0F, 1.0F);
                    changed |= ImGui::DragFloat2("Position",
                                                 &layer.transform.position.x,
                                                 0.05F);
                    changed |= ImGui::DragFloat(
                        "Rotation", &layer.transform.rotation_degrees, 0.5F);
                    changed |= ImGui::DragFloat2("Scale",
                                                 &layer.transform.scale.x,
                                                 0.01F, 0.001F, 100.0F);
                    changed |= ImGui::DragFloat2("Pivot",
                                                 &layer.transform.pivot.x,
                                                 0.01F);
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
                                        "Crop origin", &view.crop.origin.x,
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
                                        "Crop size", &view.crop.size.x, 1.0F,
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
                                    "Crop pivot", &view.pivot.x, 0.0F, 1.0F);
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
                    if (ImGui::DragFloat2("Anchor position",
                                          &anchor.position.x, 0.05F)) {
                        auto candidate =
                            *session.selected_visual_component();
                        candidate.anchors[selected_anchor] = std::move(anchor);
                        (void)session.set_selected_visual_component(
                            std::move(candidate));
                    }
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
                    } else if (auto* value = std::get_if<std::int64_t>(
                                   &parameter.default_value)) {
                        changed |= ImGui::InputScalar(
                            "Default", ImGuiDataType_S64, value);
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
                ImGui::InputFloat2("Crop origin (px)", crop_origin);
                ImGui::InputFloat2("Crop size (px)", crop_size);
                ImGui::InputFloat2("Pivot (normalized)", crop_pivot);
                ImGui::InputFloat2("View position", view_position);
                ImGui::InputFloat("View rotation", &view_rotation);
                ImGui::InputFloat2("View scale", view_scale);
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
                        ImGui::PushID(static_cast<int>(node_index));
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
            if (ImGui::InputText("Name", &node_name)) {
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
            if (ImGui::InputFloat2("Position", position)) {
                node.transform.position = {position[0], position[1]};
                commit_node(node);
            }
            float scale[]{node.transform.scale.x, node.transform.scale.y};
            if (ImGui::InputFloat2("Scale", scale)) {
                node.transform.scale = {scale[0], scale[1]};
                commit_node(node);
            }
            float rotation = node.transform.rotation_degrees;
            if (ImGui::InputFloat("Rotation", &rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                node.transform.rotation_degrees = rotation;
                commit_node(node);
            }
            float bounds_origin[]{node.shape.bounds.origin.x,
                                  node.shape.bounds.origin.y};
            if (ImGui::InputFloat2("Bounds origin", bounds_origin)) {
                node.shape.bounds.origin = {bounds_origin[0], bounds_origin[1]};
                commit_node(node);
            }
            float bounds_size[]{node.shape.bounds.size.x,
                                node.shape.bounds.size.y};
            if (ImGui::InputFloat2("Bounds size", bounds_size)) {
                node.shape.bounds.size = {bounds_size[0], bounds_size[1]};
                commit_node(node);
            }
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
                        node.shape.kind = shape_kind;
                        if (shape_kind != fabric::project::VectorShapeKind::path)
                            node.shape.path.clear();
                        commit_node(node);
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
                    if (ImGui::InputFloat2("Point", point)) {
                        node.shape.points[point_index] = {point[0], point[1]};
                        commit_node(node);
                    }
                    ImGui::PopID();
                }
            } else if (node.shape.kind == fabric::project::VectorShapeKind::path) {
                ImGui::TextDisabled("Path commands: %zu", node.shape.path.size());
                if (ImGui::Button("Add move command")) {
                    node.shape.path.push_back({
                        .kind = fabric::project::VectorPathCommandKind::move,
                        .point = node.shape.bounds.origin});
                    commit_node(node);
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
                                command.kind = command_kind;
                                commit_node(node);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    float command_point[]{command.point.x, command.point.y};
                    if (ImGui::InputFloat2("Point", command_point)) {
                        command.point = {command_point[0], command_point[1]};
                        commit_node(node);
                    }
                    if (command.kind == fabric::project::VectorPathCommandKind::cubic) {
                        float control1[]{command.control1.x, command.control1.y};
                        float control2[]{command.control2.x, command.control2.y};
                        if (ImGui::InputFloat2("Bezier handle 1", control1)) {
                            command.control1 = {control1[0], control1[1]};
                            commit_node(node);
                        }
                        if (ImGui::InputFloat2("Bezier handle 2", control2)) {
                            command.control2 = {control2[0], control2[1]};
                            commit_node(node);
                        }
                    }
                    if (ImGui::SmallButton("Remove command")) {
                        node.shape.path.erase(node.shape.path.begin() +
                                              static_cast<std::ptrdiff_t>(command_index));
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
                if (ImGui::InputFloat2("Image offset", image_offset)) {
                    node.fill.image->transform.position = {
                        image_offset[0], image_offset[1]};
                    commit_node(node);
                }
                float image_scale[]{node.fill.image->transform.scale.x,
                                    node.fill.image->transform.scale.y};
                if (ImGui::InputFloat2("Image scale", image_scale)) {
                    node.fill.image->transform.scale = {
                        image_scale[0], image_scale[1]};
                    commit_node(node);
                }
                float image_rotation =
                    node.fill.image->transform.rotation_degrees;
                if (ImGui::InputFloat("Image rotation", &image_rotation,
                                      1.0F, 10.0F, "%.2f deg")) {
                    node.fill.image->transform.rotation_degrees = image_rotation;
                    commit_node(node);
                }
                float image_pivot[]{node.fill.image->transform.pivot.x,
                                    node.fill.image->transform.pivot.y};
                if (ImGui::InputFloat2("Image pivot", image_pivot)) {
                    node.fill.image->transform.pivot = {
                        image_pivot[0], image_pivot[1]};
                    commit_node(node);
                }
                float opacity = node.fill.image->opacity;
                if (ImGui::SliderFloat("Image opacity", &opacity, 0.0F, 1.0F,
                                       "%.2f")) {
                    node.fill.image->opacity = opacity;
                    commit_node(node);
                }
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
                if (ImGui::InputFloat("Stroke width", &stroke_width,
                                      0.1F, 1.0F)) {
                    node.stroke->width = stroke_width;
                    commit_node(node);
                }
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
            }
            ImGui::EndDisabled();
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::entity &&
            session.selected_entity()) {
            const auto entity = *session.selected_entity();
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
                const auto label = entity.nodes[node_index].name + "##entity-node-" +
                    std::to_string(node_index);
                if (ImGui::Selectable(label.c_str(),
                                      canvas.selected_node == node_index)) {
                    canvas.selected_node = node_index;
                }
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
            if (ImGui::InputText("Node name", &node_name)) {
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
            if (ImGui::InputFloat2("Entity position", position)) {
                node.transform.position = {position[0], position[1]};
                commit_entity_node(node);
            }
            float scale[]{node.transform.scale.x, node.transform.scale.y};
            if (ImGui::InputFloat2("Entity scale", scale)) {
                node.transform.scale = {scale[0], scale[1]};
                commit_entity_node(node);
            }
            float pivot[]{node.transform.pivot.x, node.transform.pivot.y};
            if (ImGui::InputFloat2("Entity pivot", pivot)) {
                node.transform.pivot = {pivot[0], pivot[1]};
                commit_entity_node(node);
            }
            float rotation = node.transform.rotation_degrees;
            if (ImGui::InputFloat("Entity rotation", &rotation, 1.0F, 10.0F,
                                  "%.2f deg")) {
                node.transform.rotation_degrees = rotation;
                commit_entity_node(node);
            }
            float z_order = node.z_order;
            if (ImGui::InputFloat("Z order", &z_order, 0.1F, 1.0F, "%.2f")) {
                node.z_order = z_order;
                commit_entity_node(node);
            }
            ImGui::SeparatorText("Drawable");
            const auto drawable_label = std::string(
                fabric::project::to_string(node.drawable.kind));
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
                        node.drawable.kind = kind;
                        if (kind == fabric::project::EntityDrawableKind::none) {
                            node.drawable.resource.reset();
                            node.drawable.material.reset();
                            node.drawable.component_instance.reset();
                        } else {
                            const char* expected = kind ==
                                    fabric::project::EntityDrawableKind::texture
                                ? "texture"
                                : kind == fabric::project::EntityDrawableKind::vector
                                ? "vector" : "visualComponent";
                            if (!node.drawable.resource ||
                                node.drawable.resource->expected_type != expected)
                                node.drawable.resource =
                                    fabric::project::ResourceReference{
                                        first->id, expected};
                            if (kind == fabric::project::EntityDrawableKind::visual_component) {
                                node.drawable.material.reset();
                                if (!node.drawable.component_instance)
                                    node.drawable.component_instance =
                                        fabric::project::VisualComponentInstance{};
                            } else {
                                node.drawable.component_instance.reset();
                            }
                        }
                        commit_entity_node(node);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
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
                            auto kind = fabric::editor::StudioResourceKind::texture;
                            if (value->expected_type == "vector")
                                kind = fabric::editor::StudioResourceKind::vector;
                            else if (value->expected_type == "material")
                                kind = fabric::editor::StudioResourceKind::material;
                            else if (value->expected_type == "visualComponent")
                                kind = fabric::editor::StudioResourceKind::visual_component;
                            auto id = value->id.value;
                            if (draw_project_resource_picker(
                                    "Value", session.resources(), kind, id, false)) {
                                value->id = {.value = id};
                                override_changed = true;
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
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            ImGui::EndDisabled();
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
                for (const auto& event : loaded.audio->events)
                    ImGui::BulletText("%s — %s — volume %.2f — %s",
                                      event.id.c_str(), event.source.c_str(),
                                      event.volume, event.loop ? "loop" : "one-shot");
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
                    if (ImGui::InputInt("Code", &code)) changed = true;
                    edited_binding.device = static_cast<fabric::project::InputDevice>(device);
                    edited_binding.code = code;
                    int binding_kind = static_cast<int>(binding.kind);
                    const char* binding_kinds[] = {"button", "axis"};
                    if (ImGui::Combo("Kind", &binding_kind, binding_kinds, 2)) changed = true;
                    edited_binding.kind = static_cast<fabric::project::InputBindingKind>(binding_kind);
                    if (edited_binding.kind == fabric::project::InputBindingKind::axis) {
                        if (ImGui::InputFloat("Threshold", &edited_binding.threshold, 0.05F, 0.1F, "%.2f")) changed = true;
                        if (ImGui::InputFloat("Dead zone", &edited_binding.dead_zone, 0.05F, 0.1F, "%.2f")) changed = true;
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
            if (ImGui::InputFloat("Duration", &duration, 0.1F, 1.0F, "%.2f s")) {
                if (!session.set_selected_animation_duration(duration)) {
                    status = "Animation duration rejected; inspect diagnostics.";
                }
            }
            bool loop = clip.loop;
            if (ImGui::Checkbox("Loop", &loop)) {
                if (!session.set_selected_animation_loop(loop)) {
                    status = "Animation loop rejected; inspect diagnostics.";
                }
            }
            animation_ui.scrub_time = std::clamp(animation_ui.scrub_time, 0.0F,
                                                  std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Scrub", &animation_ui.scrub_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            ImGui::Checkbox("Auto-key at scrub time", &animation_ui.auto_key);
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
            ImGui::SliderFloat("Marker time", &animation_ui.marker_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
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
            if (session.selected_entity() &&
                !session.selected_entity()->nodes.empty()) {
                const auto& target_nodes = session.selected_entity()->nodes;
                const auto selected_node = std::ranges::find(
                    target_nodes, animation_ui.node_id,
                    &fabric::project::EntityNode::id);
                const char* node_label = selected_node == target_nodes.end()
                    ? "Choose target node..." : selected_node->name.c_str();
                if (ImGui::BeginCombo("Target node", node_label)) {
                    for (const auto& target_node : target_nodes) {
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
            const char* binding_presets[] = {
                "Custom", "Transform / Position", "Transform / Rotation",
                "Transform / Scale", "Material / Opacity", "Material / Color"};
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
                default:
                    break;
                }
            }
            ImGui::InputText("Component", &animation_ui.component_id);
            ImGui::InputText("Property", &animation_ui.property_id);
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
            ImGui::SliderFloat("Key time", &animation_ui.key_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            ImGui::Combo("Key type", &animation_ui.key_kind,
                         "Vec2\0Scalar\0Color\0Boolean\0Resource\0");
            bool auto_key_changed = false;
            if (animation_ui.key_kind == 0) {
                auto_key_changed = ImGui::InputFloat2("Vec2 value",
                                                      animation_ui.key_value);
            } else if (animation_ui.key_kind == 1) {
                auto_key_changed = ImGui::InputFloat("Scalar value",
                                                     &animation_ui.key_scalar);
            } else if (animation_ui.key_kind == 2) {
                auto_key_changed = ImGui::ColorEdit4("Color value",
                                                     animation_ui.key_color);
            } else if (animation_ui.key_kind == 3) {
                auto_key_changed = ImGui::Checkbox("Boolean value",
                                                    &animation_ui.key_boolean);
            } else {
                auto_key_changed = ImGui::InputText("Resource id",
                                                    &animation_ui.key_resource_id);
            }
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
                        animation_ui.composition)) {
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
            ImGui::SeparatorText("Tracks");
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
                    ImGui::BulletText("key %zu", key_index);
                    ImGui::SameLine();
                    float key_time = key.time;
                    ImGui::SetNextItemWidth(120.0F);
                    if (ImGui::SliderFloat("##key-time", &key_time, 0.0F,
                                           std::max(0.01F, clip.duration),
                                           "%.2f s")) {
                        if (!session.move_selected_animation_key(
                                track.binding, key_index, key_time)) {
                            status = "Key move rejected; inspect diagnostics.";
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
        ImGui::OpenPopup("Project settings");
        project_settings.request = false;
    }
    if (ImGui::BeginPopupModal("Project settings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(420.0F);
        ImGui::InputText("Project name", &project_settings.name);
        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputDouble("Pixels per unit",
                           &project_settings.pixels_per_unit, 1.0, 10.0,
                           "%.2f");
        ImGui::TextDisabled("%s", session.project_root().string().c_str());
        const bool valid = !project_settings.name.empty() &&
            project_settings.name.size() <= 255 &&
            std::isfinite(project_settings.pixels_per_unit) &&
            project_settings.pixels_per_unit > 0.0 &&
            project_settings.pixels_per_unit <= 1'000'000.0;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Apply", {110.0F, 0.0F})) {
            const bool name_updated =
                session.set_project_name(project_settings.name);
            const bool units_updated = session.set_pixels_per_unit(
                project_settings.pixels_per_unit);
            if (name_updated && units_updated) {
                status = "Project settings changed.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Invalid project settings; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
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
        std::string destination = creation.project.parent_directory.string();
        ImGui::SetNextItemWidth(560.0F);
        if (ImGui::InputText("Parent folder", &destination)) {
            creation.project.parent_directory = destination;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::array<char, 1024> selected{};
            if (choose_folder(window, selected, status)) {
                creation.project.parent_directory = selected.data();
            }
        }
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.project.name);
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
        const auto validation = creation.project.validate();
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
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.artwork.name);
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputDouble("Width", &creation.artwork.width, 1.0, 10.0,
                           "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0F);
        ImGui::InputDouble("Height", &creation.artwork.height, 1.0, 10.0,
                           "%.2f");
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
            if (ImGui::InputFloat2("Image offset", offset)) {
                creation.artwork.image_transform.position = {
                    offset[0], offset[1]};
            }
            float scale[] = {creation.artwork.image_transform.scale.x,
                             creation.artwork.image_transform.scale.y};
            if (ImGui::InputFloat2("Image scale", scale)) {
                creation.artwork.image_transform.scale = {scale[0], scale[1]};
            }
            float pivot[] = {creation.artwork.image_transform.pivot.x,
                             creation.artwork.image_transform.pivot.y};
            if (ImGui::InputFloat2("Image pivot", pivot)) {
                creation.artwork.image_transform.pivot = {
                    pivot[0], pivot[1]};
            }
            ImGui::InputFloat(
                "Image rotation",
                &creation.artwork.image_transform.rotation_degrees,
                1.0F, 10.0F, "%.2f deg");
            float opacity = static_cast<float>(
                creation.artwork.image_opacity);
            if (ImGui::SliderFloat("Image opacity", &opacity, 0.0F, 1.0F,
                                   "%.2f")) {
                creation.artwork.image_opacity = opacity;
            }
            ImGui::Checkbox("Warp pixels with vector shape (advanced)",
                            &creation.artwork.deform_image_with_shape);
            ImGui::TextDisabled(
                "Off keeps image placement independent; crop the raster source in its own viewport.");
        }
        const auto validation = creation.artwork.validate(
            session.project_root(), *session.manifest());
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
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputText("Name", &request.name);
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Resource id", &request.id.value);
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
        ImGui::InputText("Name", &fields.name);
        ImGui::InputText("Resource id", &fields.id);
        ImGui::InputFloat2("Size", fields.size);
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
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create visual component", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& fields = creation.component;
        ImGui::TextUnformatted("Wrap a composition as a reusable component");
        ImGui::InputText("Name", &fields.name);
        ImGui::InputText("Resource id", &fields.id);
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
        ImGui::InputFloat2("Bounds size", fields.size);
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
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.material.name);
        float color[] = {creation.material.color.red,
                         creation.material.color.green,
                         creation.material.color.blue,
                         creation.material.color.alpha};
        if (ImGui::ColorEdit4("Color", color)) {
            creation.material.color = {color[0], color[1], color[2], color[3]};
        }
        float opacity = static_cast<float>(creation.material.opacity);
        if (ImGui::SliderFloat("Opacity", &opacity, 0.0F, 1.0F, "%.2f")) {
            creation.material.opacity = opacity;
        }
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
        if (ImGui::InputFloat2("UV offset", uv_position)) {
            creation.material.uv_transform.position =
                {uv_position[0], uv_position[1]};
        }
        float uv_scale[] = {creation.material.uv_transform.scale.x,
                            creation.material.uv_transform.scale.y};
        if (ImGui::InputFloat2("UV scale", uv_scale)) {
            creation.material.uv_transform.scale = {uv_scale[0], uv_scale[1]};
        }
        ImGui::InputFloat("UV rotation",
                          &creation.material.uv_transform.rotation_degrees,
                          1.0F, 10.0F, "%.2f deg");
        const auto validation = creation.material.validate(
            session.project_root(), *session.manifest());
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
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.entity.name);
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Root node name", &creation.entity.node_name);
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
        if (ImGui::InputFloat2("Position", position)) {
            creation.entity.transform.position = {position[0], position[1]};
        }
        float scale[] = {creation.entity.transform.scale.x,
                         creation.entity.transform.scale.y};
        if (ImGui::InputFloat2("Scale", scale)) {
            creation.entity.transform.scale = {scale[0], scale[1]};
        }
        ImGui::InputFloat("Rotation",
                          &creation.entity.transform.rotation_degrees,
                          1.0F, 10.0F, "%.2f deg");
        ImGui::InputFloat("Z order", &creation.entity.z_order, 0.1F, 1.0F,
                          "%.2f");
        const auto validation = creation.entity.validate(
            session.project_root(), *session.manifest());
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
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.entity.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Create animation", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create an AnimationClip v2");
        ImGui::TextDisabled(
            "The validated clip is published atomically in the open project.");
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.animation.name);
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
        ImGui::Checkbox("Loop", &creation.animation.loop);
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Marker id (optional)", &creation.animation.marker_id);
        if (!creation.animation.marker_id.empty()) {
            ImGui::SetNextItemWidth(220.0F);
            ImGui::InputDouble("Marker time", &creation.animation.marker_time,
                               0.1, 1.0, "%.2f");
        }
        const auto validation = creation.animation.validate(
            session.project_root(), *session.manifest());
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
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.input.name);
        for (std::size_t action_index = 0;
             action_index < creation.input.actions.size(); ++action_index) {
            auto& action = creation.input.actions[action_index];
            ImGui::PushID(static_cast<int>(action_index));
            ImGui::SeparatorText(("Action " + std::to_string(action_index + 1)).c_str());
            ImGui::SetNextItemWidth(260.0F);
            ImGui::InputText("Id", &action.id);
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
                ImGui::InputInt("Code", &binding.code);
                if (binding.kind == fabric::project::InputBindingKind::axis) {
                    ImGui::InputFloat("Threshold", &binding.threshold, 0.05F, 0.1F, "%.2f");
                    ImGui::InputFloat("Dead zone", &binding.dead_zone, 0.05F, 0.1F, "%.2f");
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
        const auto validation = creation.input.validate(
            session.project_root(), *session.manifest());
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
                     const bool animation_e2e = false) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
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

    SDL_Window* window = SDL_CreateWindow(
        "Vertex Loom - Asset Studio", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1440, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
            ((behavior_e2e || transformation_e2e || entity_e2e || animation_e2e)
                 ? SDL_WINDOW_HIDDEN : 0U));
    if (window == nullptr) {
        std::cerr << "window creation failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
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
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

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
    bool behavior_e2e_complete = false;
    if (behavior_e2e && session.has_project()) {
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
        behavior_e2e_complete = reloaded_ok && attached && actions.size() == 1U;
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

    bool running = true;
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
                       pending_import_preview, canvas, entity_preview,
                       visual_preview,
                       animation_ui, textured_path_ui,
                       project_settings,
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
        SDL_GL_SwapWindow(window);
        if (behavior_e2e || transformation_e2e || entity_e2e || animation_e2e)
            running = false;
    }

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
    return (behavior_e2e && !behavior_e2e_complete) ||
            (transformation_e2e && !transformation_e2e_complete) ||
            (entity_e2e && !entity_e2e_complete) ||
            (animation_e2e && !animation_e2e_complete)
        ? 1 : 0;
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
    if (argument_count > 2 && !behavior_e2e && !transformation_e2e &&
        !entity_e2e && !animation_e2e) {
        std::cerr << "usage: asset_studio [project-directory]\n"
                     "       asset_studio --e2e-behavior project-directory\n"
                     "       asset_studio --e2e-transformation project-directory\n"
                     "       asset_studio --e2e-entity project-directory\n"
                     "       asset_studio --e2e-animation project-directory\n";
        return 64;
    }
    const std::filesystem::path initial_project =
        (behavior_e2e || transformation_e2e || entity_e2e || animation_e2e)
        ? std::filesystem::path{arguments[2]}
        : argument_count == 2 ? std::filesystem::path{arguments[1]}
                            : std::filesystem::path{};
    return run_asset_studio(initial_project, behavior_e2e, transformation_e2e,
                            entity_e2e, animation_e2e);
}
