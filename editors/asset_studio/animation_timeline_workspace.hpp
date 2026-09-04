#pragma once

#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace fabric::asset_studio {

struct AnimationWorkspaceState {
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
    std::string document_id;
    std::string node_id{"root"};
    std::string component_id{"transform"};
    std::string property_id{"position"};
    int binding_preset{};
    std::string visual_component_id;
    std::string marker_id{"marker"};
    bool marker_audio_enabled{};
    std::string marker_audio_id;
    std::string marker_audio_event_id;
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
    float dragging_key_pivot_time{};
    bool scaling_keys{};
    std::vector<KeySelection> selected_keys;
    std::vector<ClipboardEntry> key_clipboard;
    bool box_selecting{};
    bool box_select_additive{};
    ImVec2 box_select_start{};
    ImVec2 box_select_current{};
    bool curve_view{};
};


struct AnimationTimelineProbe {
    bool enabled{};
    bool workflow_enabled{};
    bool timeline_seen{};
    bool playback_advanced{};
    bool play_seen{};
    bool marker_seen{};
    bool second_key_seen{};
    ImVec2 play_screen{};
    ImVec2 marker_screen{};
    ImVec2 second_key_screen{};
    std::optional<float> second_key_original_time;
};

void draw_animation_timeline_workspace(
    editor::ProjectSession& session,
    AnimationWorkspaceState& state,
    std::string& status,
    AnimationTimelineProbe* probe = nullptr);

} // namespace fabric::asset_studio
