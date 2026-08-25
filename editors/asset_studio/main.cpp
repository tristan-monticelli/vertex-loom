#include "fabric/editor/creation_prompts.hpp"
#include "fabric/editor/project_session.hpp"
#include "fabric/editor/session_transition.hpp"
#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/svg_vector.hpp"
#include "fabric/render/vector_geometry.hpp"
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
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
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

struct CreationUiState {
    fabric::editor::CreateProjectPrompt project;
    fabric::editor::CreateVectorArtworkPrompt artwork;
    fabric::editor::CreateMaterialPrompt material;
    fabric::editor::CreateEntityPrompt entity;
    fabric::editor::CreateAnimationPrompt animation;
    fabric::editor::CreateInputPrompt input;
    std::optional<fabric::editor::CreateVectorArtworkPrompt> prepared_artwork;
    bool request_project{};
    bool request_artwork{};
    bool request_material{};
    bool request_entity{};
    bool request_animation{};
    bool request_input{};
    bool project_publish_attempted{};
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

    float zoom{1.0F};
    ImVec2 pan{};
    std::size_t selected_node{};
    bool native_canvas{};
    Tool tool{Tool::move};
    bool dragging{};
    ImVec2 drag_start_mouse{};
    fabric::core::Transform drag_start_transform;
    ImVec2 native_origin{};
    ImVec2 native_size{};
    fabric::core::Rect native_world_bounds;
    fabric::core::Rect entity_world_bounds{{-5.0F, -5.0F}, {10.0F, 10.0F}};
};

struct AnimationUiState {
    std::string node_id{"root"};
    std::string component_id{"transform"};
    std::string property_id{"position"};
    int binding_preset{};
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
    const fabric::editor::ProjectSession& session,
    const fabric::project::AnimationClip* animation = nullptr,
    const float animation_time = 0.0F) {
    EntityPreviewResult result;
    if (!session.selected_entity() || !session.manifest()) return result;
    auto entity = *session.selected_entity();
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
        std::optional<fabric::project::MaterialDefinition> material;
        if (node.drawable.material) {
            const auto loaded = fabric::project::load_material(
                session.project_root(), *session.manifest(),
                fabric::project::material_document_path(
                    *session.manifest(), node.drawable.material->id));
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
                session.project_root(), *session.manifest(),
                fabric::project::vector_document_path(
                    *session.manifest(), node.drawable.resource->id));
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors) {
                    result.errors.push_back(error.field + ": " + error.message);
                }
                continue;
            }
            auto drawable = std::move(*loaded.asset);
            if (drawable.source_kind == fabric::project::VectorSourceKind::linked_svg) {
                auto converted = fabric::render::convert_svg_to_native(
                    session.project_root() / drawable.source,
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
        } else if (node.drawable.kind == fabric::project::EntityDrawableKind::texture &&
                   node.drawable.resource) {
            const auto loaded = fabric::project::load_texture_asset(
                session.project_root(), *session.manifest(),
                fabric::project::texture_document_path(
                    *session.manifest(), node.drawable.resource->id));
            if (!loaded.ok()) {
                for (const auto& error : loaded.errors) {
                    result.errors.push_back(error.field + ": " + error.message);
                }
                continue;
            }
            const float ppu = static_cast<float>(session.manifest()->pixels_per_unit);
            const float half_width = std::max(0.5F,
                static_cast<float>(loaded.asset->width) / ppu * 0.5F);
            const float half_height = std::max(0.5F,
                static_cast<float>(loaded.asset->height) / ppu * 0.5F);
            fabric::render::VectorDrawPacket packet{
                .node_id = entity.document.id.value + ":" + node.id,
                .fill_color = material && !material->texture
                    ? std::optional{fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F}}
                    : std::nullopt,
                .image_fill = fabric::project::VectorImageFill{
                    .texture = *node.drawable.resource},
                .outline = {{-half_width, -half_height}, {half_width, -half_height},
                            {half_width, half_height}, {-half_width, half_height}},
                .fill_vertices = {{-half_width, -half_height},
                                  {half_width, -half_height},
                                  {half_width, half_height},
                                  {-half_width, half_height}},
                .fill_uv = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F},
                            {0.0F, 1.0F}},
                .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
                .closed_outline = true};
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

void draw_project_tree(fabric::editor::ProjectSession& session,
                       AssetPreview& preview, std::string& status) {
    if (!session.has_project()) {
        ImGui::TextDisabled("No project open");
        ImGui::Spacing();
        ImGui::TextWrapped("Create or open a Vertex Loom project to begin.");
        return;
    }

    static ImGuiTextFilter filter;
    filter.Draw("Search", -1.0F);
    ImGui::Spacing();
    const auto draw_kind = [&](const char* label,
                               const fabric::editor::StudioResourceKind kind) {
        ImGui::SeparatorText(label);
        bool any = false;
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
            if (resource.native) {
                ImGui::SameLine();
                ImGui::TextDisabled("native");
            }
        }
        if (!any) {
            ImGui::TextDisabled("None");
        }
    };
    draw_kind("Textures", fabric::editor::StudioResourceKind::texture);
    draw_kind("Vector artworks", fabric::editor::StudioResourceKind::vector);
    draw_kind("Materials / fills", fabric::editor::StudioResourceKind::material);
    draw_kind("Entities", fabric::editor::StudioResourceKind::entity);
    draw_kind("Animations", fabric::editor::StudioResourceKind::animation);
    draw_kind("Input bindings", fabric::editor::StudioResourceKind::input);
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
            (resource.kind == fabric::editor::StudioResourceKind::texture
                 ? "texture"
                 : "vector") + ")##existing-" + resource.id.value;
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

void draw_prompt_error(const fabric::editor::PromptValidation& validation,
                       const std::string_view field) {
    if (const auto error = validation.error_for(field)) {
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
        rotate_handle = {top.x, top.y - 30.0F};
        scale_handle = to_screen(world_bottom_right);
        pivot_handle = to_screen(
            transform_point(*selected_node, selected_node->transform.pivot));
    }
    if (hovered && selected_node != nullptr && !selected_node->locked &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = io.MousePos;
        const auto distance = [](const ImVec2 left, const ImVec2 right) {
            return std::hypot(left.x - right.x, left.y - right.y);
        };
        if (distance(mouse, rotate_handle) <= 12.0F) {
            canvas.tool = CanvasUiState::Tool::rotate;
        } else if (distance(mouse, scale_handle) <= 12.0F) {
            canvas.tool = CanvasUiState::Tool::scale;
        } else if (distance(mouse, pivot_handle) <= 12.0F) {
            canvas.tool = CanvasUiState::Tool::pivot;
        }
        canvas.dragging = true;
        canvas.drag_start_mouse = mouse;
        canvas.drag_start_transform = selected_node->transform;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        canvas.dragging = false;
    }
    if (hovered && canvas.dragging && selected_node != nullptr &&
        !selected_node->locked &&
        (io.MousePos.x != canvas.drag_start_mouse.x ||
         io.MousePos.y != canvas.drag_start_mouse.y)) {
        auto changed = *selected_node;
        const auto& start = canvas.drag_start_transform;
        const auto start_mouse = to_world(canvas.drag_start_mouse);
        const auto current_mouse = to_world(io.MousePos);
        if (canvas.tool == CanvasUiState::Tool::move) {
            changed.transform.position = {
                start.position.x + current_mouse.x - start_mouse.x,
                start.position.y + current_mouse.y - start_mouse.y};
        } else if (canvas.tool == CanvasUiState::Tool::rotate) {
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
        } else if (canvas.tool == CanvasUiState::Tool::scale) {
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
        } else if (canvas.tool == CanvasUiState::Tool::pivot) {
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
        draw_list->AddLine(transform_center, rotate_handle,
                           IM_COL32(236, 180, 75, 220), 1.5F);
        draw_list->AddCircleFilled(rotate_handle, 6.0F,
                                   IM_COL32(236, 180, 75, 255));
        draw_list->AddRectFilled({scale_handle.x - 6.0F, scale_handle.y - 6.0F},
                                 {scale_handle.x + 6.0F, scale_handle.y + 6.0F},
                                 IM_COL32(98, 180, 240, 255));
        draw_list->AddLine({pivot_handle.x - 7.0F, pivot_handle.y},
                           {pivot_handle.x + 7.0F, pivot_handle.y},
                           IM_COL32(180, 110, 235, 255), 2.0F);
        draw_list->AddLine({pivot_handle.x, pivot_handle.y - 7.0F},
                           {pivot_handle.x, pivot_handle.y + 7.0F},
                           IM_COL32(180, 110, 235, 255), 2.0F);
    }
    draw_list->PopClipRect();
    if (hovered) {
        ImGui::SetTooltip("Drag shape to move, orange dot to rotate, blue square to scale, purple cross to move pivot | Middle drag: pan | Wheel: zoom %.0f%%",
                          canvas.zoom * 100.0F);
    }
}

void draw_entity_preview_canvas(CanvasUiState& canvas,
                                const ImVec2 available) {
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
    ImGui::TextDisabled("Entity preview · %.0f%%", canvas.zoom * 100.0F);
}

void draw_workspace(fabric::editor::ProjectSession& session,
                    SDL_Window* window,
                    std::array<char, 1024>& path_buffer,
                    CreationUiState& creation,
                    ImportUiState& imports,
                    AssetPreview& preview,
                    AssetPreview& pending_import_preview,
                    CanvasUiState& canvas,
                    const EntityPreviewResult& entity_preview,
                    AnimationUiState& animation_ui,
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

    ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({left_width, content_height});
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
    ImGui::Begin("Preview", nullptr, fixed_panel_flags);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin, {origin.x + available.x, origin.y + available.y},
                             IM_COL32(21, 24, 30, 255), 4.0F);
    constexpr float grid = 32.0F;
    for (float x = origin.x; x < origin.x + available.x; x += grid) {
        draw_list->AddLine({x, origin.y}, {x, origin.y + available.y},
                           IM_COL32(43, 48, 58, 120));
    }
    for (float y = origin.y; y < origin.y + available.y; y += grid) {
        draw_list->AddLine({origin.x, y}, {origin.x + available.x, y},
                           IM_COL32(43, 48, 58, 120));
    }
    const bool native_selected = session.selected_resource() != nullptr &&
        session.selected_resource()->kind ==
        fabric::editor::StudioResourceKind::vector &&
        session.selected_resource()->native && session.created_vector();
    const bool entity_selected = session.selected_resource() != nullptr &&
        session.selected_resource()->kind ==
            fabric::editor::StudioResourceKind::entity &&
        session.selected_entity();
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
        draw_entity_preview_canvas(
            canvas, {std::max(1.0F, available.x - 16.0F),
                     std::max(1.0F, available.y - 42.0F)});
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

    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - right_width,
                             viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({right_width, content_height});
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
            session.created_vector()->native &&
            !session.created_vector()->native->nodes.empty()) {
            ImGui::SeparatorText("Nodes");
            const auto& nodes = session.created_vector()->native->nodes;
            canvas.selected_node = std::min(canvas.selected_node,
                                            nodes.size() - 1);
            for (std::size_t node_index = 0; node_index < nodes.size();
                 ++node_index) {
                const std::string label = nodes[node_index].name + "##node-" +
                    std::to_string(node_index);
                if (ImGui::Selectable(label.c_str(),
                                      canvas.selected_node == node_index)) {
                    canvas.selected_node = node_index;
                }
            }
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
                ImGui::Text("Image fill: %s",
                            node.fill.image->texture.id.value.c_str());
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
                float opacity = node.fill.image->opacity;
                if (ImGui::SliderFloat("Image opacity", &opacity, 0.0F, 1.0F,
                                       "%.2f")) {
                    node.fill.image->opacity = opacity;
                    commit_node(node);
                }
                bool deform = node.fill.image->deform_with_shape;
                if (ImGui::Checkbox("Deform with shape", &deform)) {
                    node.fill.image->deform_with_shape = deform;
                    commit_node(node);
                }
            }
            ImGui::EndDisabled();
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::entity &&
            session.selected_entity()) {
            const auto& entity = *session.selected_entity();
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
            if (ImGui::Button("Delete") &&
                session.remove_selected_entity_node(canvas.selected_node)) {
                canvas.selected_node = canvas.selected_node == 0U
                    ? 0U : canvas.selected_node - 1U;
                status = "Entity node deleted.";
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
                    const char* devices[] = {"keyboard", "gamepad"};
                    bool changed = false;
                    ImGui::SetNextItemWidth(130.0F);
                    if (ImGui::Combo("Device", &device, devices, 2)) changed = true;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(130.0F);
                    if (ImGui::InputInt("Code", &code)) changed = true;
                    if (changed && !session.set_selected_input_binding(
                            action_index, binding_index,
                            {static_cast<fabric::project::InputDevice>(device), code})) {
                        status = "Input binding rejected; inspect diagnostics.";
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
            ImGui::InputText("Node id", &animation_ui.node_id);
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
    if (session.dirty()) {
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
        ImGui::OpenPopup("Create animation");
        creation.request_animation = false;
    }
    if (creation.request_input && session.has_project()) {
        creation.input.reset();
        ImGui::OpenPopup("Create input bindings");
        creation.request_input = false;
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
            const auto chosen_texture = std::ranges::find_if(
                session.resources(), [&](const fabric::editor::StudioResource& resource) {
                    return resource.kind ==
                               fabric::editor::StudioResourceKind::texture &&
                        resource.id.value == creation.artwork.initial_image_id;
                });
            const char* chosen_label = chosen_texture == session.resources().end()
                ? "Choose a texture..."
                : chosen_texture->name.c_str();
            ImGui::SetNextItemWidth(360.0F);
            if (ImGui::BeginCombo("Texture", chosen_label)) {
                for (const auto& resource : session.resources()) {
                    if (resource.kind !=
                        fabric::editor::StudioResourceKind::texture) {
                        continue;
                    }
                    const bool selected = resource.id.value ==
                        creation.artwork.initial_image_id;
                    if (ImGui::Selectable(resource.name.c_str(), selected)) {
                        creation.artwork.initial_image_id = resource.id.value;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
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
            ImGui::Checkbox("Deform image with shape",
                            &creation.artwork.deform_image_with_shape);
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
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Texture id (optional)", &creation.material.texture_id);
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Vector pattern id (optional)",
                         &creation.material.vector_pattern_id);
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
        ImGui::TextUnformatted("Create a reusable EntityDefinition v1");
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
                     fabric::project::EntityDrawableKind::texture}) {
                const bool selected = creation.entity.drawable == drawable;
                const auto option = std::string(fabric::project::to_string(drawable));
                if (ImGui::Selectable(option.c_str(), selected)) {
                    creation.entity.drawable = drawable;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (creation.entity.drawable !=
            fabric::project::EntityDrawableKind::none) {
            ImGui::SetNextItemWidth(360.0F);
            ImGui::InputText("Drawable resource id", &creation.entity.resource_id);
        }
        ImGui::SetNextItemWidth(360.0F);
        ImGui::InputText("Material id (optional)", &creation.entity.material_id);
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
        ImGui::TextUnformatted("Create an AnimationClip v1");
        ImGui::TextDisabled(
            "The validated clip is published atomically in the open project.");
        ImGui::SetNextItemWidth(560.0F);
        ImGui::InputText("Name", &creation.animation.name);
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
            for (std::size_t binding_index = 0;
                 binding_index < action.bindings.size(); ++binding_index) {
                auto& binding = action.bindings[binding_index];
                ImGui::PushID(static_cast<int>(binding_index));
                int device = static_cast<int>(binding.device);
                const char* devices[] = {"keyboard", "gamepad"};
                ImGui::SetNextItemWidth(130.0F);
                if (ImGui::Combo("Device", &device, devices, 2))
                    binding.device = static_cast<fabric::project::InputDevice>(device);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(130.0F);
                ImGui::InputInt("Code", &binding.code);
                ImGui::PopID();
            }
            if (ImGui::Button("Add binding"))
                action.bindings.push_back({fabric::project::InputDevice::keyboard, 0});
            ImGui::PopID();
        }
        if (ImGui::Button("Add action"))
            creation.input.actions.push_back({"action", {}});
        const auto validation = creation.input.validate(
            session.project_root(), *session.manifest());
        draw_prompt_error(validation, "name");
        draw_prompt_error(validation, "id");
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
        if (ImGui::Button("Save and continue", {150.0F, 0.0F})) {
            if (session.save()) {
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

int run_asset_studio(const std::filesystem::path& initial_project) {
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
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                transition_guard.request(fabric::editor::SessionAction::quit,
                                         session.dirty());
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
                        session.dirty());
                }
                if (ImGui::MenuItem("Open project...", open_shortcut)) {
                    if (session.has_project()) {
                        copy_path_to_buffer(session.project_root(), path_buffer);
                    }
                    transition_guard.request(
                        fabric::editor::SessionAction::open_project,
                        session.dirty());
                }
                if (ImGui::MenuItem("Save", save_shortcut, false,
                                    session.has_project())) {
                    status = session.save()
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
                                             session.dirty());
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
                                     session.dirty());
        }
        if (shortcuts_enabled && command_modifier &&
            ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            transition_guard.request(
                fabric::editor::SessionAction::create_project,
                session.dirty());
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
                                     session.dirty());
        }
        if (shortcuts_enabled && command_modifier && session.has_project() &&
            ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            status = session.save()
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

        draw_workspace(session, window, path_buffer, creation, imports, preview,
                       pending_import_preview, canvas, entity_preview,
                       animation_ui,
                       project_settings,
                       request_open, request_png, request_svg,
                       transition_guard, running, status);

        const auto autosave_status = session.update_autosave();
        if (autosave_status == fabric::editor::AutosaveStatus::saved) {
            status = "Recovery autosave updated.";
        } else if (autosave_status == fabric::editor::AutosaveStatus::failed) {
            status = "Autosave failed; inspect the diagnostics.";
        }

        ImGui::Render();
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
        glViewport(0, 0, drawable_width, drawable_height);
        glClearColor(0.035F, 0.041F, 0.052F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        const bool render_native_vector = canvas.native_canvas &&
            session.created_vector() && session.created_vector()->native;
        const bool render_entity = canvas.native_canvas && entity_selection &&
            !entity_preview.packets.empty();
        if (render_native_vector || render_entity) {
            const auto native_packets = render_native_vector
                ? fabric::render::build_native_draw_packets(*session.created_vector())
                : fabric::render::VectorGeometryResult{};
            const auto packets = render_native_vector
                ? std::span<const fabric::render::VectorDrawPacket>(native_packets.packets)
                : std::span<const fabric::render::VectorDrawPacket>(entity_preview.packets);
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
        SDL_GL_SwapWindow(window);
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
    return 0;
}

} // namespace

int main(const int argument_count, char** arguments) {
    if (argument_count > 2) {
        std::cerr << "usage: asset_studio [project-directory]\n";
        return 64;
    }
    const std::filesystem::path initial_project =
        argument_count == 2 ? std::filesystem::path{arguments[1]}
                            : std::filesystem::path{};
    return run_asset_studio(initial_project);
}
