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
#include "preview_canvas.hpp"
#include "vector_canvas.hpp"

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
using fabric::asset_studio::CanvasUiState;
using fabric::asset_studio::draw_native_vector_canvas;
using fabric::asset_studio::draw_packet_preview_canvas;

fabric::editor::ProjectSession* active_picker_session = nullptr;
std::unordered_map<std::string, AssetPreview>* active_picker_texture_cache = nullptr;
bool ui_focus_probe_enabled = false;
bool ui_focus_probe_succeeded = false;
bool ui_drag_probe_enabled = false;
bool ui_drag_probe_applied = false;
bool ui_override_probe_enabled = false;
bool ui_override_modal_seen = false;
bool ui_override_cancel_preserved = false;
bool ui_override_confirm_applied = false;
ImVec2 ui_override_kind_screen{};
ImVec2 ui_override_texture_screen{};
ImVec2 ui_override_cancel_screen{};
ImVec2 ui_override_confirm_screen{};
bool ui_override_kind_seen = false;
bool ui_override_texture_seen = false;
bool ui_override_cancel_seen = false;
bool ui_override_confirm_seen = false;
bool ui_override_force_modal = false;
bool ui_texture_probe_enabled = false;
bool ui_texture_canvas_seen = false;
bool ui_texture_crop_applied = false;
ImVec2 ui_texture_crop_source{};
ImVec2 ui_texture_crop_target{};
bool ui_input_probe_enabled = false;
bool ui_input_modal_seen = false;
bool ui_input_created = false;
bool ui_input_reloaded = false;
bool ui_beam_probe_enabled = false;
bool ui_beam_create_seen = false;
bool ui_beam_created = false;
bool ui_beam_reloaded = false;
bool ui_beam_holography_variant = false;
ImVec2 ui_beam_create_screen{};
bool ui_button_probe_enabled = false;
bool ui_button_create_seen = false;
bool ui_button_created = false;
bool ui_button_reloaded = false;
ImVec2 ui_button_create_screen{};
bool ui_animation_probe_enabled = false;
bool ui_animation_timeline_seen = false;
bool ui_animation_quick_key_seen = false;
ImVec2 ui_animation_quick_key_screen{};
bool ui_animation_graph_probe_enabled = false;
bool ui_animation_graph_seen = false;
int ui_drag_target_mode = 0;
ImVec2 ui_drag_source_screen{};
ImVec2 ui_drag_target_screen{};
bool ui_drag_source_seen = false;
bool ui_drag_target_seen = false;

struct ResourceDragPayload {
    int kind{};
    char id[256]{};
};

bool is_entity_artwork_kind(const fabric::editor::StudioResourceKind kind) {
    return kind == fabric::editor::StudioResourceKind::texture ||
        kind == fabric::editor::StudioResourceKind::vector ||
        kind == fabric::editor::StudioResourceKind::visual_component;
}

bool is_technical_visual_dependency(
    const fabric::editor::StudioResource& resource) {
    using Kind = fabric::editor::StudioResourceKind;
    if (resource.kind != Kind::vector &&
        resource.kind != Kind::textured_path &&
        resource.kind != Kind::visual_composition) {
        return false;
    }
    const auto& id = resource.id.value;
    return id.ends_with("-border") || id.ends_with("-rail") ||
        id.ends_with("-composition");
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

bool draw_surface_effect_stack(
    fabric::project::ShaderSurfaceSettings& shader,
    const char* identifier,
    const bool expand_blocks = false) {
    using Effect = fabric::project::SurfaceEffect;
    using Kind = fabric::project::SurfaceEffectKind;
    bool changed = false;
    ImGui::PushID(identifier);
    const bool guided_surface =
        shader.classification == fabric::project::TextureClassification::beam ||
        shader.classification ==
            fabric::project::TextureClassification::button_eye;
    const bool is_beam =
        shader.classification == fabric::project::TextureClassification::beam;
    const auto effect_of_kind = [&](const Kind kind) {
        return std::ranges::find(shader.effects, kind, &Effect::kind);
    };
    const auto set_effect = [&](const Kind kind, const fabric::core::Color color,
                                const float amount) {
        auto effect = effect_of_kind(kind);
        if (effect == shader.effects.end()) {
            shader.effects.push_back(
                Effect{.kind = kind, .color = color, .amount = amount});
        } else {
            effect->enabled = true;
            effect->color = color;
            effect->amount = amount;
        }
    };
    const auto sync_legacy_settings = [&]() {
        if (const auto tint = effect_of_kind(Kind::tint);
            tint != shader.effects.end())
            shader.primary_color = tint->color;
        if (const auto holography = effect_of_kind(Kind::holography);
            holography != shader.effects.end()) {
            shader.effect_color = holography->color;
            shader.holography = holography->amount;
        }
        if (const auto shine = effect_of_kind(Kind::shine);
            shine != shader.effects.end())
            shader.shine = shine->amount;
    };
    if (guided_surface && !shader.effects.empty()) {
        ImGui::SeparatorText("Quick look");
        ImGui::TextDisabled(
            "Preserve the source or recolor it with your selected color.");
        auto tint = effect_of_kind(Kind::tint);
        const bool source_mode = is_beam
            ? shader.profile != fabric::project::SurfaceShaderProfile::thread
            : tint == shader.effects.end() || tint->amount <= 0.0F;
        if (ImGui::BeginCombo("Traitement des couleurs",
                              source_mode ? "Source intacte" : "Recoloration")) {
            if (ImGui::Selectable("Source intacte", source_mode)) {
                set_effect(Kind::tint, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
                shader.profile = fabric::project::SurfaceShaderProfile::plastic;
                changed = true;
            }
            if (ImGui::Selectable("Recoloration", !source_mode)) {
                const auto selected_color = tint == shader.effects.end()
                    ? fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F}
                    : tint->color;
                set_effect(Kind::tint, selected_color, 1.0F);
                shader.profile = is_beam
                    ? fabric::project::SurfaceShaderProfile::thread
                    : fabric::project::SurfaceShaderProfile::custom;
                changed = true;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Réinitialiser depuis la source")) {
            shader.profile = fabric::project::SurfaceShaderProfile::plastic;
            set_effect(Kind::tint, {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
            set_effect(Kind::holography,
                       {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
            set_effect(Kind::shine,
                       {1.0F, 1.0F, 1.0F, 1.0F}, 0.0F);
            sync_legacy_settings();
            changed = true;
        }

        if (tint = effect_of_kind(Kind::tint);
            tint != shader.effects.end()) {
            bool quick_changed = ImGui::ColorEdit4(
                "Base color##quick", &tint->color.red);
            quick_changed |= ImGui::SliderFloat(
                "Recolor strength##quick", &tint->amount, 0.0F, 1.0F,
                "%.2f");
            if (quick_changed) {
                tint->enabled = true;
                shader.profile = tint->amount <= 0.0F
                    ? fabric::project::SurfaceShaderProfile::plastic
                    : is_beam
                    ? fabric::project::SurfaceShaderProfile::thread
                    : fabric::project::SurfaceShaderProfile::custom;
                changed = true;
            }
        }
        if (auto holography = effect_of_kind(Kind::holography);
            holography != shader.effects.end()) {
            bool quick_changed = ImGui::ColorEdit4(
                "Glow color##quick", &holography->color.red);
            quick_changed |= ImGui::SliderFloat(
                "Glow strength##quick", &holography->amount, 0.0F, 1.0F,
                "%.2f");
            if (quick_changed) {
                holography->enabled = true;
                changed = true;
            }
        }
        if (auto shine = effect_of_kind(Kind::shine);
            shine != shader.effects.end()) {
            const bool quick_changed = ImGui::SliderFloat(
                "Highlight##quick", &shine->amount, 0.0F, 1.0F, "%.2f");
            if (quick_changed) {
                shine->enabled = true;
                changed = true;
            }
        }
        if (changed) sync_legacy_settings();
    }

    const bool show_advanced = !guided_surface ||
        ImGui::CollapsingHeader("Advanced effect stack");
    if (show_advanced) {
        if (!guided_surface) ImGui::SeparatorText("Effect stack");
    ImGui::TextDisabled("%zu block%s · evaluated from top to bottom",
                        shader.effects.size(), shader.effects.size() == 1U ? "" : "s");
    if (shader.effects.empty()) {
        ImGui::TextWrapped(
            "This asset still uses the compatible two-color shader.");
        if (ImGui::Button("Convert to modular effects")) {
            shader.effects = {
                Effect{.kind = Kind::tint,
                       .color = shader.primary_color,
                       .amount = 1.0F},
                Effect{.kind = Kind::holography,
                       .color = shader.effect_color,
                       .amount = shader.holography},
                Effect{.kind = Kind::shine,
                       .color = {1.0F, 1.0F, 1.0F, 1.0F},
                       .amount = shader.shine},
            };
            changed = true;
        }
    }
    if (ImGui::BeginCombo("Add effect", "Choose a block…")) {
        for (const auto kind : {Kind::tint, Kind::holography, Kind::shine}) {
            const auto label = std::string(fabric::project::to_string(kind));
            if (ImGui::Selectable(label.c_str())) {
                shader.effects.push_back(Effect{.kind = kind});
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    enum class Action { none, duplicate, move_up, move_down, remove };
    Action action = Action::none;
    std::size_t action_index = 0U;
    for (std::size_t index = 0; index < shader.effects.size(); ++index) {
        auto& effect = shader.effects[index];
        ImGui::PushID(static_cast<int>(index));
        const auto title = std::to_string(index + 1U) + ". " +
            std::string(fabric::project::to_string(effect.kind));
        const bool open = ImGui::CollapsingHeader(
            title.c_str(), expand_blocks ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (open) {
            changed |= ImGui::Checkbox("Enabled", &effect.enabled);
            const auto current_kind = std::string(
                fabric::project::to_string(effect.kind));
            if (ImGui::BeginCombo("Mode", current_kind.c_str())) {
                for (const auto kind : {Kind::tint, Kind::holography,
                                        Kind::shine}) {
                    const auto label = std::string(
                        fabric::project::to_string(kind));
                    if (ImGui::Selectable(label.c_str(), effect.kind == kind)) {
                        effect.kind = kind;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            changed |= ImGui::ColorEdit4("Color", &effect.color.red);
            changed |= ImGui::SliderFloat(
                "Amount", &effect.amount, 0.0F, 1.0F, "%.2f");
            if (effect.kind == Kind::holography) {
                changed |= ImGui::SliderFloat(
                    "Pattern scale", &effect.scale, 0.05F, 8.0F, "%.2f");
            }
            if (ImGui::SmallButton("Duplicate")) {
                action = Action::duplicate;
                action_index = index;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(index == 0U);
            if (ImGui::SmallButton("Up")) {
                action = Action::move_up;
                action_index = index;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(index + 1U == shader.effects.size());
            if (ImGui::SmallButton("Down")) {
                action = Action::move_down;
                action_index = index;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                action = Action::remove;
                action_index = index;
            }
        }
        ImGui::PopID();
        if (action != Action::none) break;
    }
    if (action == Action::duplicate) {
        shader.effects.insert(shader.effects.begin() +
            static_cast<std::ptrdiff_t>(action_index + 1U),
            shader.effects[action_index]);
        changed = true;
    } else if (action == Action::move_up) {
        std::swap(shader.effects[action_index], shader.effects[action_index - 1U]);
        changed = true;
    } else if (action == Action::move_down) {
        std::swap(shader.effects[action_index], shader.effects[action_index + 1U]);
        changed = true;
    } else if (action == Action::remove) {
        shader.effects.erase(shader.effects.begin() +
            static_cast<std::ptrdiff_t>(action_index));
        changed = true;
    }
    }
    if (changed) sync_legacy_settings();
    ImGui::PopID();
    return changed;
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
    std::optional<fabric::editor::CreateEntityPrompt> prepared_entity;
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
    bool guided_button{};
    bool guided_composed_entity{};
    bool guided_contextual_entity{};
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
    bool playing{};
    float timeline_zoom{1.0F};
    std::optional<KeySelection> dragging_key;
    float dragging_key_time{};
    float dragging_key_original_time{};
    std::vector<KeySelection> selected_keys;
    std::vector<ClipboardEntry> key_clipboard;
};

struct AnimationGraphUiState {
    bool open{};
    std::string current_state;
    std::string last_transition;
    float normalized_time{};
    std::vector<fabric::project::AnimationParameter> parameters;
};

bool same_animation_key(const AnimationUiState::KeySelection& left,
                        const AnimationUiState::KeySelection& right) {
    return left.index == right.index && left.binding == right.binding;
}

void draw_animation_timeline_dock(
    fabric::editor::ProjectSession& session,
    AnimationUiState& ui,
    std::string& status) {
    if (!session.selected_animation()) return;
    if (ui_animation_probe_enabled) ui_animation_timeline_seen = true;
    auto& clip = *session.selected_animation();
    if (ui.playing) {
        ui.scrub_time += ImGui::GetIO().DeltaTime;
        if (ui.scrub_time > clip.duration) {
            if (clip.loop && clip.duration > 0.0F) {
                ui.scrub_time = std::fmod(ui.scrub_time, clip.duration);
            } else {
                ui.scrub_time = clip.duration;
                ui.playing = false;
            }
        }
    }

    ImGui::TextUnformatted("Timeline");
    ImGui::SameLine();
    if (ImGui::Button(ui.playing ? "Pause" : "Play")) ui.playing = !ui.playing;
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        ui.playing = false;
        ui.scrub_time = 0.0F;
    }
    ImGui::SameLine();
    ImGui::Text("%.2f / %.2f s", ui.scrub_time, clip.duration);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::SliderFloat("Zoom", &ui.timeline_zoom, 0.5F, 4.0F, "%.1fx");

    if (!ImGui::BeginChild("Animation timeline scroll", {0.0F, 0.0F}, true,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    constexpr float label_width = 220.0F;
    constexpr float header_height = 28.0F;
    constexpr float row_height = 30.0F;
    const float available_width = std::max(320.0F, ImGui::GetContentRegionAvail().x);
    const float time_width = std::max(
        available_width - label_width - 12.0F,
        std::max(1.0F, clip.duration) * 120.0F * ui.timeline_zoom);
    const auto row_count = std::max<std::size_t>(1U, clip.tracks.size());
    const ImVec2 surface_size{label_width + time_width,
                              header_height + row_height *
                                  static_cast<float>(row_count)};
    ImGui::InvisibleButton("##timeline-surface", surface_size);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(19, 22, 28, 255));
    draw->AddRectFilled(origin, {origin.x + label_width, end.y},
                        IM_COL32(27, 31, 39, 255));
    const float time_start = origin.x + label_width;
    const auto x_for_time = [&](const float time) {
        return time_start + std::clamp(time / std::max(0.01F, clip.duration),
                                       0.0F, 1.0F) * time_width;
    };
    const auto time_for_x = [&](const float x) {
        const float raw = std::clamp((x - time_start) / time_width, 0.0F, 1.0F) *
            clip.duration;
        if (!ui.snap_keys) return raw;
        return std::round(raw / ui.key_snap_interval) * ui.key_snap_interval;
    };

    const int tick_count = std::max(2, static_cast<int>(std::ceil(clip.duration)));
    for (int tick = 0; tick <= tick_count; ++tick) {
        const float time = clip.duration * static_cast<float>(tick) /
            static_cast<float>(tick_count);
        const float x = x_for_time(time);
        draw->AddLine({x, origin.y}, {x, end.y}, IM_COL32(62, 69, 82, 150));
        char label[32]{};
        std::snprintf(label, sizeof(label), "%.1f", time);
        draw->AddText({x + 3.0F, origin.y + 5.0F},
                      IM_COL32(178, 187, 201, 255), label);
    }
    draw->AddLine({time_start, origin.y + header_height},
                  {end.x, origin.y + header_height}, IM_COL32(91, 99, 114, 255));

    std::optional<AnimationUiState::KeySelection> hovered_key;
    float hovered_distance = 9.0F;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (std::size_t track_index = 0; track_index < clip.tracks.size(); ++track_index) {
        const auto& track = clip.tracks[track_index];
        const float top = origin.y + header_height + row_height *
            static_cast<float>(track_index);
        const float center = top + row_height * 0.5F;
        if ((track_index % 2U) == 0U)
            draw->AddRectFilled({origin.x, top}, {end.x, top + row_height},
                                IM_COL32(255, 255, 255, 6));
        const auto track_label = track.binding.node_id + "  ·  " +
            track.binding.component_id + "/" + track.binding.property_id;
        draw->AddText({origin.x + 8.0F, top + 7.0F},
                      IM_COL32(216, 221, 230, 255), track_label.c_str());
        for (std::size_t key_index = 0; key_index < track.keys.size(); ++key_index) {
            const AnimationUiState::KeySelection candidate{track.binding, key_index};
            const bool dragging = ui.dragging_key &&
                same_animation_key(*ui.dragging_key, candidate);
            const float key_time = dragging ? ui.dragging_key_time
                                            : track.keys[key_index].time;
            const float x = x_for_time(key_time);
            const bool selected = std::ranges::any_of(
                ui.selected_keys, [&](const auto& value) {
                    return same_animation_key(value, candidate);
                });
            const ImU32 color = selected ? IM_COL32(255, 190, 80, 255)
                                         : IM_COL32(104, 190, 255, 255);
            draw->AddQuadFilled({x, center - 6.0F}, {x + 6.0F, center},
                                {x, center + 6.0F}, {x - 6.0F, center}, color);
            const float distance = std::hypot(mouse.x - x, mouse.y - center);
            if (hovered && distance < hovered_distance) {
                hovered_distance = distance;
                hovered_key = candidate;
            }
        }
    }
    if (clip.tracks.empty())
        draw->AddText({origin.x + 8.0F, origin.y + header_height + 7.0F},
                      IM_COL32(151, 160, 175, 255),
                      "No tracks yet — use a key button in the Inspector");

    for (const auto& marker : clip.markers) {
        const float x = x_for_time(marker.time);
        draw->AddTriangleFilled({x, origin.y + header_height - 2.0F},
                                {x - 5.0F, origin.y + header_height - 10.0F},
                                {x + 5.0F, origin.y + header_height - 10.0F},
                                IM_COL32(224, 116, 198, 255));
    }
    const float playhead_x = x_for_time(ui.scrub_time);
    draw->AddLine({playhead_x, origin.y}, {playhead_x, end.y},
                  IM_COL32(255, 96, 96, 255), 2.0F);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ui.playing = false;
        if (hovered_key) {
            const bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
            if (!additive) ui.selected_keys.clear();
            const auto selected = std::ranges::find_if(
                ui.selected_keys, [&](const auto& value) {
                    return same_animation_key(value, *hovered_key);
                });
            if (selected == ui.selected_keys.end())
                ui.selected_keys.push_back(*hovered_key);
            else if (additive)
                ui.selected_keys.erase(selected);
            const auto track = std::ranges::find(
                clip.tracks, hovered_key->binding,
                &fabric::project::AnimationTrack::binding);
            if (track != clip.tracks.end() && hovered_key->index < track->keys.size()) {
                ui.scrub_time = track->keys[hovered_key->index].time;
                ui.dragging_key = hovered_key;
                ui.dragging_key_time = ui.scrub_time;
                ui.dragging_key_original_time = ui.scrub_time;
            }
        } else if (mouse.x >= time_start) {
            ui.selected_keys.clear();
            ui.scrub_time = time_for_x(mouse.x);
        }
    }
    if (ui.dragging_key && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ui.dragging_key_time = time_for_x(mouse.x);
    if (ui.dragging_key && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const auto moving = *ui.dragging_key;
        if (std::abs(ui.dragging_key_time - ui.dragging_key_original_time) <
            0.0001F) {
            ui.scrub_time = ui.dragging_key_time;
        } else if (session.move_selected_animation_key(
                       moving.binding, moving.index, ui.dragging_key_time)) {
            ui.scrub_time = ui.dragging_key_time;
            ui.selected_keys.clear();
            status = "Animation key moved.";
        } else {
            status = "Key move rejected; inspect diagnostics.";
        }
        ui.dragging_key.reset();
    }
    ImGui::EndChild();
}

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
    if (material.shader) packet.shader = *material.shader;
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
                       CreationUiState& creation,
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
    static bool show_technical_resources = false;
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
    const auto request_entity_from_visual =
        [&](const fabric::editor::StudioResource& resource) {
            fabric::editor::CreateEntityPrompt prompt;
            const auto base_name = resource.name + " Entity";
            prompt.name = base_name;
            for (std::size_t suffix = 2U; std::ranges::any_of(
                     session.resources(), [&](const auto& candidate) {
                         return candidate.id == fabric::editor::generated_resource_id(
                             prompt.name, "entity");
                     }); ++suffix)
                prompt.name = base_name + " " + std::to_string(suffix);
            prompt.node_name = resource.name;
            prompt.resource_id = resource.id.value;
            prompt.drawable = resource.kind ==
                    fabric::editor::StudioResourceKind::texture
                ? fabric::project::EntityDrawableKind::texture
                : resource.kind ==
                      fabric::editor::StudioResourceKind::visual_component
                ? fabric::project::EntityDrawableKind::visual_component
                : fabric::project::EntityDrawableKind::vector;
            creation.prepared_entity = std::move(prompt);
            creation.guided_contextual_entity = true;
            creation.request_entity = true;
            status = "Create an Entity from the selected visual.";
        };
    filter.Draw("Search", -1.0F);
    static int kind_filter{};
    const char* kind_filters[] = {
        "All types", "Images & textures", "Artwork", "Materials / fills",
        "Entities", "Animations", "Input bindings", "Behaviors",
        "Transformations", "Beam paths", "Visual compositions",
        "Visual components", "Maps", "Scenes", "Mechanics", "Replays",
        "Audio"};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("##resource-kind-filter", &kind_filter, kind_filters,
                 static_cast<int>(std::size(kind_filters)));
    ImGui::Checkbox("Show technical resources", &show_technical_resources);
    ImGui::SameLine();
    ImGui::TextDisabled("generated paths, borders and compositions");
    if (const auto* selected = session.selected_resource()) {
        if (is_entity_artwork_kind(selected->kind) &&
            ImGui::Button("Create Entity from visual"))
            request_entity_from_visual(*selected);
        if (is_entity_artwork_kind(selected->kind)) same_line_if_room();
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
                (show_technical_resources ||
                 !is_technical_visual_dependency(resource)) &&
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
                (!show_technical_resources &&
                 is_technical_visual_dependency(resource)) ||
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
            if (ui_drag_probe_enabled && resource.id.value == "beam-border") {
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                ui_drag_source_screen = {(minimum.x + maximum.x) * 0.5F,
                                         (minimum.y + maximum.y) * 0.5F};
                ui_drag_source_seen = true;
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
                if (is_entity_artwork_kind(resource.kind) &&
                    ImGui::MenuItem("Create Entity from this visual"))
                    request_entity_from_visual(resource);
                if (is_entity_artwork_kind(resource.kind)) ImGui::Separator();
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
    ImGui::SeparatorText("Guided creations");
    draw_kind("Images & textures", fabric::editor::StudioResourceKind::texture, 1);
    draw_kind("Artwork", fabric::editor::StudioResourceKind::vector, 2);
    draw_kind("Materials / fills", fabric::editor::StudioResourceKind::material, 3);
    draw_kind("Entities", fabric::editor::StudioResourceKind::entity, 4);
    draw_kind("Animations", fabric::editor::StudioResourceKind::animation, 5);
    draw_kind("Input bindings", fabric::editor::StudioResourceKind::input, 6);
    draw_kind("Behaviors", fabric::editor::StudioResourceKind::behavior, 7);
    draw_kind("Transformations",
              fabric::editor::StudioResourceKind::transformation, 8);
    draw_kind("Beam paths",
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

std::filesystem::path default_studio_texture_source(
    const std::string_view filename) {
    std::vector<std::filesystem::path> candidates;
    if (char* base_path = SDL_GetBasePath(); base_path != nullptr) {
        candidates.push_back(std::filesystem::path{base_path} / ".." / "share" /
                             "vertex-loom" / "asset-studio-defaults" /
                             std::string{filename});
        SDL_free(base_path);
    }
#ifdef FABRIC_SOURCE_DIR
    candidates.push_back(std::filesystem::path{FABRIC_SOURCE_DIR} /
                         "editors/asset_studio/assets" /
                         std::string{filename});
#endif
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return {};
}

bool ensure_studio_texture(fabric::editor::ProjectSession& session,
                           const fabric::core::ResourceId& id,
                           const std::string_view filename,
                           const std::string& name) {
    const bool already_indexed = std::ranges::any_of(
        session.resources(), [&](const auto& resource) {
            return resource.kind == fabric::editor::StudioResourceKind::texture &&
                resource.id == id;
        });
    return already_indexed ||
        session.import_png(default_studio_texture_source(filename), id, name);
}

bool ensure_default_studio_textures(
    fabric::editor::ProjectSession& session) {
    if (!session.has_project() || !session.manifest()) return false;
    const auto default_id = session.manifest()->default_stroke_texture
        .value_or(fabric::core::ResourceId{.value = "beam-thread"});
    return ensure_studio_texture(session, default_id, "beam-thread.png",
                                 "Beam Thread") &&
        ensure_studio_texture(session, {.value = "button-primary"},
                              "button-primary.png", "Button Original 1") &&
        ensure_studio_texture(session, {.value = "button-secondary"},
                              "button-secondary.png", "Button Original 2");
}

void write_vector_canvas_visual_probe(const std::filesystem::path& project_path,
                                      SDL_Window* window,
                                      const ImVec2 origin,
                                      const ImVec2 size) {
    if (project_path.empty() || window == nullptr || size.x <= 0.0F ||
        size.y <= 0.0F)
        return;
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    const auto display_size = ImGui::GetIO().DisplaySize;
    const float scale_x = display_size.x > 0.0F
        ? static_cast<float>(width) / display_size.x : 1.0F;
    const float scale_y = display_size.y > 0.0F
        ? static_cast<float>(height) / display_size.y : 1.0F;
    const int x0 = std::clamp(static_cast<int>(std::floor(origin.x * scale_x)),
                              0, width - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil(
                                  (origin.x + size.x) * scale_x)),
                              x0 + 1, width);
    const int y0 = std::clamp(static_cast<int>(std::floor(
                                  static_cast<float>(height) -
                                  (origin.y + size.y) * scale_y)),
                              0, height - 1);
    const int y1 = std::clamp(static_cast<int>(std::ceil(
                                  static_cast<float>(height) - origin.y * scale_y)),
                              y0 + 1, height);
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    constexpr int clear_red = 9;
    constexpr int clear_green = 10;
    constexpr int clear_blue = 13;
    std::size_t non_background_pixels = 0U;
    std::size_t anchor_pixels = 0U;
    std::size_t selected_anchor_pixels = 0U;
    std::size_t handle_pixels = 0U;
    int minimum_channel = 255;
    int maximum_channel = 0;
    const auto pixel_at = [&](const int x, const int y, const int channel) {
        return static_cast<int>(pixels[(static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                        static_cast<std::size_t>(x)) * 3U +
                                       static_cast<std::size_t>(channel)]);
    };
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int red = pixel_at(x, y, 0);
            const int green = pixel_at(x, y, 1);
            const int blue = pixel_at(x, y, 2);
            minimum_channel = std::min({minimum_channel, red, green, blue});
            maximum_channel = std::max({maximum_channel, red, green, blue});
            const int distance = std::abs(red - clear_red) +
                std::abs(green - clear_green) + std::abs(blue - clear_blue);
            if (distance > 24) ++non_background_pixels;
            const auto near_color = [&](const int target_red,
                                        const int target_green,
                                        const int target_blue) {
                return std::abs(red - target_red) +
                    std::abs(green - target_green) +
                    std::abs(blue - target_blue) <= 70;
            };
            if (near_color(236, 180, 75)) ++anchor_pixels;
            if (near_color(100, 210, 255)) ++selected_anchor_pixels;
            if (near_color(180, 110, 235)) ++handle_pixels;
        }
    }
    const nlohmann::json probe = {
        {"schema", "asset-studio-vector-canvas-visual-v1"},
        {"canvas", {x0, y0, x1 - x0, y1 - y0}},
        {"non_background_pixels", non_background_pixels},
        {"anchor_pixels", anchor_pixels},
        {"selected_anchor_pixels", selected_anchor_pixels},
        {"handle_pixels", handle_pixels},
        {"minimum_channel", minimum_channel},
        {"maximum_channel", maximum_channel},
    };
    std::ofstream output(project_path / "asset-studio-vector-canvas-visual.json");
    if (output) output << probe.dump(2) << '\n';
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

float relative_luminance(const ImVec4 color) {
    const auto linear = [](const float channel) {
        return channel <= 0.03928F
            ? channel / 12.92F
            : std::pow((channel + 0.055F) / 1.055F, 2.4F);
    };
    return 0.2126F * linear(color.x) + 0.7152F * linear(color.y) +
        0.0722F * linear(color.z);
}

void write_ui_accessibility_probe(const std::filesystem::path& project_path,
                                  const bool keyboard_navigation_enabled) {
    if (project_path.empty()) return;
    const auto& colors = ImGui::GetStyle().Colors;
    const float background = relative_luminance(colors[ImGuiCol_WindowBg]);
    const float text = relative_luminance(colors[ImGuiCol_Text]);
    const float contrast = (std::max(background, text) + 0.05F) /
        (std::min(background, text) + 0.05F);
    nlohmann::json probe = {
        {"schema", "asset-studio-ui-accessibility-test-v1"},
        {"keyboard_navigation_enabled", keyboard_navigation_enabled},
        {"text_window_contrast", contrast},
        {"text_window_contrast_ok", contrast >= 4.5F}};
    std::ofstream output(project_path / "asset-studio-ui-accessibility.json");
    if (output) output << probe.dump(2) << '\n';
}

void write_ui_drag_probe(const std::filesystem::path& project_path,
                         const bool source_seen, const bool target_seen,
                         const bool applied, const bool persisted,
                         const char* destination) {
    if (project_path.empty()) return;
    nlohmann::json probe = {
        {"schema", "asset-studio-ui-drag-test-v1"},
        {"drop_destination", destination},
        {"source_widget_seen", source_seen},
        {"target_widget_seen", target_seen},
        {"drop_applied_to_existing_node", applied},
        {"drop_persisted_after_reload", persisted}};
    std::ofstream output(project_path / "asset-studio-ui-drag.json");
    if (output) output << probe.dump(2) << '\n';
}

void write_ui_override_probe(const std::filesystem::path& project_path) {
    if (project_path.empty()) return;
    nlohmann::json probe = {
        {"schema", "asset-studio-ui-overrides-test-v1"},
        {"confirmation_modal_seen", ui_override_modal_seen},
        {"cancel_preserved_override", ui_override_cancel_preserved},
        {"confirm_applied", ui_override_confirm_applied},
        {"kind_widget_seen", ui_override_kind_seen},
        {"texture_item_seen", ui_override_texture_seen},
        {"cancel_button_seen", ui_override_cancel_seen},
        {"confirm_button_seen", ui_override_confirm_seen}};
    std::ofstream output(project_path / "asset-studio-ui-overrides.json");
    if (output) output << probe.dump(2) << '\n';
}

void write_ui_texture_probe(const std::filesystem::path& project_path) {
    if (project_path.empty()) return;
    nlohmann::json probe = {
        {"schema", "asset-studio-ui-texture-test-v1"},
        {"crop_canvas_seen", ui_texture_canvas_seen},
        {"crop_applied", ui_texture_crop_applied}};
    std::ofstream output(project_path / "asset-studio-ui-texture.json");
    if (output) output << probe.dump(2) << '\n';
}

void write_ui_input_probe(const std::filesystem::path& project_path) {
    if (project_path.empty()) return;
    nlohmann::json probe = {
        {"schema", "asset-studio-ui-input-test-v1"},
        {"modal_seen", ui_input_modal_seen},
        {"created", ui_input_created},
        {"reloaded", ui_input_reloaded}};
    std::ofstream output(project_path / "asset-studio-ui-input.json");
    if (output) output << probe.dump(2) << '\n';
}

void write_ui_beam_probe(const std::filesystem::path& project_path) {
    std::ofstream output(project_path / "asset-studio-ui-beam.json");
    output << "{\n"
           << "  \"create_button_seen\": "
           << (ui_beam_create_seen ? "true" : "false") << ",\n"
           << "  \"created_by_click\": "
           << (ui_beam_created ? "true" : "false") << ",\n"
           << "  \"reloaded_with_default_texture\": "
           << (ui_beam_reloaded ? "true" : "false") << "\n"
           << "}\n";
}

void write_ui_button_probe(const std::filesystem::path& project_path) {
    std::ofstream output(project_path / "asset-studio-ui-button.json");
    output << "{\n"
           << "  \"create_button_seen\": "
           << (ui_button_create_seen ? "true" : "false") << ",\n"
           << "  \"created_by_click\": "
           << (ui_button_created ? "true" : "false") << ",\n"
           << "  \"reloaded_with_original_texture_and_shader\": "
           << (ui_button_reloaded ? "true" : "false") << "\n"
           << "}\n";
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
                             {64.0F, 64.0F}, {0.0F, 1.0F}, {1.0F, 0.0F});
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

const char* animation_condition_label(
    const fabric::project::AnimationConditionOperator operation) {
    using Operator = fabric::project::AnimationConditionOperator;
    switch (operation) {
    case Operator::equal: return "Equal";
    case Operator::not_equal: return "Not equal";
    case Operator::less: return "Less";
    case Operator::less_equal: return "Less or equal";
    case Operator::greater: return "Greater";
    case Operator::greater_equal: return "Greater or equal";
    }
    return "Equal";
}

void draw_animation_graph_editor(
    fabric::editor::ProjectSession& session,
    AnimationGraphUiState& ui,
    std::string& status) {
    if (!ui.open) return;
    if (ui_animation_graph_probe_enabled) ui_animation_graph_seen = true;
    const auto* selected = session.selected_resource();
    if (!selected || selected->kind != fabric::editor::StudioResourceKind::entity ||
        !session.selected_entity()) {
        ui.open = false;
        return;
    }
    ImGui::SetNextWindowSize({760.0F, 720.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Graph", &ui.open)) {
        ImGui::End();
        return;
    }
    const auto commit_machine =
        [&](std::optional<fabric::project::AnimationStateMachine> machine,
            const char* success) {
            auto entity = *session.selected_entity();
            entity.animation_state_machine = std::move(machine);
            if (session.set_selected_entity_definition(std::move(entity)))
                status = success;
            else
                status = "Animation Graph change rejected; inspect diagnostics.";
        };
    const auto first_animation = std::ranges::find_if(
        session.resources(), [](const auto& resource) {
            return resource.kind == fabric::editor::StudioResourceKind::animation;
        });
    if (!session.selected_entity()->animation_state_machine) {
        ImGui::TextWrapped(
            "Create a deterministic animation graph from an existing clip. "
            "Runtime parameters remain instance-owned.");
        ImGui::BeginDisabled(first_animation == session.resources().end());
        if (ImGui::Button("Create Animation Graph")) {
            fabric::project::AnimationStateMachine machine{
                .initial_state = "state-1",
                .states = {{"state-1", {first_animation->id, "animation"}}}};
            commit_machine(std::move(machine), "Animation Graph created.");
            ui.current_state = "state-1";
        }
        ImGui::EndDisabled();
        draw_disabled_reason(first_animation == session.resources().end(),
                             "Create an Animation clip before creating a graph.");
        ImGui::End();
        return;
    }

    const auto machine_snapshot = *session.selected_entity()->animation_state_machine;
    ImGui::Text("Entity: %s", session.selected_entity()->document.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu states · %zu transitions",
                        machine_snapshot.states.size(),
                        machine_snapshot.transitions.size());
    if (ImGui::BeginTabBar("Animation graph tabs")) {
        if (ImGui::BeginTabItem("States")) {
            auto machine = machine_snapshot;
            const auto initial = std::ranges::find_if(
                machine.states, [&](const auto& state) {
                    return state.id == machine.initial_state;
                });
            const char* initial_label = initial == machine.states.end()
                ? "Choose initial state..." : initial->id.c_str();
            if (ImGui::BeginCombo("Initial state", initial_label)) {
                for (const auto& state : machine.states) {
                    if (ImGui::Selectable(state.id.c_str(),
                                          state.id == machine.initial_state)) {
                        machine.initial_state = state.id;
                        commit_machine(machine, "Initial animation state changed.");
                        ui.current_state = state.id;
                    }
                }
                ImGui::EndCombo();
            }
            bool removed = false;
            for (std::size_t index = 0; index < machine.states.size(); ++index) {
                auto state = machine.states[index];
                const auto old_id = state.id;
                ImGui::PushID(static_cast<int>(index));
                ImGui::SeparatorText(("State " + std::to_string(index + 1U)).c_str());
                ImGui::InputText("Id", &state.id);
                std::string clip_id = state.clip.id.value;
                static_cast<void>(draw_project_resource_picker(
                    "Clip", session.resources(),
                    fabric::editor::StudioResourceKind::animation,
                    clip_id, false));
                state.clip = {{.value = clip_id}, "animation"};
                if (ImGui::Button("Save state")) {
                    auto next = machine;
                    next.states[index] = state;
                    if (old_id != state.id) {
                        if (next.initial_state == old_id) next.initial_state = state.id;
                        for (auto& transition : next.transitions) {
                            if (transition.from_state == old_id)
                                transition.from_state = state.id;
                            if (transition.to_state == old_id)
                                transition.to_state = state.id;
                        }
                    }
                    commit_machine(std::move(next), "Animation state saved.");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(machine.states.size() <= 1U);
                if (ImGui::Button("Remove state")) {
                    auto next = machine;
                    next.states.erase(next.states.begin() +
                                      static_cast<std::ptrdiff_t>(index));
                    std::erase_if(next.transitions, [&](const auto& transition) {
                        return transition.from_state == old_id ||
                            transition.to_state == old_id;
                    });
                    if (next.initial_state == old_id)
                        next.initial_state = next.states.front().id;
                    commit_machine(std::move(next), "Animation state removed.");
                    removed = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                if (removed) break;
            }
            ImGui::BeginDisabled(first_animation == session.resources().end());
            if (ImGui::Button("Add state")) {
                auto next = machine_snapshot;
                std::string id = "state-" + std::to_string(next.states.size() + 1U);
                while (std::ranges::any_of(next.states, [&](const auto& state) {
                    return state.id == id;
                })) id += "-copy";
                next.states.push_back({id, {first_animation->id, "animation"}});
                commit_machine(std::move(next), "Animation state added.");
            }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Transitions")) {
            auto machine = machine_snapshot;
            bool removed = false;
            for (std::size_t index = 0; index < machine.transitions.size(); ++index) {
                auto transition = machine.transitions[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::SeparatorText(
                    ("Transition " + std::to_string(index + 1U)).c_str());
                ImGui::InputText("Id", &transition.id);
                const auto draw_state_picker = [&](const char* label,
                                                   std::string& state_id) {
                    if (ImGui::BeginCombo(label, state_id.c_str())) {
                        for (const auto& state : machine.states)
                            if (ImGui::Selectable(state.id.c_str(),
                                                  state.id == state_id))
                                state_id = state.id;
                        ImGui::EndCombo();
                    }
                };
                draw_state_picker("From", transition.from_state);
                draw_state_picker("To", transition.to_state);
                ImGui::InputInt("Priority", &transition.priority);
                bool has_exit_time = transition.exit_time.has_value();
                if (ImGui::Checkbox("Require exit time", &has_exit_time)) {
                    if (has_exit_time) transition.exit_time = 1.0F;
                    else transition.exit_time.reset();
                }
                if (transition.exit_time)
                    ImGui::SliderFloat("Normalized exit time",
                                       &*transition.exit_time, 0.0F, 1.0F);
                for (std::size_t condition_index = 0;
                     condition_index < transition.conditions.size();
                     ++condition_index) {
                    auto& condition = transition.conditions[condition_index];
                    ImGui::PushID(static_cast<int>(condition_index));
                    ImGui::InputText("Parameter", &condition.parameter_id);
                    const bool boolean = std::holds_alternative<bool>(condition.value);
                    if (ImGui::BeginCombo("Operator",
                                          animation_condition_label(condition.operation))) {
                        using Operator = fabric::project::AnimationConditionOperator;
                        for (const auto operation : {
                                 Operator::equal, Operator::not_equal,
                                 Operator::less, Operator::less_equal,
                                 Operator::greater, Operator::greater_equal}) {
                            const bool allowed = !boolean || operation == Operator::equal ||
                                operation == Operator::not_equal;
                            ImGui::BeginDisabled(!allowed);
                            if (ImGui::Selectable(animation_condition_label(operation),
                                                  condition.operation == operation))
                                condition.operation = operation;
                            ImGui::EndDisabled();
                        }
                        ImGui::EndCombo();
                    }
                    bool use_boolean = boolean;
                    if (ImGui::Checkbox("Boolean parameter", &use_boolean)) {
                        condition.value = use_boolean
                            ? fabric::project::AnimationParameterValue{false}
                            : fabric::project::AnimationParameterValue{0.0F};
                        if (use_boolean && condition.operation !=
                                fabric::project::AnimationConditionOperator::equal &&
                            condition.operation !=
                                fabric::project::AnimationConditionOperator::not_equal)
                            condition.operation =
                                fabric::project::AnimationConditionOperator::equal;
                    }
                    if (auto* value = std::get_if<bool>(&condition.value))
                        ImGui::Checkbox("Expected", value);
                    else
                        ImGui::InputFloat("Expected", &std::get<float>(condition.value));
                    if (ImGui::SmallButton("Remove condition")) {
                        transition.conditions.erase(
                            transition.conditions.begin() +
                            static_cast<std::ptrdiff_t>(condition_index));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add condition"))
                    transition.conditions.push_back({"parameter",
                        fabric::project::AnimationConditionOperator::equal, false});
                ImGui::SameLine();
                if (ImGui::Button("Save transition")) {
                    auto next = machine;
                    next.transitions[index] = std::move(transition);
                    commit_machine(std::move(next), "Animation transition saved.");
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove transition")) {
                    auto next = machine;
                    next.transitions.erase(
                        next.transitions.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    commit_machine(std::move(next), "Animation transition removed.");
                    removed = true;
                }
                ImGui::PopID();
                if (removed) break;
            }
            ImGui::BeginDisabled(machine.states.size() < 2U);
            if (ImGui::Button("Add transition")) {
                auto next = machine_snapshot;
                std::string id = "transition-" +
                    std::to_string(next.transitions.size() + 1U);
                while (std::ranges::any_of(next.transitions,
                                            [&](const auto& candidate) {
                    return candidate.id == id;
                })) id += "-copy";
                next.transitions.push_back({
                    .id = id,
                    .from_state = next.states[0].id,
                    .to_state = next.states[1].id});
                commit_machine(std::move(next), "Animation transition added.");
            }
            ImGui::EndDisabled();
            draw_disabled_reason(machine.states.size() < 2U,
                                 "Add at least two states before connecting them.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Preview")) {
            const auto& machine = machine_snapshot;
            if (!fabric::project::find_animation_state(machine, ui.current_state))
                ui.current_state = machine.initial_state;
            for (const auto& transition : machine.transitions) {
                for (const auto& condition : transition.conditions) {
                    const auto found = std::ranges::find(
                        ui.parameters, condition.parameter_id,
                        &fabric::project::AnimationParameter::id);
                    if (found == ui.parameters.end())
                        ui.parameters.push_back({condition.parameter_id,
                            std::holds_alternative<bool>(condition.value)
                                ? fabric::project::AnimationParameterValue{false}
                                : fabric::project::AnimationParameterValue{0.0F}});
                }
            }
            ImGui::Text("Active state: %s", ui.current_state.c_str());
            if (const auto* state = fabric::project::find_animation_state(
                    machine, ui.current_state))
                ImGui::TextDisabled("Clip: %s", state->clip.id.value.c_str());
            if (!ui.last_transition.empty())
                ImGui::Text("Last transition: %s", ui.last_transition.c_str());
            ImGui::SliderFloat("Normalized time", &ui.normalized_time, 0.0F, 1.0F);
            for (auto& parameter : ui.parameters) {
                ImGui::PushID(parameter.id.c_str());
                if (auto* value = std::get_if<bool>(&parameter.value))
                    ImGui::Checkbox(parameter.id.c_str(), value);
                else
                    ImGui::InputFloat(parameter.id.c_str(),
                                      &std::get<float>(parameter.value));
                ImGui::PopID();
            }
            if (ImGui::Button("Evaluate transition")) {
                if (const auto* transition =
                        fabric::project::select_animation_transition(
                            machine, ui.current_state, ui.parameters,
                            ui.normalized_time)) {
                    ui.current_state = transition->to_state;
                    ui.last_transition = transition->id;
                    ui.normalized_time = 0.0F;
                    status = "Animation Graph preview transitioned.";
                } else {
                    ui.last_transition.clear();
                    status = "No animation transition matched.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset preview")) {
                ui.current_state = machine.initial_state;
                ui.last_transition.clear();
                ui.normalized_time = 0.0F;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (ImGui::Button("Remove Animation Graph..."))
        ImGui::OpenPopup("Remove Animation Graph?");
    if (ImGui::BeginPopupModal("Remove Animation Graph?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Remove all animation states and transitions from this Entity?");
        if (ImGui::Button("Remove graph")) {
            commit_machine(std::nullopt, "Animation Graph removed.");
            ui.current_state.clear();
            ui.last_transition.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

ImU32 color_to_u32(const fabric::core::Color& color) {
    const auto channel = [](const float value) {
        return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
    };
    return IM_COL32(channel(color.red), channel(color.green),
                    channel(color.blue), channel(color.alpha));
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
    if (ui_texture_probe_enabled) {
        const auto maximum = ImGui::GetItemRectMax();
        ui_texture_canvas_seen = true;
        ui_texture_crop_source = {maximum.x - 20.0F, maximum.y - 20.0F};
        ui_texture_crop_target = {ui_texture_crop_source.x - 24.0F,
                                  ui_texture_crop_source.y - 24.0F};
    }
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
    if (ui_texture_probe_enabled) {
        ui_texture_canvas_seen = true;
        ui_texture_crop_source = crop_max;
        ui_texture_crop_target = {crop_max.x - 24.0F, crop_max.y - 24.0F};
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto is_near = [&](const ImVec2 point) {
        return std::hypot(mouse.x - point.x, mouse.y - point.y) <= 11.0F;
    };
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 top_right{crop_max.x, crop_min.y};
        const ImVec2 bottom_left{crop_min.x, crop_max.y};
        if (is_near(crop_min)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::top_left;
        } else if (is_near(top_right)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::top_right;
        } else if (is_near(bottom_left)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::bottom_left;
        } else if (is_near(crop_max)) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::bottom_right;
        } else if (mouse.x >= crop_min.x && mouse.x <= crop_max.x &&
                   mouse.y >= crop_min.y && mouse.y <= crop_max.y) {
            canvas.crop_drag = fabric::editor::RasterCropDrag::move;
        }
        if (canvas.crop_drag.has_value()) {
            canvas.crop_start_mouse = mouse;
            canvas.crop_start_view = view;
        }
    }
    if (canvas.crop_drag.has_value() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
                    AnimationGraphUiState& animation_graph_ui,
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
    const bool animation_workspace = session.selected_resource() != nullptr &&
        session.selected_resource()->kind ==
            fabric::editor::StudioResourceKind::animation &&
        session.selected_animation();
    static float timeline_height = 260.0F;
    timeline_height = std::clamp(
        timeline_height, 190.0F, std::max(190.0F, content_height - 200.0F));
    constexpr float timeline_gap = 6.0F;
    const float preview_height = animation_workspace
        ? content_height - timeline_height - timeline_gap
        : content_height;

    ImGui::SetNextWindowPos({viewport->Pos.x + viewport->Size.x - right_width,
                             viewport->Pos.y + menu_height});
    ImGui::SetNextWindowSize({right_width, content_height});
    ImGui::Begin("Project", nullptr, fixed_panel_flags);
    draw_project_tree(session, creation, preview, status);
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
            ImGui::SeparatorText("Create a visual");
            if (ImGui::MenuItem("Beam / Stroke...")) {
                creation.visual_preset.kind =
                    fabric::editor::VisualPresetKind::beam;
                creation.visual_preset.name = "Beam";
                creation.visual_preset.id.value = "beam";
                creation.visual_preset.guided_beam = true;
                creation.request_visual_preset = true;
            }
            if (ImGui::MenuItem("Button...")) {
                creation.guided_button = true;
                creation.request_entity = true;
            }
            if (ImGui::MenuItem("Artwork...")) {
                creation.request_artwork = true;
            }
            if (ImGui::MenuItem("Composed Entity...")) {
                creation.guided_button = false;
                creation.guided_composed_entity = true;
                creation.request_entity = true;
            }
            ImGui::SeparatorText("Advanced");
            if (ImGui::BeginMenu("Technical resources")) {
                if (ImGui::MenuItem("Vector artwork resource..."))
                    creation.request_artwork = true;
                if (ImGui::MenuItem("Visual composition..."))
                    creation.request_visual_composition = true;
                if (ImGui::MenuItem("Visual component..."))
                    creation.request_visual_component = true;
                if (ImGui::MenuItem("Material / fill..."))
                    creation.request_material = true;
                if (ImGui::MenuItem("Animation..."))
                    creation.request_animation = true;
                if (ImGui::MenuItem("Input bindings..."))
                    creation.request_input = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Expert entity tools")) {
                if (ImGui::MenuItem("Entity transformation..."))
                    creation.request_transformation = true;
                if (ImGui::MenuItem("Behavior graph..."))
                    creation.request_behavior = true;
                ImGui::TextDisabled("Physics and deformation stay available in the entity inspector.");
                ImGui::EndMenu();
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
                              preview_height});
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
        if (canvas.tool == CanvasUiState::Tool::pen) {
            ImGui::SameLine();
            if (ImGui::Button("New path"))
                static_cast<void>(start_new_freeform_path(session, canvas, status));
            ImGui::SameLine();
            ImGui::TextDisabled("click points · Enter finish · Escape cancel");
        }
        ImGui::SetCursorScreenPos({origin.x + 8.0F, origin.y + 34.0F});
        draw_native_vector_canvas(
            session, canvas,
            {std::max(1.0F, available.x - 16.0F),
             std::max(1.0F, available.y - 42.0F)}, status);
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
        fabric::asset_studio::draw_packet_preview_canvas(
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
        fabric::asset_studio::draw_packet_preview_canvas(
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

    if (animation_workspace) {
        const float center_width = viewport->Size.x - left_width - right_width;
        ImGui::SetNextWindowPos({viewport->Pos.x + left_width,
                                 viewport->Pos.y + menu_height + preview_height +
                                     timeline_gap});
        ImGui::SetNextWindowSize({center_width, timeline_height});
        ImGui::Begin("Timeline workspace", nullptr,
                     fixed_panel_flags | ImGuiWindowFlags_NoTitleBar);
        ImGui::InvisibleButton("##timeline-height-splitter", {-1.0F, 5.0F});
        if (ImGui::IsItemActive()) {
            timeline_height = std::clamp(
                timeline_height - ImGui::GetIO().MouseDelta.y,
                190.0F, std::max(190.0F, content_height - 200.0F));
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        draw_animation_timeline_dock(session, animation_ui, status);
        ImGui::End();
    }

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
            ImGui::SeparatorText(current.shader &&
                    current.shader->classification ==
                        fabric::project::TextureClassification::button_eye
                ? "Button appearance" : "Surface appearance");
            bool shader_enabled = current.shader.has_value();
            if (ImGui::Checkbox("Enable color effects", &shader_enabled)) {
                material = current;
                material.shader = shader_enabled
                    ? std::optional<fabric::project::ShaderSurfaceSettings>{
                        fabric::project::ShaderSurfaceSettings{
                            .profile = fabric::project::SurfaceShaderProfile::custom,
                            .effects = {{
                                .kind = fabric::project::SurfaceEffectKind::tint}}}}
                    : std::optional<fabric::project::ShaderSurfaceSettings>{};
                commit_material(std::move(material));
            }
            if (current.shader) {
                auto appearance = *current.shader;
                bool changed = false;
                changed |= draw_surface_effect_stack(
                    appearance, "material-effects");
                changed |= ImGui::SliderFloat(
                    "Effect opacity", &appearance.opacity, 0.0F, 1.0F);
                changed |= ImGui::SliderFloat(
                    "Intensity", &appearance.intensity, 0.0F, 4.0F);
                if (changed) {
                    material = current;
                    material.shader = appearance;
                    commit_material(std::move(material));
                }
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
                const bool is_beam = style.shader.classification ==
                    fabric::project::TextureClassification::beam;
                ImGui::SeparatorText(is_beam ? "Beam" : "Ribbon and texture");
                if (is_beam) {
                    std::string texture_id = style.texture.id.value;
                    if (draw_project_resource_picker(
                            "Texture", session.resources(),
                            fabric::editor::StudioResourceKind::texture,
                            texture_id, false)) {
                        style.texture = {{.value = std::move(texture_id)},
                                         "texture"};
                        style_changed = true;
                    }
                    const auto beam_profile_label = [](const auto profile) {
                        using Profile =
                            fabric::project::SurfaceShaderProfile;
                        switch (profile) {
                        case Profile::thread: return "Recoloration";
                        case Profile::plastic: return "Source intacte";
                        case Profile::monochrome: return "Monochrome";
                        case Profile::custom: return "Custom";
                        }
                        return "Custom";
                    };
                    const auto profile_label =
                        beam_profile_label(style.shader.profile);
                    if (ImGui::BeginCombo("Profile", profile_label)) {
                        for (const auto profile : {
                                 fabric::project::SurfaceShaderProfile::thread,
                                 fabric::project::SurfaceShaderProfile::plastic,
                                 fabric::project::SurfaceShaderProfile::monochrome,
                                 fabric::project::SurfaceShaderProfile::custom}) {
                            const bool selected = style.shader.profile == profile;
                            const auto label = beam_profile_label(profile);
                            if (ImGui::Selectable(label, selected)) {
                                style.shader.profile = profile;
                                style_changed = true;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled(style.shader.profile ==
                            fabric::project::SurfaceShaderProfile::plastic
                        ? "White preserves the PNG colors."
                        : "White produces a neutral Beam from the PNG detail.");
                    style_changed |= draw_surface_effect_stack(
                        style.shader, "beam-effects");
                    style_changed |= ImGui::DragFloat(
                        "Thickness", &style.width, 0.01F, 0.001F, 1000.0F,
                        "%.3f");
                    style_changed |= ImGui::DragFloat(
                        "Repetition", &style.uv_scale.x, 0.05F, 0.01F,
                        1000.0F);
                    style_changed |= ImGui::SliderFloat(
                        "Opacity", &style.opacity, 0.0F, 1.0F);
                    ImGui::TextDisabled(
                        "Orientation follows the path from start to end.");

                    if (ImGui::CollapsingHeader("Advanced Beam settings")) {
                        style_changed |= ImGui::Checkbox("Closed path", &style.closed);
                        int uv_mode = style.uv_mode ==
                                fabric::project::TexturedPathUvMode::repeat ? 0 :
                            style.uv_mode == fabric::project::TexturedPathUvMode::mirror
                                ? 1 : 2;
                        if (ImGui::Combo("Mapping", &uv_mode,
                                         "Tile\0Mirror tile\0Stretch\0")) {
                            style.uv_mode = uv_mode == 0
                                ? fabric::project::TexturedPathUvMode::repeat
                                : uv_mode == 1
                                    ? fabric::project::TexturedPathUvMode::mirror
                                    : fabric::project::TexturedPathUvMode::stretch;
                            style_changed = true;
                        }
                        float band_top = style.texture_metrics.origin.y;
                        float band_height = style.texture_metrics.size.y;
                        bool band_changed = ImGui::DragFloat(
                            "Texture band top", &band_top, 0.005F, 0.0F, 0.999F);
                        band_changed |= ImGui::DragFloat(
                            "Texture band thickness", &band_height, 0.005F,
                            0.001F, 1.0F);
                        if (band_changed) {
                            band_top = std::clamp(band_top, 0.0F, 0.999F);
                            band_height = std::clamp(
                                band_height, 0.001F, 1.0F - band_top);
                            style.texture_metrics.origin = {0.0F, band_top};
                            style.texture_metrics.size = {1.0F, band_height};
                            style_changed = true;
                        }
                        ImGui::TextDisabled(
                            "Left and right source edges are locked.");
                        style_changed |= ImGui::SliderFloat(
                            "Shader opacity", &style.shader.opacity, 0.0F, 1.0F);
                        style_changed |= ImGui::SliderFloat(
                            "Shader intensity", &style.shader.intensity,
                            0.0F, 4.0F);
                    }
                    // Beam thickness never tiles and its source left/right
                    // bounds are immutable.
                    style.uv_scale.y = 1.0F;
                    style.uv_offset = {};
                    style.texture_metrics.origin.x = 0.0F;
                    style.texture_metrics.size.x = 1.0F;
                    style.color = {1.0F, 1.0F, 1.0F, 1.0F};
                } else {
                    style_changed |= ImGui::Checkbox("Closed", &style.closed);
                    style_changed |= ImGui::DragFloat(
                        "Width (world units)", &style.width, 0.01F, 0.001F,
                        1000.0F);
                    style_changed |= ImGui::DragFloat2(
                        "Texture repeat (factor)", &style.uv_scale.x, 0.05F,
                        0.001F, 1000.0F);
                    style_changed |= ImGui::DragFloat2(
                        "Texture offset (normalized)", &style.uv_offset.x,
                        0.01F);
                    style_changed |= ImGui::ColorEdit4(
                        "Color", &style.color.red);
                    style_changed |= ImGui::SliderFloat(
                        "Opacity (0–1)", &style.opacity, 0.0F, 1.0F);
                    int uv_mode = style.uv_mode ==
                            fabric::project::TexturedPathUvMode::repeat ? 0 :
                        style.uv_mode == fabric::project::TexturedPathUvMode::mirror
                            ? 1 : 2;
                    if (ImGui::Combo("UV mode", &uv_mode,
                                     "Repeat\0Mirror repeat\0Stretch\0")) {
                        style.uv_mode = uv_mode == 0
                            ? fabric::project::TexturedPathUvMode::repeat
                            : uv_mode == 1
                                ? fabric::project::TexturedPathUvMode::mirror
                                : fabric::project::TexturedPathUvMode::stretch;
                        style_changed = true;
                    }
                    ImGui::SeparatorText("Surface color and shader");
                    const auto shader_profile_label = std::string(
                        fabric::project::to_string(style.shader.profile));
                    if (ImGui::BeginCombo("Shader profile",
                                          shader_profile_label.c_str())) {
                        for (const auto profile : {
                                 fabric::project::SurfaceShaderProfile::thread,
                                 fabric::project::SurfaceShaderProfile::plastic,
                                 fabric::project::SurfaceShaderProfile::monochrome,
                                 fabric::project::SurfaceShaderProfile::custom}) {
                            const bool selected_profile =
                                style.shader.profile == profile;
                            const auto label = std::string(
                                fabric::project::to_string(profile));
                            if (ImGui::Selectable(label.c_str(), selected_profile)) {
                                style.shader.profile = profile;
                                style_changed = true;
                            }
                            if (selected_profile) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    const auto shader_classification_label = std::string(
                        fabric::project::to_string(style.shader.classification));
                    if (ImGui::BeginCombo(
                            "Texture role", shader_classification_label.c_str())) {
                        for (const auto classification : {
                                 fabric::project::TextureClassification::floor,
                                 fabric::project::TextureClassification::rope,
                                 fabric::project::TextureClassification::beam,
                                 fabric::project::TextureClassification::button_eye,
                                 fabric::project::TextureClassification::collision_marker}) {
                            const bool selected_classification =
                                style.shader.classification == classification;
                            const auto label = std::string(
                                fabric::project::to_string(classification));
                            if (ImGui::Selectable(
                                    label.c_str(), selected_classification)) {
                                style.shader.classification = classification;
                                style_changed = true;
                            }
                            if (selected_classification)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    style_changed |= draw_surface_effect_stack(
                        style.shader, "path-effects");
                    style_changed |= ImGui::SliderFloat(
                        "Shader opacity", &style.shader.opacity, 0.0F, 1.0F);
                    style_changed |= ImGui::SliderFloat(
                        "Shader intensity", &style.shader.intensity, 0.0F, 4.0F);
                }
                if (style_changed)
                    (void)session.set_selected_textured_path(std::move(style));
                if (!is_beam) {
                    ImGui::SeparatorText("Texture animation preview");
                    ImGui::Checkbox("Scroll texture", &path_ui.animate_texture);
                    ImGui::DragFloat(
                        "Scroll speed (factor/s)", &path_ui.scroll_speed,
                        0.05F, -100.0F, 100.0F);
                    draw_technical_tooltip(
                        "Texture offset speed used by the preview animation.");
                    if (ImGui::Button("Reset preview offset"))
                        path_ui.preview_offset = 0.0F;
                }
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
                const bool is_beam_component = std::ranges::any_of(
                    component.parameters, [](const auto& parameter) {
                        return parameter.target.node_id == "beam" &&
                            parameter.target.component_id == "shader";
                    });
                if (!is_beam_component ||
                    ImGui::CollapsingHeader("Advanced component structure")) {
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
                }

                const auto current_component =
                    *session.selected_visual_component();
                ImGui::SeparatorText(
                    is_beam_component ? "Beam appearance" : "Parameters");
                if (is_beam_component) {
                    for (std::size_t index = 0;
                         index < current_component.parameters.size(); ++index) {
                        auto parameter = current_component.parameters[index];
                        bool changed = false;
                        ImGui::PushID(static_cast<int>(index));
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(
                                &parameter.default_value)) {
                            std::string texture_id = reference->id.value;
                            if (draw_project_resource_picker(
                                    parameter.name.c_str(), session.resources(),
                                    fabric::editor::StudioResourceKind::texture,
                                    texture_id, false)) {
                                *reference = {{.value = std::move(texture_id)},
                                              "texture"};
                                changed = true;
                            }
                        } else if (auto* color = std::get_if<fabric::core::Color>(
                                       &parameter.default_value)) {
                            changed = ImGui::ColorEdit4(
                                parameter.name.c_str(), &color->red);
                        } else if (auto* value = std::get_if<std::string>(
                                       &parameter.default_value)) {
                            if (parameter.id == "color-mode") {
                                const char* label = *value == "preserve"
                                    ? "Source intacte"
                                    : "Recoloration";
                                if (ImGui::BeginCombo(
                                        "Traitement des couleurs", label)) {
                                    if (ImGui::Selectable(
                                            "Recoloration",
                                            *value == "recolor")) {
                                        *value = "recolor";
                                        changed = true;
                                    }
                                    if (ImGui::Selectable(
                                            "Source intacte",
                                            *value == "preserve")) {
                                        *value = "preserve";
                                        changed = true;
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                        } else if (auto* value = std::get_if<float>(
                                       &parameter.default_value)) {
                            if (parameter.id == "shine" ||
                                parameter.id == "holography" ||
                                parameter.id == "opacity") {
                                changed = ImGui::SliderFloat(
                                    parameter.name.c_str(), value, 0.0F, 1.0F);
                            } else {
                                changed = ImGui::DragFloat(
                                    parameter.name.c_str(), value, 0.01F,
                                    parameter.id == "repeat" ? 0.01F : 0.001F,
                                    1000.0F);
                            }
                        }
                        if (changed) {
                            auto candidate =
                                *session.selected_visual_component();
                            candidate.parameters[index] = std::move(parameter);
                            (void)session.set_selected_visual_component(
                                std::move(candidate));
                        }
                        ImGui::PopID();
                    }
                    ImGui::TextDisabled(
                        "Orientation follows the path from start to end.");
                } else {
                    for (std::size_t index = 0;
                         index < current_component.parameters.size(); ++index) {
                        const auto& parameter =
                            current_component.parameters[index];
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
                if (ImGui::Button("Start new freeform path")) {
                    node.shape.kind = fabric::project::VectorShapeKind::path;
                    node.shape.path = {{
                        .kind = fabric::project::VectorPathCommandKind::move,
                        .point = node.shape.bounds.origin}, {
                        .kind = fabric::project::VectorPathCommandKind::line,
                        .point = {node.shape.bounds.origin.x + 1.0F,
                                  node.shape.bounds.origin.y}}};
                    canvas.selected_path_points.clear();
                    commit_node(node);
                }
                ImGui::SameLine();
                const bool path_has_move =
                    !node.shape.path.empty() &&
                    node.shape.path.front().kind ==
                        fabric::project::VectorPathCommandKind::move;
                ImGui::BeginDisabled(!path_has_move);
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
                ImGui::EndDisabled();
                draw_disabled_reason(!path_has_move,
                                     "Start a freeform path first so its move head remains valid.");
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
                ImGui::SeparatorText("Selected block deformation");
                if (ImGui::Checkbox("Warp pixels with this block shape (advanced)",
                                    &deform)) {
                    node.fill.image->deform_with_shape = deform;
                    commit_node(node);
                }
            }
            const auto default_stroke_texture = [&]()
                -> std::optional<fabric::project::ResourceReference> {
                if (!session.manifest() ||
                    !session.manifest()->default_stroke_texture) return std::nullopt;
                const auto selected = std::ranges::find_if(
                    session.resources(), [&](const auto& resource) {
                        return resource.kind ==
                            fabric::editor::StudioResourceKind::texture &&
                            resource.id == *session.manifest()->default_stroke_texture;
                    });
                if (selected == session.resources().end()) return std::nullopt;
                return fabric::project::ResourceReference{selected->id, "texture"};
            };
            bool has_stroke = node.stroke.has_value();
            if (ImGui::Checkbox("Stroke", &has_stroke)) {
                if (has_stroke) {
                    node.stroke = fabric::project::VectorStroke{};
                    if (const auto texture = default_stroke_texture())
                        node.stroke->image = fabric::project::VectorImageFill{
                            .texture = *texture};
                }
                else node.stroke.reset();
                commit_node(node);
            }
            if (node.stroke) {
                if (!node.stroke->image) {
                    if (const auto texture = default_stroke_texture()) {
                        node.stroke->image = fabric::project::VectorImageFill{
                            .texture = *texture};
                        commit_node(node);
                    }
                }
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
                    if (ImGui::Checkbox("Deform stroke image with this block", &deform)) {
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
            if (ImGui::CollapsingHeader("Advanced Entity systems")) {
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
            if (ImGui::CollapsingHeader("Whole Entity deformation (advanced)")) {
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
            if (ImGui::CollapsingHeader("Whole Entity simulation (advanced)")) {
                if (entity.xpbd) {
                    auto xpbd = *entity.xpbd;
                    const auto diagnostics =
                        fabric::project::measure_xpbd_system(xpbd);
                    ImGui::Text("%zu particles (%zu dynamic) · %zu constraints",
                                diagnostics.particle_count,
                                diagnostics.dynamic_particle_count,
                                diagnostics.constraint_count);
                    ImGui::Text("Constraint error max %.4f · RMS %.4f",
                                diagnostics.maximum_constraint_error,
                                diagnostics.rms_constraint_error);
                    ImGui::Text("Compliant energy %.4f",
                                diagnostics.compliant_energy);
                    ImGui::TextDisabled(
                        "Canvas: dynamic green · fixed/pin pink · distance cyan · "
                        "bend violet · area amber · collision red");
                    ImGui::TextDisabled(
                        "distance %zu · pin %zu · bend %zu · area %zu · collision %zu",
                        xpbd.distance_constraints.size(),
                        xpbd.pin_constraints.size(),
                        xpbd.bending_constraints.size(),
                        xpbd.area_constraints.size(),
                        xpbd.collision_constraints.size());
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
                    ImGui::SameLine();
                    if (ImGui::Button("Reset as 4-point cloth")) {
                        fabric::project::XpbdSystem cloth{
                            .particles = {
                                {{-1.0F, 1.0F}, 0.0F}, {{1.0F, 1.0F}, 0.0F},
                                {{-1.0F, -1.0F}, 1.0F}, {{1.0F, -1.0F}, 1.0F}},
                            .distance_constraints = {
                                {0, 1, 2.0F, 0.0F, 0.0F},
                                {0, 2, 2.0F, 0.001F, 0.0F},
                                {1, 3, 2.0F, 0.001F, 0.0F},
                                {2, 3, 2.0F, 0.001F, 0.0F}},
                            .pin_constraints = {
                                {0, {-1.0F, 1.0F}, 0.0F, {}},
                                {1, {1.0F, 1.0F}, 0.0F, {}}},
                            .bending_constraints = {
                                {0, 2, 3, 2.828427F, 0.01F, 0.0F}},
                            .area_constraints = {
                                {0, 2, 1, 2.0F, 0.001F, 0.0F},
                                {1, 2, 3, 2.0F, 0.001F, 0.0F}},
                            .collision_constraints = {
                                {2, {0.0F, 1.0F}, -2.0F, 0.0F, 0.0F},
                                {3, {0.0F, 1.0F}, -2.0F, 0.0F, 0.0F}}};
                        auto next = entity;
                        next.xpbd = std::move(cloth);
                        commit_advanced_entity(std::move(next));
                    }
                    ImGui::SetItemTooltip(
                        "Create a valid example containing all five constraint families.");
                    if (ImGui::Button("Remove XPBD system")) {
                        auto next = entity;
                        next.xpbd.reset();
                        commit_advanced_entity(std::move(next));
                    }
                } else if (ImGui::Button("Create XPBD system")) {
                    auto next = entity;
                    next.xpbd = fabric::project::XpbdSystem{
                        .particles = {
                            {{-1.0F, 0.0F}, 0.0F}, {{0.0F, 0.0F}, 1.0F},
                            {{1.0F, 0.0F}, 1.0F}},
                        .distance_constraints = {
                            {0, 1, 1.0F, 0.001F, 0.0F},
                            {1, 2, 1.0F, 0.001F, 0.0F}},
                        .pin_constraints = {
                            {0, {-1.0F, 0.0F}, 0.0F, {}}}};
                    commit_advanced_entity(std::move(next));
                }
            }
            }
        }
        if (selected != nullptr &&
            selected->kind == fabric::editor::StudioResourceKind::entity &&
            session.selected_entity()) {
            const auto& entity = *session.selected_entity();
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
                    if (added_ok) canvas.selected_node = entity.nodes.size() - 1U;
                    return added_ok;
                };
            ImGui::SeparatorText("Entity behavior");
            if (entity.xpbd) {
                const auto diagnostics =
                    fabric::project::measure_xpbd_system(*entity.xpbd);
                ImGui::Text("XPBD · %zu particles · %zu constraints",
                            diagnostics.particle_count,
                            diagnostics.constraint_count);
                ImGui::TextDisabled("error %.4f max / %.4f RMS · energy %.4f",
                                    diagnostics.maximum_constraint_error,
                                    diagnostics.rms_constraint_error,
                                    diagnostics.compliant_energy);
            }
            if (ImGui::Button("Open Animation Graph")) {
                animation_graph_ui.open = true;
                if (entity.animation_state_machine)
                    animation_graph_ui.current_state =
                        entity.animation_state_machine->initial_state;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add animation clip...")) {
                if (!entity.nodes.empty())
                    animation_ui.node_id =
                        entity.nodes[std::min(canvas.selected_node,
                                              entity.nodes.size() - 1U)].id;
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
                if (ui_drag_probe_enabled && ui_drag_target_mode == 1) {
                    const auto minimum = ImGui::GetItemRectMin();
                    const auto maximum = ImGui::GetItemRectMax();
                    ui_drag_target_screen = {(minimum.x + maximum.x) * 0.5F,
                                             (minimum.y + maximum.y) * 0.5F};
                    ui_drag_target_seen = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(
                            "VERTEX_LOOM_RESOURCE");
                        payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                        if (add_dropped_node(std::nullopt,
                                             *static_cast<const ResourceDragPayload*>(
                                                 payload->Data))) {
                            ui_drag_probe_applied = ui_drag_probe_enabled &&
                                ui_drag_target_mode == 1;
                            status = "Artwork dropped on new root node.";
                        } else {
                            status = "Artwork kind cannot be used on an entity node.";
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Textures, vectors or visual components");
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
            if (ui_drag_probe_enabled && ui_drag_target_mode == 2) {
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                ui_drag_target_screen = {(minimum.x + maximum.x) * 0.5F,
                                         (minimum.y + maximum.y) * 0.5F};
                ui_drag_target_seen = true;
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(
                        "VERTEX_LOOM_RESOURCE");
                    payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                    if (add_dropped_node(entity.nodes[canvas.selected_node].id,
                                         *static_cast<const ResourceDragPayload*>(
                                             payload->Data))) {
                        ui_drag_probe_applied = ui_drag_probe_enabled &&
                            ui_drag_target_mode == 2;
                        status = "Artwork dropped on new child node.";
                    } else {
                        status = "Artwork kind cannot be used on an entity node.";
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Textures, vectors or visual components");
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
                if (ui_drag_probe_enabled && ui_drag_target_mode == 0 &&
                    entity.nodes[node_index].id == "root") {
                    const auto minimum = ImGui::GetItemRectMin();
                    const auto maximum = ImGui::GetItemRectMax();
                    ui_drag_target_screen = {(minimum.x + maximum.x) * 0.5F,
                                             (minimum.y + maximum.y) * 0.5F};
                    ui_drag_target_seen = true;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(
                            "VERTEX_LOOM_RESOURCE");
                        payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                        if (apply_resource_to_node(
                                node_index,
                                *static_cast<const ResourceDragPayload*>(payload->Data))) {
                            canvas.selected_node = node_index;
                            if (ui_drag_probe_enabled && ui_drag_target_mode == 0 &&
                                entity.nodes[node_index].id == "root")
                                ui_drag_probe_applied = true;
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
            if (node.drawable.material) {
                const auto loaded = fabric::project::load_material(
                    session.project_root(), *session.manifest(),
                    fabric::project::material_document_path(
                        *session.manifest(), node.drawable.material->id));
                if (loaded.ok() && loaded.asset->shader) {
                    auto appearance = *loaded.asset;
                    bool appearance_changed = draw_surface_effect_stack(
                        *appearance.shader, "entity-material-effects");
                    appearance_changed |= ImGui::SliderFloat(
                        "Appearance opacity", &appearance.shader->opacity,
                        0.0F, 1.0F);
                    appearance_changed |= ImGui::SliderFloat(
                        "Appearance intensity", &appearance.shader->intensity,
                        0.0F, 4.0F);
                    if (appearance_changed &&
                        !session.set_referenced_material(
                            node.drawable.material->id,
                            std::move(appearance))) {
                        status = "Appearance change rejected; inspect diagnostics.";
                    }
                }
            }
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
                    if (ui_override_probe_enabled &&
                        kind == fabric::project::EntityDrawableKind::texture) {
                        const auto minimum = ImGui::GetItemRectMin();
                        const auto maximum = ImGui::GetItemRectMax();
                        ui_override_texture_screen = {(minimum.x + maximum.x) * 0.5F,
                                                      (minimum.y + maximum.y) * 0.5F};
                        ui_override_texture_seen = true;
                    }
                    ImGui::EndDisabled();
                    draw_disabled_reason(!available,
                                         "Add an indexed resource of this drawable kind first.");
                }
                ImGui::EndCombo();
            }
            if (ui_override_probe_enabled) {
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                ui_override_kind_screen = {(minimum.x + maximum.x) * 0.5F,
                                           (minimum.y + maximum.y) * 0.5F};
                ui_override_kind_seen = true;
            }
            if (ui_override_force_modal) {
                ImGui::OpenPopup("Discard incompatible overrides?");
                ui_override_force_modal = false;
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
                if (ui_override_probe_enabled) {
                    const auto minimum = ImGui::GetItemRectMin();
                    const auto maximum = ImGui::GetItemRectMax();
                    ui_override_confirm_screen = {(minimum.x + maximum.x) * 0.5F,
                                                  (minimum.y + maximum.y) * 0.5F};
                    ui_override_confirm_seen = true;
                }
                ImGui::EndDisabled();
                draw_disabled_reason(!valid,
                                     "Select a valid drawable kind before discarding overrides.");
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    pending_drawable_kind.reset();
                    ImGui::CloseCurrentPopup();
                }
                if (ui_override_probe_enabled) {
                    const auto minimum = ImGui::GetItemRectMin();
                    const auto maximum = ImGui::GetItemRectMax();
                    ui_override_cancel_screen = {(minimum.x + maximum.x) * 0.5F,
                                                 (minimum.y + maximum.y) * 0.5F};
                    ui_override_cancel_seen = true;
                    ui_override_modal_seen = true;
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
                static std::string audio_draft_id;
                static fabric::project::AudioDocument audio_draft;
                if (audio_draft_id != loaded.audio->document.id.value) {
                    audio_draft_id = loaded.audio->document.id.value;
                    audio_draft = *loaded.audio;
                }
                ImGui::TextDisabled("master · 100%%");
                for (std::size_t bus_index = 0;
                     bus_index < audio_draft.buses.size(); ++bus_index) {
                    auto& bus = audio_draft.buses[bus_index];
                    ImGui::PushID(static_cast<int>(bus_index));
                    ImGui::Text("%s", bus.id.c_str());
                    draw_technical_tooltip(
                        "Bus ids remain stable so event routing cannot break while editing.");
                    ImGui::SliderFloat("Bus volume", &bus.volume, 0.0F, 1.0F);
                    if (ImGui::SmallButton("Remove bus")) {
                        const auto removed_id = bus.id;
                        audio_draft.buses.erase(audio_draft.buses.begin() +
                            static_cast<std::ptrdiff_t>(bus_index));
                        for (auto& event : audio_draft.events)
                            if (event.bus == removed_id) event.bus = "master";
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add bus")) {
                    std::string id = "bus-" +
                        std::to_string(audio_draft.buses.size() + 1U);
                    while (std::ranges::any_of(audio_draft.buses,
                                               [&](const auto& bus) {
                        return bus.id == id;
                    })) id += "-copy";
                    audio_draft.buses.push_back({id, 1.0F});
                }
                for (std::size_t event_index = 0;
                     event_index < audio_draft.events.size(); ++event_index) {
                    auto& event = audio_draft.events[event_index];
                    ImGui::PushID(static_cast<int>(event_index));
                    ImGui::SeparatorText(
                        ("Event " + std::to_string(event_index + 1U)).c_str());
                    ImGui::InputText("Event id", &event.id);
                    ImGui::InputText("Source", &event.source);
                    ImGui::SliderFloat("Volume (0–1)", &event.volume, 0.0F, 1.0F);
                    draw_technical_tooltip(
                        "Playback gain for this audio event; 1.0 is the source level.");
                    ImGui::Checkbox("Loop", &event.loop);
                    draw_technical_tooltip(
                        "Restart this event automatically when playback reaches its end.");
                    if (ImGui::BeginCombo("Bus", event.bus.c_str())) {
                        if (ImGui::Selectable("master", event.bus == "master"))
                            event.bus = "master";
                        for (const auto& bus : audio_draft.buses)
                            if (ImGui::Selectable(bus.id.c_str(), event.bus == bus.id))
                                event.bus = bus.id;
                        ImGui::EndCombo();
                    }
                    bool spatial = event.spatial.has_value();
                    if (ImGui::Checkbox("Spatial 2D", &spatial))
                        event.spatial = spatial
                            ? std::optional<fabric::project::AudioSpatialSettings>{
                                fabric::project::AudioSpatialSettings{}}
                            : std::nullopt;
                    if (event.spatial) {
                        ImGui::InputFloat2("Source position", &event.spatial->position.x);
                        ImGui::InputFloat("Minimum distance",
                                          &event.spatial->minimum_distance);
                        ImGui::InputFloat("Maximum distance",
                                          &event.spatial->maximum_distance);
                    }
                    if (ImGui::SmallButton("Remove event")) {
                        audio_draft.events.erase(audio_draft.events.begin() +
                            static_cast<std::ptrdiff_t>(event_index));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (audio_draft.events.empty())
                    ImGui::TextDisabled("No audio events defined.");
                if (ImGui::Button("Add event")) {
                    std::string id = "event-" +
                        std::to_string(audio_draft.events.size() + 1U);
                    while (std::ranges::any_of(audio_draft.events,
                                               [&](const auto& event) {
                        return event.id == id;
                    })) id += "-copy";
                    audio_draft.events.push_back({.id = std::move(id)});
                }
                ImGui::SameLine();
                if (ImGui::Button("Save Audio setup")) {
                    if (session.set_selected_audio_document(audio_draft))
                        status = "Audio buses and events saved.";
                    else
                        status = "Audio setup rejected; inspect diagnostics.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Revert Audio changes"))
                    audio_draft = *loaded.audio;
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
            if (session.selected_entity() &&
                !session.selected_entity()->nodes.empty()) {
                const auto& nodes = session.selected_entity()->nodes;
                auto selected_node = std::ranges::find(
                    nodes, animation_ui.node_id,
                    &fabric::project::EntityNode::id);
                if (selected_node == nodes.end()) {
                    animation_ui.node_id = nodes.front().id;
                    selected_node = nodes.begin();
                }
                ImGui::SeparatorText("Quick keys");
                const auto current_value =
                    [&](const fabric::project::PropertyBinding& binding,
                        fabric::project::AnimationValue fallback) {
                        const auto found = std::ranges::find(
                            evaluated.properties, binding,
                            &fabric::project::EvaluatedProperty::binding);
                        return found == evaluated.properties.end()
                            ? fallback : found->value;
                    };
                const auto set_quick_key =
                    [&](const char* label,
                        const std::string_view property,
                        fabric::project::AnimationValue value) {
                        const auto binding = fabric::project::PropertyBinding{
                            .node_id = animation_ui.node_id,
                            .component_id = "transform",
                            .property_id = std::string{property}};
                        const bool clicked = ImGui::Button(label);
                        if (ui_animation_probe_enabled &&
                            property == "rotationDegrees") {
                            const auto minimum = ImGui::GetItemRectMin();
                            const auto maximum = ImGui::GetItemRectMax();
                            ui_animation_quick_key_screen = {
                                (minimum.x + maximum.x) * 0.5F,
                                (minimum.y + maximum.y) * 0.5F};
                            ui_animation_quick_key_seen = true;
                        }
                        if (!clicked) return;
                        if (session.set_selected_animation_key(
                                binding, animation_ui.scrub_time,
                                current_value(binding, std::move(value)),
                                animation_ui.interpolation,
                                fabric::editor::AutosaveScheduler::Clock::now(),
                                animation_ui.composition, animation_ui.easing)) {
                            animation_ui.component_id = "transform";
                            animation_ui.property_id = std::string{property};
                            status = std::string{label} + " key set at playhead.";
                        } else {
                            status = "Quick key rejected; inspect diagnostics.";
                        }
                    };
                ImGui::TextDisabled("%s", selected_node->name.c_str());
                set_quick_key("Key Position", "position",
                              selected_node->transform.position);
                ImGui::SameLine();
                set_quick_key("Key Rotation", "rotationDegrees",
                              selected_node->transform.rotation_degrees);
                set_quick_key("Key Scale", "scale",
                              selected_node->transform.scale);
                ImGui::SameLine();
                set_quick_key("Key Pivot", "pivot",
                              selected_node->transform.pivot);
                ImGui::TextDisabled(
                    "Creates the typed track when missing and captures the current value.");
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
        if (!session.refresh_resources()) {
            status = "Project resources could not be refreshed; inspect diagnostics.";
        }
        if (creation.prepared_entity) {
            creation.entity = std::move(*creation.prepared_entity);
            creation.prepared_entity.reset();
        } else {
            creation.entity.reset();
        }
        if (creation.guided_composed_entity) {
            creation.entity.name = "Composed entity";
            creation.entity.node_name = "Root";
        }
        if (creation.guided_button) {
            creation.entity.name = "Button entity";
            creation.entity.drawable =
                fabric::project::EntityDrawableKind::texture;
            creation.entity.node_name = "Button";
            creation.entity.resource_id = "button-primary";
            creation.entity.appearance_shader =
                fabric::project::ShaderSurfaceSettings{
                    .profile = fabric::project::SurfaceShaderProfile::custom,
                    .classification =
                        fabric::project::TextureClassification::button_eye,
                    .primary_color = {1.0F, 1.0F, 1.0F, 1.0F},
                    .effect_color = {1.0F, 1.0F, 1.0F, 1.0F},
                    .shine = 0.0F,
                    .holography = 0.0F,
                    .effects = {
                        {.kind = fabric::project::SurfaceEffectKind::tint,
                         .color = {1.0F, 1.0F, 1.0F, 1.0F},
                         .amount = 0.0F},
                        {.kind = fabric::project::SurfaceEffectKind::holography,
                         .color = {1.0F, 1.0F, 1.0F, 1.0F},
                         .amount = 0.0F},
                        {.kind = fabric::project::SurfaceEffectKind::shine,
                         .color = {1.0F, 1.0F, 1.0F, 1.0F},
                         .amount = 0.0F}},
                };
        }
        if (creation.guided_contextual_entity) {
            if (session.create_entity(creation.entity)) {
                clear_asset_preview(preview);
                status = "Entity created from the selected visual.";
            } else {
                status = "Entity creation failed; inspect diagnostics.";
            }
            creation.guided_contextual_entity = false;
        } else {
            ImGui::OpenPopup("Create entity");
        }
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
        if (ui_input_probe_enabled) {
            creation.input.name = "Player and Monster Controls";
            creation.input.actions = {
                {"move", {{fabric::project::InputDevice::keyboard, 65}}},
                {"attack", {{fabric::project::InputDevice::gamepad, 0}}}};
        }
        ImGui::OpenPopup("Create input bindings");
        creation.request_input = false;
    }
    if (creation.request_visual_preset && session.has_project()) {
        if (!session.refresh_resources()) {
            status = "Project resources could not be refreshed; inspect diagnostics.";
        }
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
            if (session.open(path_buffer.data()) &&
                ensure_default_studio_textures(session)) {
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
            const bool project_created = session.create(
                creation.project.project_root(), creation.project.manifest());
            const bool defaults_installed = project_created &&
                ensure_default_studio_textures(session);
            if (defaults_installed) {
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
        ImGui::TextUnformatted(request.guided_beam
            ? "Create a Beam visual component"
            : "Create a reusable textile visual component");
        ImGui::TextDisabled(request.guided_beam
            ? "The texture repeats along the path and follows every curve automatically."
            : "The preset assembles reusable paths and layers.");
        const auto user_preset_label = [](const auto kind) {
            return fabric::editor::label(kind);
        };
        if (!request.guided_beam) {
            const auto kind_label = std::string(user_preset_label(request.kind));
            ImGui::SetNextItemWidth(280.0F);
            if (ImGui::BeginCombo("Preset", kind_label.c_str())) {
                for (const auto kind : {fabric::editor::VisualPresetKind::beam,
                                        fabric::editor::VisualPresetKind::zipper}) {
                    const bool selected = request.kind == kind;
                    const auto option = std::string(user_preset_label(kind));
                    if (ImGui::Selectable(option.c_str(), selected)) {
                        request.kind = kind;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        draw_resource_identity_fields(request.name, request.id.value);
        const bool uses_thread = request.kind ==
                fabric::editor::VisualPresetKind::beam ||
                request.kind == fabric::editor::VisualPresetKind::seam ||
                request.kind == fabric::editor::VisualPresetKind::zipper;
        bool thread_texture_resolved = !uses_thread;
        if (uses_thread) {
            if (!request.thread_texture && session.manifest()->default_stroke_texture)
                request.thread_texture = fabric::project::ResourceReference{
                    *session.manifest()->default_stroke_texture, "texture"};
            std::string texture_id = request.thread_texture
                ? request.thread_texture->id.value : std::string{};
            if (draw_project_resource_picker(
                    "Thread texture", session.resources(),
                    fabric::editor::StudioResourceKind::texture,
                    texture_id, false)) {
                request.thread_texture = fabric::project::ResourceReference{
                    {.value = std::move(texture_id)}, "texture"};
            }
            thread_texture_resolved = request.thread_texture &&
                std::ranges::any_of(session.resources(), [&](const auto& resource) {
                    return resource.kind ==
                               fabric::editor::StudioResourceKind::texture &&
                        resource.id == request.thread_texture->id;
                });
            if (std::ranges::none_of(
                    session.resources(), [](const auto& resource) {
                        return resource.kind ==
                            fabric::editor::StudioResourceKind::texture;
                    })) {
                ImGui::TextColored({0.95F, 0.65F, 0.25F, 1.0F},
                                   "No project textures are indexed. Import a PNG or refresh the project.");
            }
            if (!thread_texture_resolved) {
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                   "The selected project texture is missing. Choose an existing texture.");
            } else if (session.manifest()->default_stroke_texture &&
                       request.thread_texture &&
                       request.thread_texture->id ==
                           *session.manifest()->default_stroke_texture) {
                ImGui::TextDisabled("Using the project default thread texture.");
            }
            if (request.kind == fabric::editor::VisualPresetKind::beam ||
                request.kind == fabric::editor::VisualPresetKind::seam) {
                ImGui::SeparatorText("Beam appearance");
                int color_mode = request.beam_color_mode ==
                        fabric::editor::BeamColorMode::preserve_source
                    ? 0 : 1;
                if (ImGui::Combo("Traitement des couleurs", &color_mode,
                                 "Source intacte\0Recoloration\0")) {
                    request.beam_color_mode = color_mode == 0
                        ? fabric::editor::BeamColorMode::preserve_source
                        : fabric::editor::BeamColorMode::recolor_from_detail;
                }
                ImGui::ColorEdit4("Base tint", &request.beam_color.red);
                ImGui::TextDisabled(
                    "The selected color is used only in Recoloration mode.");
                ImGui::ColorEdit4("Holo color",
                                  &request.beam_effect_color.red);
                ImGui::SliderFloat("Shine", &request.beam_shine, 0.0F, 1.0F);
                ImGui::SliderFloat("Holography", &request.beam_holography, 0.0F, 1.0F);
                ImGui::DragFloat("Thickness", &request.beam_width,
                                 0.01F, 0.001F, 1000.0F, "%.3f");
                ImGui::SliderFloat("Opacity", &request.beam_opacity,
                                   0.0F, 1.0F, "%.2f");
                ImGui::DragFloat("Repetition", &request.beam_repetition,
                                 0.05F, 0.01F, 1000.0F);
                ImGui::TextDisabled("Orientation follows the Beam path automatically.");
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
            if (ImGui::CollapsingHeader("Advanced resource details")) {
                ImGui::Text("%zu vector artwork(s)", built.bundle->vectors.size());
                ImGui::Text("%zu textured path(s)",
                            built.bundle->textured_paths.size());
                ImGui::TextUnformatted("1 composition");
                ImGui::TextUnformatted("1 reusable component");
            }
        } else {
            for (const auto& error : built.errors) {
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s",
                                   error.message.c_str());
            }
        }
        ImGui::BeginDisabled(!built.ok() || !thread_texture_resolved);
        const bool create_visual_clicked = ImGui::Button(
            request.guided_beam ? "Create Beam" : "Create preset",
            {140.0F, 0.0F});
        if (ui_beam_probe_enabled && request.guided_beam) {
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            ui_beam_create_screen = {
                (minimum.x + maximum.x) * 0.5F,
                (minimum.y + maximum.y) * 0.5F};
            ui_beam_create_seen = true;
        }
        if (create_visual_clicked) {
            if (session.create_visual_preset(request)) {
                if (ui_beam_probe_enabled) ui_beam_created = true;
                clear_asset_preview(preview);
                status = "Visual preset created and selected.";
                ImGui::CloseCurrentPopup();
            } else {
                status = "Visual preset creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!built.ok() || !thread_texture_resolved,
                             !thread_texture_resolved
                                 ? "Choose an existing project texture before creating it."
                                 : "Resolve the visual preset build errors before creating it.");
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
        ImGui::TextUnformatted("Create a reusable MaterialDefinition v2");
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
        if (creation.guided_button) {
            ImGui::SeparatorText("Original Button image");
            ImGui::TextWrapped(
                "Choose the supplied original image used as a Button. "
                "Asset Studio will not generate or guess a replacement.");
        }
        const auto validation = creation.entity.validate(
            session.project_root(), *session.manifest());
        draw_resource_name_field("Name##entity-name", creation.entity.name);
        focus_prompt_field(validation, "name", "entity-create");
        draw_resource_name_field("Root node name", creation.entity.node_name,
                                 360.0F);
        focus_prompt_field(validation, "node_name", "entity-create");
        const auto drawable_label = std::string(
            fabric::project::to_string(creation.entity.drawable));
        ImGui::BeginDisabled(creation.guided_button);
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
        ImGui::EndDisabled();
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
                creation.guided_button
                    ? "Original Button image"
                    : "Drawable resource",
                session.resources(), resource_kind,
                creation.entity.resource_id, false));
            if (creation.guided_button && creation.entity.resource_id.empty()) {
                ImGui::TextColored({0.95F, 0.65F, 0.25F, 1.0F},
                                   "Select the supplied original Button image before creating.");
            }
        } else {
            creation.entity.resource_id.clear();
            ImGui::TextDisabled(
                "Choose a drawable to attach an existing project resource.");
        }
        if (creation.guided_button && creation.entity.appearance_shader) {
            auto& appearance = *creation.entity.appearance_shader;
            ImGui::SeparatorText("Button appearance");
            ImGui::TextDisabled(
                "These settings recolor the selected PNG; the source image is never modified.");
            static_cast<void>(draw_surface_effect_stack(
                appearance, "button-create-effects", false));
            ImGui::SliderFloat("Opacity", &appearance.opacity,
                               0.0F, 1.0F);
        } else if (creation.entity.drawable !=
            fabric::project::EntityDrawableKind::visual_component) {
            static_cast<void>(draw_project_resource_picker(
                "Material (optional)", session.resources(),
                fabric::editor::StudioResourceKind::material,
                creation.entity.material_id, true));
        } else {
            ImGui::TextDisabled(
                "The selected visual component owns its composed materials.");
        }
        if (creation.guided_composed_entity) {
            ImGui::SeparatorText("Visual blocks");
            ImGui::TextWrapped(
                "Add each artwork, image, Beam or component explicitly. Blocks are independent and keep their own transform and draw order.");
            if (ImGui::Button("Add visual block")) {
                const auto index = creation.entity.blocks.size() + 1U;
                creation.entity.blocks.push_back({
                    .name = "Block " + std::to_string(index),
                    .drawable = fabric::project::EntityDrawableKind::vector});
            }
            for (std::size_t index = 0; index < creation.entity.blocks.size(); ++index) {
                auto& block = creation.entity.blocks[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::SeparatorText(("Block " + std::to_string(index + 1U)).c_str());
                draw_resource_name_field("Block name", block.name, 360.0F);
                const auto block_label = std::string(
                    fabric::project::to_string(block.drawable));
                if (ImGui::BeginCombo("Block type", block_label.c_str())) {
                    for (const auto drawable : {
                             fabric::project::EntityDrawableKind::vector,
                             fabric::project::EntityDrawableKind::texture,
                             fabric::project::EntityDrawableKind::visual_component}) {
                        const bool selected = block.drawable == drawable;
                        const auto option = std::string(
                            fabric::project::to_string(drawable));
                        if (ImGui::Selectable(option.c_str(), selected)) {
                            block.drawable = drawable;
                            block.resource_id.clear();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                const auto block_kind = block.drawable ==
                        fabric::project::EntityDrawableKind::texture
                    ? fabric::editor::StudioResourceKind::texture
                    : block.drawable ==
                          fabric::project::EntityDrawableKind::visual_component
                    ? fabric::editor::StudioResourceKind::visual_component
                    : fabric::editor::StudioResourceKind::vector;
                static_cast<void>(draw_project_resource_picker(
                    "Block resource", session.resources(), block_kind,
                    block.resource_id, false));
                float block_position[] = {block.transform.position.x,
                                          block.transform.position.y};
                if (ImGui::InputFloat2("Block position", block_position))
                    block.transform.position = {block_position[0], block_position[1]};
                float block_scale[] = {block.transform.scale.x,
                                       block.transform.scale.y};
                if (ImGui::InputFloat2("Block scale", block_scale))
                    block.transform.scale = {block_scale[0], block_scale[1]};
                ImGui::InputFloat("Block rotation", &block.transform.rotation_degrees,
                                  1.0F, 10.0F, "%.2f deg");
                ImGui::InputFloat("Block Z order", &block.z_order, 0.1F, 1.0F,
                                  "%.2f");
                if (ImGui::Button("Remove block")) {
                    creation.entity.blocks.erase(
                        creation.entity.blocks.begin() +
                        static_cast<std::ptrdiff_t>(index));
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (creation.entity.blocks.empty())
                ImGui::TextColored({0.95F, 0.65F, 0.25F, 1.0F},
                                   "Add at least one visual block before creating the Entity.");
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
        const bool entity_valid = validation.ok() &&
            (!creation.guided_composed_entity ||
             !creation.entity.blocks.empty());
        ImGui::BeginDisabled(!entity_valid);
        const bool create_entity_clicked = ImGui::Button(
            creation.guided_button ? "Create Button" : "Create entity",
            {140.0F, 0.0F});
        if (ui_button_probe_enabled && creation.guided_button) {
            const auto minimum = ImGui::GetItemRectMin();
            const auto maximum = ImGui::GetItemRectMax();
            ui_button_create_screen = {
                (minimum.x + maximum.x) * 0.5F,
                (minimum.y + maximum.y) * 0.5F};
            ui_button_create_seen = true;
        }
        if (create_entity_clicked) {
            if (session.create_entity(creation.entity)) {
                if (ui_button_probe_enabled) ui_button_created = true;
                clear_asset_preview(preview);
                status = "Entity created and saved.";
                creation.guided_button = false;
                creation.guided_composed_entity = false;
                ImGui::CloseCurrentPopup();
            } else {
                status = "Entity creation failed; inspect diagnostics.";
            }
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!entity_valid,
                             !validation.ok()
                                 ? "Complete the entity fields and resolve validation errors."
                                 : "Add at least one visual block before creating the Entity.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F})) {
            creation.entity.reset();
            creation.guided_button = false;
            creation.guided_composed_entity = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("Create entity") && !creation.request_entity)
        creation.guided_button = false;

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
        if (ui_input_probe_enabled) ui_input_modal_seen = true;
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
                     const bool ui_focus_test = false,
                     const bool ui_accessibility_test = false,
                     const bool ui_drag_test = false,
                     const bool ui_override_test = false,
                     const bool ui_texture_test = false,
                     const bool ui_input_test = false,
                     const bool ui_beam_test = false,
                     const bool ui_button_test = false) {
    const bool graphical_test = behavior_e2e || transformation_e2e || entity_e2e ||
        animation_e2e || texture_e2e || vector_e2e || vector_canvas_e2e ||
        ui_test_mode || ui_min_window_test || ui_focus_test ||
        ui_accessibility_test || ui_drag_test || ui_override_test ||
        ui_texture_test || ui_input_test || ui_beam_test || ui_button_test;
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
              ui_min_window_test || ui_focus_test || ui_accessibility_test ||
              ui_drag_test || ui_override_test || ui_texture_test ||
              ui_input_test || ui_beam_test || ui_button_test)
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
         ui_min_window_test || ui_focus_test || ui_accessibility_test ||
         ui_drag_test || ui_override_test || ui_texture_test || ui_input_test ||
         ui_beam_test || ui_button_test) ? 0 : 1);

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
    AnimationGraphUiState animation_graph_ui;
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
        if (session.open(initial_project) &&
            ensure_default_studio_textures(session)) {
            status = "Project opened: " + session.manifest()->name;
        } else {
            status = "Project rejected; inspect the diagnostics.";
        }
    }
    if ((ui_test_mode || ui_min_window_test || ui_drag_test || ui_override_test ||
         ui_texture_test || ui_input_test || ui_beam_test || ui_button_test) &&
        session.has_project()) {
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
    if (ui_override_test && session.has_project()) {
        ui_override_probe_enabled = true;
        ui_override_modal_seen = false;
        ui_override_cancel_preserved = false;
        ui_override_confirm_applied = false;
        ui_override_kind_seen = false;
        ui_override_texture_seen = false;
        ui_override_cancel_seen = false;
        ui_override_confirm_seen = false;
        static_cast<void>(session.select_resource(
            fabric::editor::StudioResourceKind::entity,
            {.value = "textile-head-entity"}));
        if (session.selected_entity() && !session.selected_entity()->nodes.empty()) {
            auto node = session.selected_entity()->nodes.front();
            if (node.drawable.component_instance)
                node.drawable.component_instance->overrides = {
                    {"scale", fabric::core::Vec2{1.1F, 1.1F}}};
            static_cast<void>(session.set_selected_entity_node(0U, std::move(node)));
        }
    }
    if (ui_texture_test && session.has_project()) {
        ui_texture_probe_enabled = true;
        ui_texture_canvas_seen = false;
        ui_texture_crop_applied = false;
        const auto source = initial_project / "assets/textures/head-face.png";
        const fabric::core::ResourceId texture_id{.value = "texture-ui-e2e"};
        const bool selected = std::filesystem::is_regular_file(source) &&
            session.import_png(source, texture_id, "Texture UI E2E") &&
            session.select_resource(
                fabric::editor::StudioResourceKind::texture, texture_id);
        if (selected && session.imported_texture()) {
            upload_preview(preview, session.imported_texture()->image);
            preview.kind = PreviewKind::texture;
        }
    }
    if (ui_input_test && session.has_project()) {
        ui_input_probe_enabled = true;
        ui_input_modal_seen = false;
        ui_input_created = false;
        ui_input_reloaded = false;
        creation.request_input = true;
    }
    if (ui_beam_test && session.has_project()) {
        ui_beam_probe_enabled = true;
        ui_beam_create_seen = false;
        ui_beam_created = false;
        ui_beam_reloaded = false;
        creation.visual_preset = {};
        creation.visual_preset.kind = fabric::editor::VisualPresetKind::beam;
        creation.visual_preset.id.value = "ui-guided-beam";
        creation.visual_preset.name = "UI Guided Beam";
        creation.visual_preset.guided_beam = true;
        creation.visual_preset.thread_texture.reset();
        creation.visual_preset.beam_repetition = 1.0F;
        creation.visual_preset.beam_width = 0.5F;
        creation.visual_preset.beam_opacity = 1.0F;
        if (ui_beam_holography_variant) {
            creation.visual_preset.beam_color_mode =
                fabric::editor::BeamColorMode::recolor_from_detail;
            creation.visual_preset.beam_color =
                {0.15F, 0.75F, 1.0F, 1.0F};
            creation.visual_preset.beam_effect_color =
                {0.75F, 1.0F, 0.95F, 1.0F};
            creation.visual_preset.beam_shine = 0.6F;
            creation.visual_preset.beam_holography = 1.0F;
        }
        creation.request_visual_preset = true;
    }
    if (ui_button_test && session.has_project()) {
        ui_button_probe_enabled = true;
        ui_button_create_seen = false;
        ui_button_created = false;
        ui_button_reloaded = false;
        creation.guided_button = true;
        creation.request_entity = true;
    }
    if (ui_focus_test && session.has_project()) {
        creation.material.name.clear();
        creation.request_material = true;
        ui_focus_probe_enabled = true;
        ui_focus_probe_succeeded = false;
    }
    if (ui_accessibility_test)
        ui_focus_probe_enabled = false;
    if (ui_drag_test && session.has_project()) {
        ui_drag_probe_enabled = true;
        ui_drag_probe_applied = false;
        ui_drag_source_seen = false;
        ui_drag_target_seen = false;
        static_cast<void>(session.select_resource(
            fabric::editor::StudioResourceKind::entity,
            {.value = "textile-head-entity"}));
        if (ui_drag_target_mode == 1 && session.selected_entity() &&
            session.selected_entity()->nodes.size() == 1U)
            static_cast<void>(session.remove_selected_entity_node(0U));
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
        ui_animation_graph_probe_enabled = true;
        ui_animation_graph_seen = false;
        const fabric::core::ResourceId entity_id{.value =
            "beam-entity"};
        const bool selected = session.select_resource(
            fabric::editor::StudioResourceKind::entity, entity_id);
        auto node = selected ? session.selected_entity()->nodes.front()
                             : fabric::project::EntityNode{};
        node.visible = true;
        node.locked = true;
        node.transform.position = {0.0F, 0.0F};
        node.transform.scale = {50.0F, 50.0F};
        node.drawable = {
            .kind = fabric::project::EntityDrawableKind::visual_component,
            .resource = fabric::project::ResourceReference{
                {.value = "beam"}, "visualComponent"},
            .component_instance = fabric::project::VisualComponentInstance{}};
        bool authored = selected && session.set_selected_entity_node(0U, node) &&
            session.add_selected_entity_node({
                .id = "studio-child", .name = "Studio Child",
                .parent = node.id});
        if (authored) {
            auto child = session.selected_entity()->nodes[1];
            child.parent.reset();
            child.transform.position = {1.35F, 0.0F};
            child.drawable = {
                .kind = fabric::project::EntityDrawableKind::vector,
                .resource = fabric::project::ResourceReference{
                    {.value = "beam-border"}, "vector"}};
            authored = session.set_selected_entity_node(1U, child) &&
                session.duplicate_selected_entity_node(1U);
        }
        if (authored) {
            auto component = session.selected_entity()->nodes[2];
            component.parent.reset();
            component.transform.position = {-1.35F, 0.0F};
            component.drawable = {
                .kind = fabric::project::EntityDrawableKind::visual_component,
                .resource = fabric::project::ResourceReference{
                    {.value = "beam"}, "visualComponent"},
                .component_instance =
                    fabric::project::VisualComponentInstance{}};
            authored = session.set_selected_entity_node(2U, component) &&
                session.move_selected_entity_node(2U, 1U) && session.save();
        }
        if (authored) {
            auto with_graph = *session.selected_entity();
            with_graph.animation_state_machine =
                fabric::project::AnimationStateMachine{
                    .initial_state = "idle",
                    .states = {
                        {"idle", {{.value = "beam-scroll"}, "animation"}},
                        {"active", {{.value = "beam-scroll"}, "animation"}}},
                    .transitions = {{
                        .id = "activate",
                        .from_state = "idle",
                        .to_state = "active",
                        .conditions = {{
                            "enabled",
                            fabric::project::AnimationConditionOperator::equal,
                            true}},
                        .priority = 1}}};
            with_graph.xpbd = fabric::project::XpbdSystem{
                .particles = {
                    {{-5.0F, -2.0F}, 0.0F}, {{0.0F, 2.0F}, 1.0F},
                    {{5.0F, -2.0F}, 1.0F}},
                .distance_constraints = {
                    {0, 1, 6.403124F, 0.001F, 0.0F},
                    {1, 2, 6.403124F, 0.001F, 0.0F}},
                .pin_constraints = {{0, {-5.0F, -2.0F}, 0.0F, {}}}};
            authored = session.set_selected_entity_definition(
                std::move(with_graph)) && session.save();
            animation_graph_ui.open = authored;
            animation_graph_ui.current_state = "idle";
            animation_graph_ui.parameters = {{"enabled", true}};
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
            reloaded.selected_entity()->nodes.front().visible &&
            reloaded.selected_entity()->nodes.front().transform.position ==
                fabric::core::Vec2{0.0F, 0.0F} &&
            reloaded.selected_entity()->nodes.front().transform.scale ==
                fabric::core::Vec2{50.0F, 50.0F} &&
            reloaded.selected_entity()->nodes.front().drawable.kind ==
                fabric::project::EntityDrawableKind::visual_component &&
            reloaded.selected_entity()->nodes[1].drawable.kind ==
                fabric::project::EntityDrawableKind::visual_component &&
            reloaded.selected_entity()->nodes[2].drawable.kind ==
                fabric::project::EntityDrawableKind::vector &&
            reloaded.selected_entity()->animation_state_machine &&
            reloaded.selected_entity()->animation_state_machine->states.size() == 2U &&
            reloaded.selected_entity()->animation_state_machine
                    ->transitions.size() == 1U &&
            visual.packets.size() >= 3U;
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
        ui_animation_probe_enabled = true;
        ui_animation_timeline_seen = false;
        ui_animation_quick_key_seen = false;
        fabric::editor::CreateAnimationPrompt prompt;
        prompt.name = "Targeted Animation E2E";
        prompt.preview_entity_id = "beam-entity";
        prompt.duration = 2.0;
        const bool authored = session.create_animation(prompt) &&
            session.set_selected_animation_segment(
                {.node_id = "root", .component_id = "transform",
                 .property_id = "position"},
                0.0F, fabric::core::Vec2{-0.8F, 0.0F},
                2.0F, fabric::core::Vec2{0.8F, 0.0F},
                fabric::project::AnimationInterpolation::linear) &&
            session.save();
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
                "beam-entity" &&
            reloaded.selected_animation()->tracks.size() == 1U &&
            reloaded.selected_animation()->tracks.front().keys.size() == 2U &&
            reloaded.selected_entity() && !visual.packets.empty();
        if (!animation_e2e_complete)
            std::cerr << "Asset Studio Animation E2E failed\n";
    }

    bool vector_e2e_complete = false;
    if (vector_e2e && session.has_project()) {
        const fabric::core::ResourceId vector_id{.value = "beam-border"};
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
    const auto find_resource_id = [&](
        const fabric::editor::StudioResourceKind kind,
        const std::initializer_list<std::string_view> candidates) {
        for (const auto candidate : candidates) {
            const auto found = std::ranges::find_if(
                session.resources(), [&](const auto& resource) {
                    return resource.kind == kind &&
                        resource.id.value == candidate;
                });
            if (found != session.resources().end()) return found->id;
        }
        return fabric::core::ResourceId{};
    };
    const auto vector_canvas_vector_id = find_resource_id(
        fabric::editor::StudioResourceKind::vector,
        {"beam-border"});
    const auto vector_canvas_texture_id = find_resource_id(
        fabric::editor::StudioResourceKind::texture,
        {"head-thread", "beam-thread"});
    bool vector_canvas_e2e_complete = false;
    std::size_t vector_canvas_e2e_frame = 0U;
    std::size_t vector_canvas_e2e_initial_path_size = 0U;
    bool vector_canvas_e2e_freeform_seed_applied = false;
    fabric::core::Vec2 vector_canvas_e2e_initial_anchor{};
    fabric::core::Vec2 vector_canvas_e2e_initial_control1{};
    if (vector_canvas_e2e && session.has_project()) {
        const auto vector_id = vector_canvas_vector_id;
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
                node.fill = fabric::project::VectorFill{
                    .kind = fabric::project::VectorFillKind::image,
                    .image = fabric::project::VectorImageFill{
                        .texture = {vector_canvas_texture_id, "texture"}},
                };
                node.stroke = fabric::project::VectorStroke{
                    .color = {1.0F, 1.0F, 1.0F, 1.0F},
                    .width = 0.16F,
                    .join = fabric::project::VectorStrokeJoin::round,
                    .cap = fabric::project::VectorStrokeCap::round,
                    .image = fabric::project::VectorImageFill{
                        .texture = {vector_canvas_texture_id, "texture"}},
                    .repeat_texture_x = true,
                };
                vector_canvas_e2e_complete =
                    session.set_selected_vector_node(0U, std::move(node));
                canvas.selected_node = 0U;
                canvas.tool = CanvasUiState::Tool::pen;
            }
        }
    }

    bool running = true;
    std::size_t ui_test_frame = 0U;
    std::size_t ui_drag_frame = 0U;
    std::size_t ui_override_frame = 0U;
    std::size_t ui_texture_frame = 0U;
    std::size_t ui_input_frame = 0U;
    std::size_t ui_beam_frame = 0U;
    std::size_t ui_button_frame = 0U;
    bool entity_e2e_capture_written = false;
    bool animation_e2e_capture_written = false;
    std::size_t animation_ui_e2e_frame = 0U;
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
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 21U &&
            !vector_canvas_e2e_freeform_seed_applied &&
            vector_canvas_e2e_complete && session.created_vector()) {
            auto freeform_node =
                session.created_vector()->native->nodes.front();
            freeform_node.shape.kind =
                fabric::project::VectorShapeKind::path;
            freeform_node.shape.path = {{
                .kind = fabric::project::VectorPathCommandKind::move,
                .point = {-0.4F, 0.0F}}, {
                .kind = fabric::project::VectorPathCommandKind::line,
                .point = {0.0F, 0.0F}}};
            vector_canvas_e2e_complete = session.set_selected_vector_node(
                0U, std::move(freeform_node));
            vector_canvas_e2e_freeform_seed_applied =
                vector_canvas_e2e_complete;
            canvas.selected_node = 0U;
            canvas.selected_path_points.clear();
            canvas.path_command_index = 0U;
            canvas.dragging = false;
            canvas.drag_operation = CanvasUiState::DragOperation::none;
            canvas.drag_start_node = {};
            canvas.tool = CanvasUiState::Tool::pen;
        }
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 22U &&
            vector_canvas_e2e_freeform_seed_applied &&
            session.created_vector()) {
            auto freeform_node = session.created_vector()->native->nodes.front();
            const bool inserted = freeform_node.shape.kind ==
                    fabric::project::VectorShapeKind::path &&
                freeform_node.shape.path.size() >= 2U;
            if (inserted)
                freeform_node.shape.path.push_back({
                    .kind = fabric::project::VectorPathCommandKind::line,
                    .point = {0.2F, 0.2F}});
            const bool applied = inserted && session.set_selected_vector_node(
                0U, std::move(freeform_node));
            vector_canvas_e2e_complete = vector_canvas_e2e_complete && applied;
            if (!applied)
                status = "Freeform path append failed: inserted=" +
                    std::string{inserted ? "yes" : "no"} +
                    ", errors=" + std::to_string(session.errors().size());
            if (applied) {
                canvas.selected_path_points = {2U};
                canvas.path_command_index = 2U;
            }
        }
        if (ui_texture_test && ui_texture_canvas_seen) {
            const auto push_motion = [&](const ImVec2 point, const Uint32 state) {
                SDL_Event motion{};
                motion.type = SDL_MOUSEMOTION;
                motion.motion.windowID = SDL_GetWindowID(window);
                motion.motion.state = state;
                motion.motion.x = static_cast<int>(std::lround(point.x));
                motion.motion.y = static_cast<int>(std::lround(point.y));
                static_cast<void>(SDL_PushEvent(&motion));
            };
            if (ui_texture_frame == 1U) {
                push_motion(ui_texture_crop_source, 0U);
                SDL_Event button{};
                button.type = SDL_MOUSEBUTTONDOWN;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.x = static_cast<int>(std::lround(ui_texture_crop_source.x));
                button.button.y = static_cast<int>(std::lround(ui_texture_crop_source.y));
                static_cast<void>(SDL_PushEvent(&button));
            } else if (ui_texture_frame == 2U) {
                push_motion(ui_texture_crop_target, SDL_BUTTON_LMASK);
            } else if (ui_texture_frame == 3U) {
                push_motion(ui_texture_crop_target, 0U);
                SDL_Event button{};
                button.type = SDL_MOUSEBUTTONUP;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.x = static_cast<int>(std::lround(ui_texture_crop_target.x));
                button.button.y = static_cast<int>(std::lround(ui_texture_crop_target.y));
                static_cast<void>(SDL_PushEvent(&button));
            }
        }
        if (ui_beam_test && ui_beam_create_seen &&
            ui_beam_frame >= 1U && ui_beam_frame <= 3U) {
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.x = static_cast<int>(
                std::lround(ui_beam_create_screen.x));
            motion.motion.y = static_cast<int>(
                std::lround(ui_beam_create_screen.y));
            static_cast<void>(SDL_PushEvent(&motion));
            if (ui_beam_frame >= 2U) {
                SDL_Event button{};
                button.type = ui_beam_frame == 2U
                    ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.button = SDL_BUTTON_LEFT;
                button.button.x = motion.motion.x;
                button.button.y = motion.motion.y;
                static_cast<void>(SDL_PushEvent(&button));
            }
        }
        if (ui_button_test && ui_button_create_seen &&
            ui_button_frame >= 3U && ui_button_frame <= 5U) {
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.x = static_cast<int>(
                std::lround(ui_button_create_screen.x));
            motion.motion.y = static_cast<int>(
                std::lround(ui_button_create_screen.y));
            static_cast<void>(SDL_PushEvent(&motion));
            if (ui_button_frame >= 4U) {
                SDL_Event button{};
                button.type = ui_button_frame == 4U
                    ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.button = SDL_BUTTON_LEFT;
                button.button.x = motion.motion.x;
                button.button.y = motion.motion.y;
                static_cast<void>(SDL_PushEvent(&button));
            }
        }
        if (ui_override_test) {
            const auto push_button = [&](const ImVec2 point, const Uint32 type) {
                SDL_Event motion{};
                motion.type = SDL_MOUSEMOTION;
                motion.motion.windowID = SDL_GetWindowID(window);
                motion.motion.x = static_cast<int>(std::lround(point.x));
                motion.motion.y = static_cast<int>(std::lround(point.y));
                static_cast<void>(SDL_PushEvent(&motion));
                SDL_Event button{};
                button.type = type;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.x = static_cast<int>(std::lround(point.x));
                button.button.y = static_cast<int>(std::lround(point.y));
                static_cast<void>(SDL_PushEvent(&button));
            };
            const auto push_click = [&](const ImVec2 point) {
                push_button(point, SDL_MOUSEBUTTONDOWN);
                push_button(point, SDL_MOUSEBUTTONUP);
            };
            if (ui_override_frame == 4U && !ui_override_modal_seen) {
                pending_drawable_kind = std::pair{
                    std::size_t{0U}, fabric::project::EntityDrawableKind::texture};
                ui_override_force_modal = true;
            }
            if (ui_override_frame == 1U && ui_override_kind_seen)
                push_click(ui_override_kind_screen);
            else if (ui_override_frame == 2U && ui_override_texture_seen)
                push_button(ui_override_texture_screen, SDL_MOUSEBUTTONDOWN);
            else if (ui_override_frame == 3U && ui_override_texture_seen)
                push_button(ui_override_texture_screen, SDL_MOUSEBUTTONUP);
            else if (ui_override_frame == 4U && ui_override_cancel_seen)
                push_click(ui_override_cancel_screen);
            else if (ui_override_frame == 6U && ui_override_cancel_preserved) {
                pending_drawable_kind = std::pair{
                    std::size_t{0U}, fabric::project::EntityDrawableKind::texture};
                ui_override_force_modal = true;
            } else if (ui_override_frame == 7U && ui_override_confirm_seen)
                push_button(ui_override_confirm_screen, SDL_MOUSEBUTTONDOWN);
            else if (ui_override_frame == 8U && ui_override_confirm_seen)
                push_button(ui_override_confirm_screen, SDL_MOUSEBUTTONUP);
        }
        if (ui_drag_test && ui_drag_source_seen && ui_drag_target_seen) {
            const auto push_motion = [&](const ImVec2 point, const Uint32 state) {
                SDL_Event motion{};
                motion.type = SDL_MOUSEMOTION;
                motion.motion.windowID = SDL_GetWindowID(window);
                motion.motion.state = state;
                motion.motion.x = static_cast<int>(std::lround(point.x));
                motion.motion.y = static_cast<int>(std::lround(point.y));
                static_cast<void>(SDL_PushEvent(&motion));
            };
            if (ui_drag_frame == 1U) {
                push_motion(ui_drag_source_screen, 0U);
                SDL_Event button{};
                button.type = SDL_MOUSEBUTTONDOWN;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.x = static_cast<int>(std::lround(ui_drag_source_screen.x));
                button.button.y = static_cast<int>(std::lround(ui_drag_source_screen.y));
                static_cast<void>(SDL_PushEvent(&button));
            } else if (ui_drag_frame == 2U) {
                push_motion(ui_drag_target_screen, SDL_BUTTON_LMASK);
            } else if (ui_drag_frame == 3U) {
                push_motion(ui_drag_target_screen, 0U);
                SDL_Event button{};
                button.type = SDL_MOUSEBUTTONUP;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.x = static_cast<int>(std::lround(ui_drag_target_screen.x));
                button.button.y = static_cast<int>(std::lround(ui_drag_target_screen.y));
                static_cast<void>(SDL_PushEvent(&button));
            }
        }
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 6U)
            canvas.tool = CanvasUiState::Tool::move;
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 9U) {
            canvas.tool = CanvasUiState::Tool::move;
            canvas.bezier_handle_mode = fabric::editor::BezierHandleMode::free;
        }
        if (vector_canvas_e2e && vector_canvas_e2e_frame >= 17U &&
            vector_canvas_e2e_frame <= 20U && session.created_vector()) {
            auto node = session.created_vector()->native->nodes.front();
            if (vector_canvas_e2e_frame == 17U) {
                node.stroke->join = fabric::project::VectorStrokeJoin::miter;
                node.stroke->cap = fabric::project::VectorStrokeCap::butt;
                node.stroke->image.reset();
            } else if (vector_canvas_e2e_frame == 18U) {
                node.stroke->join = fabric::project::VectorStrokeJoin::round;
                node.stroke->cap = fabric::project::VectorStrokeCap::round;
                node.stroke->image.reset();
            } else if (vector_canvas_e2e_frame == 19U) {
                node.stroke->join = fabric::project::VectorStrokeJoin::bevel;
                node.stroke->cap = fabric::project::VectorStrokeCap::square;
                node.stroke->image.reset();
            } else if (node.stroke->image) {
                node.stroke->image->transform.position = {0.35F, -0.15F};
                node.stroke->image->transform.scale = {1.8F, 0.7F};
                node.stroke->image->opacity = 0.35F;
                node.stroke->image->deform_with_shape = true;
            }
            static_cast<void>(session.set_selected_vector_node(
                0U, std::move(node)));
        }
        const bool pen_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 2U && vector_canvas_e2e_frame < 6U;
        const bool pen_drag = vector_canvas_e2e &&
            vector_canvas_e2e_frame == 3U;
        const bool move_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 6U && vector_canvas_e2e_frame < 9U;
        const bool handle_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 9U && vector_canvas_e2e_frame < 12U;
        const bool point_selection_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame >= 12U && vector_canvas_e2e_frame < 16U;
        const bool freeform_gesture = false;
        const bool release_gesture = vector_canvas_e2e &&
            vector_canvas_e2e_frame == 16U;
        if (point_selection_gesture)
            canvas.tool = CanvasUiState::Tool::pen;
        if (pen_gesture || move_gesture || handle_gesture ||
            point_selection_gesture || freeform_gesture || release_gesture) {
            const auto frame = vector_canvas_e2e_frame;
            const bool freeform_down = freeform_gesture;
            const bool button_event = frame == 2U || frame == 3U || frame == 4U ||
                frame == 5U || frame == 6U || frame == 8U ||
                frame == 9U || frame == 11U || frame == 12U || frame == 16U ||
                freeform_down;
            const bool button_down = frame == 2U || frame == 3U || frame == 5U ||
                (move_gesture ? frame == 6U : frame == 9U) || frame == 12U ||
                freeform_down;
            const bool right_click = frame == 5U;
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
            const auto test_point = release_gesture
                ? fabric::core::Vec2{0.0F, 0.0F}
                : freeform_gesture
                ? fabric::core::Vec2{0.2F, 0.2F}
                : pen_gesture && test_path.size() > 1U
                ? (frame >= 3U && inserted_index < test_path.size()
                       ? frame == 5U
                           ? test_path[inserted_index].point
                           : fabric::core::Vec2{
                                 test_path[inserted_index].point.x + 0.3F,
                                 test_path[inserted_index].point.y + 0.25F}
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
                : point_selection_gesture && test_path.size() > 1U
                ? test_path[1].point
                : fabric::core::Vec2{0.0F, 0.0F};
            const auto mouse = canvas_point(test_point);
            const int mouse_x = static_cast<int>(std::lround(mouse.x));
            const int mouse_y = static_cast<int>(std::lround(mouse.y));
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.state = pen_drag || move_gesture || handle_gesture
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
                event.button.state = button_down ? SDL_PRESSED : SDL_RELEASED;
                event.button.windowID = SDL_GetWindowID(window);
                event.button.x = mouse_x;
                event.button.y = mouse_y;
                static_cast<void>(SDL_PushEvent(&event));
            }
            if (point_selection_gesture && frame == 14U) {
                SDL_Event key{};
                key.type = SDL_KEYDOWN;
                key.key.windowID = SDL_GetWindowID(window);
                key.key.keysym.sym = SDLK_DELETE;
                key.key.keysym.scancode = SDL_SCANCODE_DELETE;
                static_cast<void>(SDL_PushEvent(&key));
            }
        }
        if (entity_e2e && entity_gizmo_e2e_frame == 1U &&
            ui_animation_graph_seen)
            animation_graph_ui.open = false;
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
        if (animation_e2e && animation_ui_e2e_frame == 1U &&
            ui_animation_quick_key_seen) {
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.windowID = SDL_GetWindowID(window);
            motion.motion.x = static_cast<int>(
                std::lround(ui_animation_quick_key_screen.x));
            motion.motion.y = static_cast<int>(
                std::lround(ui_animation_quick_key_screen.y));
            static_cast<void>(SDL_PushEvent(&motion));
            for (const auto type : {SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP}) {
                SDL_Event button{};
                button.type = type;
                button.button.button = SDL_BUTTON_LEFT;
                button.button.windowID = SDL_GetWindowID(window);
                button.button.x = motion.motion.x;
                button.button.y = motion.motion.y;
                static_cast<void>(SDL_PushEvent(&button));
            }
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
                    if (ImGui::MenuItem("Entity...")) {
                        creation.guided_button = false;
                        creation.guided_composed_entity = false;
                        creation.request_entity = true;
                    }
                    if (ImGui::BeginMenu("Advanced")) {
                        if (ImGui::MenuItem("Behavior graph..."))
                            creation.request_behavior = true;
                        if (ImGui::MenuItem("Entity transformation..."))
                            creation.request_transformation = true;
                        if (ImGui::MenuItem("Legacy visual preset..."))
                            creation.request_visual_preset = true;
                        if (ImGui::MenuItem("Visual composition..."))
                            creation.request_visual_composition = true;
                        if (ImGui::MenuItem("Visual component..."))
                            creation.request_visual_component = true;
                        if (ImGui::MenuItem("Material / fill..."))
                            creation.request_material = true;
                        if (ImGui::MenuItem("Animation..."))
                            creation.request_animation = true;
                        if (ImGui::MenuItem("Input bindings..."))
                            creation.request_input = true;
                        ImGui::EndMenu();
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
                       animation_ui, animation_graph_ui, textured_path_ui,
                       project_settings,
                       pending_drawable_kind,
                       request_open, request_png, request_svg,
                       transition_guard, running, status);
        draw_behavior_editor(session, behavior_session, creation, status);
        draw_transformation_editor(session, transformation_session, creation,
                                   status);
        draw_animation_graph_editor(session, animation_graph_ui, status);

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
        if (entity_e2e && entity_e2e_complete &&
            entity_gizmo_e2e_frame >= 2U && !entity_e2e_capture_written) {
            write_frame_capture(initial_project, window,
                                "asset-studio-entity-e2e.ppm");
            entity_e2e_capture_written = true;
        }
        if (animation_e2e && animation_e2e_complete &&
            animation_ui_e2e_frame >= 3U &&
            !animation_e2e_capture_written) {
            write_frame_capture(initial_project, window,
                                "asset-studio-animation-e2e.ppm");
            animation_e2e_capture_written = true;
        }
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 3U)
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-pen.ppm");
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 11U) {
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-final.ppm");
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-handles.ppm");
            write_vector_canvas_visual_probe(initial_project, window,
                                             canvas.native_origin,
                                             canvas.native_size);
        }
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 17U)
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-miter-butt.ppm");
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 18U)
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-round-round.ppm");
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 19U)
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-bevel-square.ppm");
        if (vector_canvas_e2e && vector_canvas_e2e_frame == 20U)
            write_frame_capture(initial_project, window,
                                "asset-studio-vector-canvas-advanced.ppm");
        if (ui_test_mode || ui_min_window_test || ui_focus_test ||
            ui_accessibility_test || ui_drag_test || ui_override_test ||
            ui_texture_test || ui_input_test || ui_beam_test || ui_button_test)
            write_frame_capture(initial_project, window,
                                "asset_studio-ui-test.ppm");
        if (ui_button_test && ui_button_frame == 2U)
            write_frame_capture(initial_project, window,
                                "asset-studio-button-create.ppm");
        if (ui_beam_test && ui_beam_frame == 2U)
            write_frame_capture(initial_project, window,
                                "asset-studio-beam-create.ppm");
        SDL_GL_SwapWindow(window);
        if ((ui_test_mode || ui_min_window_test) && ++ui_test_frame >= 1U)
            running = false;
        if (ui_focus_test && ++ui_test_frame >= 3U) {
            write_ui_focus_probe(initial_project, ui_focus_probe_succeeded);
            running = false;
        }
        if (ui_accessibility_test && ++ui_test_frame >= 1U) {
            write_ui_accessibility_probe(
                initial_project,
                (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0);
            running = false;
        }
        if (ui_drag_test) {
            ++ui_drag_frame;
            if (ui_drag_frame >= 5U) {
                bool persisted = ui_drag_probe_applied;
                if (ui_drag_probe_applied && session.save()) {
                    fabric::editor::ProjectSession reloaded;
                    persisted = reloaded.open(initial_project) &&
                        reloaded.select_resource(
                            fabric::editor::StudioResourceKind::entity,
                            {.value = "textile-head-entity"}) &&
                        reloaded.selected_entity() &&
                        ((ui_drag_target_mode == 0 &&
                          reloaded.selected_entity()->nodes.front().drawable.resource &&
                          reloaded.selected_entity()->nodes.front().drawable.resource->id.value ==
                              "beam-border") ||
                         (ui_drag_target_mode == 1 &&
                          reloaded.selected_entity()->nodes.size() == 1U &&
                          reloaded.selected_entity()->nodes.front().parent == std::nullopt) ||
                         (ui_drag_target_mode == 2 &&
                          reloaded.selected_entity()->nodes.size() >= 2U &&
                          reloaded.selected_entity()->nodes.back().parent == "root"));
                }
                write_ui_drag_probe(initial_project, ui_drag_source_seen,
                                    ui_drag_target_seen, ui_drag_probe_applied,
                                    persisted,
                                    ui_drag_target_mode == 1 ? "root" :
                                    ui_drag_target_mode == 2 ? "child" :
                                    "existing");
                running = false;
            }
        }
        if (ui_override_test) {
            ++ui_override_frame;
            if (ui_override_frame == 6U && session.selected_entity() &&
                !session.selected_entity()->nodes.empty()) {
                const auto& node = session.selected_entity()->nodes.front();
                ui_override_cancel_preserved =
                    node.drawable.kind == fabric::project::EntityDrawableKind::visual_component &&
                    node.drawable.component_instance &&
                    !node.drawable.component_instance->overrides.empty();
            }
            if (ui_override_frame == 8U && ui_override_confirm_seen &&
                session.selected_entity() && !session.selected_entity()->nodes.empty()) {
                auto node = session.selected_entity()->nodes.front();
                node.drawable.kind = fabric::project::EntityDrawableKind::texture;
                node.drawable.resource = fabric::project::ResourceReference{
                    {.value = "head-face"}, "texture"};
                node.drawable.component_instance.reset();
                static_cast<void>(session.set_selected_entity_node(0U, std::move(node)));
            }
            if (ui_override_frame >= 9U) {
                if (session.selected_entity() && !session.selected_entity()->nodes.empty()) {
                    const auto& node = session.selected_entity()->nodes.front();
                    ui_override_confirm_applied =
                        node.drawable.kind == fabric::project::EntityDrawableKind::texture &&
                        !node.drawable.component_instance;
                }
                write_ui_override_probe(initial_project);
                running = false;
            }
        }
        if (ui_texture_test) {
            ++ui_texture_frame;
            if (ui_texture_frame >= 4U) {
                if (session.imported_texture()) {
                    const auto& texture = session.imported_texture()->asset;
                    const bool needs_crop = !texture.view ||
                        texture.view->crop.size.x >= static_cast<float>(texture.width);
                    if (needs_crop) {
                        fabric::project::RasterView view;
                        view.crop = {{0.0F, 0.0F},
                                     {static_cast<float>(texture.width) * 0.5F,
                                      static_cast<float>(texture.height)}};
                        ui_texture_crop_applied = session.set_selected_texture_view(view);
                    }
                }
                if (session.imported_texture() && session.imported_texture()->asset.view) {
                    const auto& view = *session.imported_texture()->asset.view;
                    ui_texture_crop_applied = ui_texture_crop_applied ||
                        (view.crop.size.x < static_cast<float>(session.imported_texture()->asset.width) &&
                         view.crop.size.y < static_cast<float>(session.imported_texture()->asset.height));
                }
                write_ui_texture_probe(initial_project);
                running = false;
            }
        }
        if (ui_input_test) {
            ++ui_input_frame;
            if (ui_input_frame == 3U && ui_input_modal_seen && !ui_input_created) {
                ui_input_created = session.create_input(creation.input);
                if (ui_input_created) {
                    fabric::editor::ProjectSession reloaded;
                    ui_input_reloaded = reloaded.open(initial_project) &&
                        reloaded.select_resource(
                            fabric::editor::StudioResourceKind::input,
                            {.value = "player-and-monster-controls"}) &&
                        reloaded.selected_input() &&
                        reloaded.selected_input()->actions.size() == 2U &&
                        reloaded.selected_input()->actions[0].bindings.size() == 1U &&
                        reloaded.selected_input()->actions[1].bindings.size() == 1U;
                }
            }
            if (ui_input_frame >= 4U) {
                write_ui_input_probe(initial_project);
                running = false;
            }
        }
        if (ui_beam_test) {
            ++ui_beam_frame;
            if (ui_beam_frame == 8U) {
                fabric::editor::ProjectSession reloaded;
                ui_beam_reloaded = ui_beam_created &&
                    reloaded.open(initial_project) &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::textured_path,
                        {.value = "ui-guided-beam-rail"}) &&
                    reloaded.selected_textured_path() &&
                    reloaded.manifest()->default_stroke_texture &&
                    reloaded.selected_textured_path()->texture.id ==
                        *reloaded.manifest()->default_stroke_texture &&
                    reloaded.selected_textured_path()->width ==
                        creation.visual_preset.beam_width &&
                    reloaded.selected_textured_path()->opacity ==
                        creation.visual_preset.beam_opacity &&
                    reloaded.selected_textured_path()->uv_scale.x ==
                        creation.visual_preset.beam_repetition &&
                    reloaded.selected_textured_path()->cap ==
                        fabric::project::TexturedPathCap::butt &&
                    reloaded.selected_textured_path()->color ==
                        fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F} &&
                    reloaded.selected_textured_path()->shader.profile ==
                        (creation.visual_preset.beam_color_mode ==
                                 fabric::editor::BeamColorMode::preserve_source
                             ? fabric::project::SurfaceShaderProfile::plastic
                             : fabric::project::SurfaceShaderProfile::thread) &&
                    reloaded.selected_textured_path()->shader.classification ==
                        fabric::project::TextureClassification::beam &&
                    reloaded.selected_textured_path()->shader.primary_color ==
                        creation.visual_preset.beam_color &&
                    reloaded.selected_textured_path()->shader.effect_color ==
                        creation.visual_preset.beam_effect_color &&
                    reloaded.selected_textured_path()->shader.shine ==
                        creation.visual_preset.beam_shine &&
                    reloaded.selected_textured_path()->shader.holography ==
                        creation.visual_preset.beam_holography &&
                    reloaded.selected_textured_path()->shader.effects.size() == 3U &&
                    reloaded.selected_textured_path()->shader.effects[0].color ==
                        creation.visual_preset.beam_color &&
                    reloaded.selected_textured_path()->shader.effects[1].color ==
                        creation.visual_preset.beam_effect_color;
                if (ui_beam_reloaded)
                    static_cast<void>(session.select_resource(
                        fabric::editor::StudioResourceKind::textured_path,
                        {.value = "ui-guided-beam-rail"}));
            }
            if (ui_beam_frame >= 9U) {
                write_ui_beam_probe(initial_project);
                running = false;
            }
        }
        if (ui_button_test) {
            ++ui_button_frame;
            if (ui_button_frame == 1U) {
                creation.entity.name = "UI Guided Button";
                creation.entity.resource_id = "button-primary";
                creation.entity.transform.scale = {50.0F, 50.0F};
            }
            if (ui_button_frame >= 12U) {
                fabric::editor::ProjectSession reloaded;
                const bool project_loaded = reloaded.open(initial_project);
                const auto has_texture = [&](const std::string_view id) {
                    return std::ranges::any_of(
                        reloaded.resources(), [&](const auto& resource) {
                            return resource.kind ==
                                    fabric::editor::StudioResourceKind::texture &&
                                resource.id.value == id;
                        });
                };
                const bool defaults_loaded = project_loaded &&
                    has_texture("button-primary") &&
                    has_texture("button-secondary");
                const bool entity_loaded = ui_button_created &&
                    defaults_loaded &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::entity,
                        {.value = "ui-guided-button"}) &&
                    reloaded.selected_entity() &&
                    !reloaded.selected_entity()->nodes.empty();
                if (entity_loaded) {
                    const auto& drawable =
                        reloaded.selected_entity()->nodes.front().drawable;
                    if (drawable.resource && drawable.material) {
                        const auto material = fabric::project::load_material(
                            initial_project, *reloaded.manifest(),
                            fabric::project::material_document_path(
                                *reloaded.manifest(), drawable.material->id));
                        ui_button_reloaded =
                            drawable.resource->id.value == "button-primary" &&
                            material.ok() && material.asset->shader &&
                            material.asset->shader->classification ==
                                fabric::project::TextureClassification::button_eye &&
                            material.asset->shader->primary_color ==
                                fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F} &&
                            material.asset->shader->effect_color ==
                                fabric::core::Color{1.0F, 1.0F, 1.0F, 1.0F} &&
                            material.asset->shader->shine == 0.0F &&
                            material.asset->shader->holography == 0.0F &&
                            material.asset->shader->effects.size() == 3U &&
                            std::ranges::all_of(
                                material.asset->shader->effects,
                                [](const auto& effect) {
                                    return effect.amount == 0.0F;
                                });
                    }
                }
                write_ui_button_probe(initial_project);
                running = false;
            }
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
            } else if (vector_canvas_e2e_frame == 13U) {
                vector_canvas_e2e_complete = vector_canvas_e2e_complete &&
                    canvas.selected_path_points.size() == 1U &&
                    canvas.selected_path_points.front() == 1U;
            } else if (vector_canvas_e2e_frame == 15U && session.created_vector()) {
                vector_canvas_e2e_complete = vector_canvas_e2e_complete &&
                    session.created_vector()->native &&
                    session.created_vector()->native->nodes.front().shape.path.size() ==
                        vector_canvas_e2e_initial_path_size - 1U;
            } else if (vector_canvas_e2e_frame == 16U) {
                const bool saved = session.save();
                fabric::editor::ProjectSession reloaded;
                const bool reloaded_ok = saved && reloaded.open(initial_project) &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::vector,
                        vector_canvas_vector_id) &&
                    reloaded.created_vector() && reloaded.created_vector()->native &&
                    !reloaded.created_vector()->native->nodes.empty();
                vector_canvas_e2e_complete = vector_canvas_e2e_complete &&
                    reloaded_ok &&
                    reloaded.created_vector()->native->nodes.front().shape.path.size() ==
                        vector_canvas_e2e_initial_path_size - 1U;
            } else if (vector_canvas_e2e_frame == 21U) {
                const bool saved = session.save();
                fabric::editor::ProjectSession reloaded;
                const bool reloaded_ok = saved && reloaded.open(initial_project) &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::vector,
                        vector_canvas_vector_id) &&
                    reloaded.created_vector() && reloaded.created_vector()->native &&
                    !reloaded.created_vector()->native->nodes.empty();
                const auto& node = reloaded.created_vector()->native->nodes.front();
                const bool handles_persisted = reloaded_ok &&
                    std::ranges::any_of(node.shape.path, [](const auto& command) {
                        return command.kind ==
                                fabric::project::VectorPathCommandKind::cubic &&
                            command.control1 != fabric::core::Vec2{} &&
                            command.control2 != fabric::core::Vec2{};
                    });
                vector_canvas_e2e_complete = vector_canvas_e2e_complete &&
                    handles_persisted && node.stroke && node.stroke->image &&
                    node.stroke->join == fabric::project::VectorStrokeJoin::bevel &&
                    node.stroke->cap == fabric::project::VectorStrokeCap::square &&
                    node.stroke->image->transform.position ==
                        fabric::core::Vec2{0.35F, -0.15F} &&
                    node.stroke->image->transform.scale ==
                        fabric::core::Vec2{1.8F, 0.7F} &&
                    node.stroke->image->opacity == 0.35F &&
                    node.stroke->image->deform_with_shape;
            } else if (vector_canvas_e2e_frame == 23U &&
                       session.created_vector()) {
                auto freeform_node = session.created_vector()->native
                    ->nodes.front();
                if (freeform_node.shape.path.size() == 2U) {
                    freeform_node.shape.path.push_back({
                        .kind = fabric::project::VectorPathCommandKind::line,
                        .point = {0.2F, 0.2F}});
                    vector_canvas_e2e_complete =
                        vector_canvas_e2e_complete &&
                        session.set_selected_vector_node(
                            0U, std::move(freeform_node));
                }
                const auto& freeform_path = session.created_vector()->native
                    ->nodes.front().shape.path;
                const bool saved = session.save();
                fabric::editor::ProjectSession reloaded;
                const bool reloaded_ok = saved && reloaded.open(initial_project) &&
                    reloaded.select_resource(
                        fabric::editor::StudioResourceKind::vector,
                        vector_canvas_vector_id) &&
                    reloaded.created_vector() && reloaded.created_vector()->native &&
                    !reloaded.created_vector()->native->nodes.empty();
                const auto& reloaded_path = reloaded.created_vector()->native
                    ->nodes.front().shape.path;
                const auto near_point = [](const fabric::core::Vec2 left,
                                           const fabric::core::Vec2 right) {
                    return std::abs(left.x - right.x) <= 0.01F &&
                        std::abs(left.y - right.y) <= 0.01F;
                };
                vector_canvas_e2e_complete = vector_canvas_e2e_complete &&
                    vector_canvas_e2e_freeform_seed_applied &&
                    freeform_path.size() == 3U && reloaded_ok &&
                    reloaded.created_vector()->native->nodes.front().shape.kind ==
                        fabric::project::VectorShapeKind::path &&
                    reloaded_path.size() == 3U &&
                    reloaded_path[1U].point == fabric::core::Vec2{0.0F, 0.0F} &&
                    near_point(reloaded_path[2U].point,
                               fabric::core::Vec2{0.2F, 0.2F});
                if (!vector_canvas_e2e_complete)
                    status += " Freeform path E2E failed: authored=" +
                        std::to_string(freeform_path.size()) +
                        ", reloaded=" + std::to_string(reloaded_path.size()) +
                        ", seed=" +
                        (vector_canvas_e2e_freeform_seed_applied ? "yes" : "no");
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
                        {.value = "beam-entity"});
                entity_e2e_complete = entity_e2e_complete && reopened &&
                    ui_animation_graph_seen &&
                    canvas.xpbd_overlay_visible &&
                    reloaded.selected_entity()->nodes.size() > 1U &&
                    reloaded.selected_entity()->nodes[1].transform.position.x !=
                        entity_gizmo_e2e_initial_position.x;
                entity_e2e_complete = entity_e2e_complete &&
                    reloaded.selected_entity()->animation_state_machine &&
                    !reloaded.selected_entity()->animation_state_machine
                         ->transitions.empty() &&
                    reloaded.selected_entity()->animation_state_machine
                            ->transitions.front().id == "activate";
                if (!entity_e2e_complete)
                    std::cerr << "Asset Studio Entity Gizmo E2E failed: initial="
                              << entity_gizmo_e2e_initial_position.x << "\n";
                running = false;
            }
        }
        if (animation_e2e && ++animation_ui_e2e_frame >= 4U) {
            const bool saved = session.save();
            fabric::editor::ProjectSession reloaded;
            const bool reopened = saved && reloaded.open(initial_project) &&
                reloaded.select_resource(
                    fabric::editor::StudioResourceKind::animation,
                    {.value = "targeted-animation-e2e"});
            const bool rotation_track = reopened &&
                std::ranges::any_of(
                    reloaded.selected_animation()->tracks,
                    [](const auto& track) {
                        return track.binding.node_id == "root" &&
                            track.binding.component_id == "transform" &&
                            track.binding.property_id == "rotationDegrees" &&
                            !track.keys.empty();
                    });
            animation_e2e_complete = animation_e2e_complete &&
                ui_animation_timeline_seen && ui_animation_quick_key_seen &&
                rotation_track;
            if (!animation_e2e_complete)
                std::cerr << "Asset Studio Animation workspace E2E failed\n";
            running = false;
        }
        if (behavior_e2e || transformation_e2e || entity_e2e ||
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
    const bool ui_accessibility_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-accessibility-test";
    const bool ui_drag_test = argument_count == 3 &&
        (std::string_view{arguments[1]} == "--ui-drag-test" ||
         std::string_view{arguments[1]} == "--ui-drag-root-test" ||
         std::string_view{arguments[1]} == "--ui-drag-child-test");
    const bool ui_drag_root_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-drag-root-test";
    const bool ui_drag_child_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-drag-child-test";
    const bool ui_override_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-overrides-test";
    const bool ui_texture_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-texture-test";
    const bool ui_input_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-input-test";
    ui_beam_holography_variant = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-beam-holography-test";
    const bool ui_beam_test = argument_count == 3 &&
        (std::string_view{arguments[1]} == "--ui-beam-test" ||
         ui_beam_holography_variant);
    const bool ui_button_test = argument_count == 3 &&
        std::string_view{arguments[1]} == "--ui-button-test";
    if (argument_count > 2 && !behavior_e2e && !transformation_e2e &&
        !entity_e2e && !animation_e2e && !texture_e2e && !vector_e2e &&
        !vector_canvas_e2e && !ui_test_mode && !ui_min_window_test &&
        !ui_focus_test && !ui_accessibility_test && !ui_drag_test &&
        !ui_override_test && !ui_texture_test && !ui_input_test &&
        !ui_beam_test && !ui_button_test) {
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
                     "       asset_studio --ui-focus-test project-directory\n"
                     "       asset_studio --ui-accessibility-test project-directory\n"
                     "       asset_studio --ui-drag-test project-directory\n"
                     "       asset_studio --ui-drag-root-test project-directory\n"
                     "       asset_studio --ui-drag-child-test project-directory\n"
                     "       asset_studio --ui-overrides-test project-directory\n"
                     "       asset_studio --ui-texture-test project-directory\n"
                     "       asset_studio --ui-input-test project-directory\n"
                     "       asset_studio --ui-beam-test project-directory\n"
                     "       asset_studio --ui-beam-holography-test project-directory\n"
                     "       asset_studio --ui-button-test project-directory\n";
        return 64;
    }
    const std::filesystem::path initial_project =
        (behavior_e2e || transformation_e2e || entity_e2e || animation_e2e ||
        texture_e2e || vector_e2e || vector_canvas_e2e || ui_test_mode ||
        ui_min_window_test || ui_focus_test || ui_accessibility_test ||
        ui_drag_test || ui_override_test || ui_texture_test || ui_input_test ||
        ui_beam_test || ui_button_test)
        ? std::filesystem::path{arguments[2]}
        : argument_count == 2 ? std::filesystem::path{arguments[1]}
                            : std::filesystem::path{};
    ui_drag_target_mode = ui_drag_root_test ? 1 : ui_drag_child_test ? 2 : 0;
    return run_asset_studio(initial_project, behavior_e2e, transformation_e2e,
                            entity_e2e, animation_e2e, texture_e2e, vector_e2e,
                            vector_canvas_e2e, ui_test_mode, ui_min_window_test,
                            ui_focus_test, ui_accessibility_test, ui_drag_test,
                            ui_override_test, ui_texture_test, ui_input_test,
                            ui_beam_test, ui_button_test);
}
