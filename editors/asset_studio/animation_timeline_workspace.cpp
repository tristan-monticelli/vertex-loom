#include "animation_timeline_workspace.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace fabric::asset_studio {

bool same_animation_key(const AnimationWorkspaceState::KeySelection& left,
                        const AnimationWorkspaceState::KeySelection& right) {
    return left.index == right.index && left.binding == right.binding;
}

void draw_animation_timeline_workspace(
    fabric::editor::ProjectSession& session,
    AnimationWorkspaceState& ui,
    std::string& status,
    AnimationTimelineProbe* probe) {
    if (!session.selected_animation()) return;
    const auto& document_id = session.selected_animation()->document.id.value;
    if (ui.document_id != document_id) {
        ui.document_id = document_id;
        ui.playing = false;
        ui.dragging_key.reset();
        ui.scaling_keys = false;
        ui.selected_keys.clear();
        ui.box_selecting = false;
    }
    if ((probe != nullptr && probe->enabled)) probe->timeline_seen = true;
    auto& clip = *session.selected_animation();
    if (ui.playing) {
        const float before = ui.scrub_time;
        ui.scrub_time += ImGui::GetIO().DeltaTime;
        if ((probe != nullptr && probe->workflow_enabled) &&
            ui.scrub_time > before)
            probe->playback_advanced = true;
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
    std::optional<std::string> quick_marker;
    if (ImGui::Button(ui.playing ? "Pause" : "Play")) ui.playing = !ui.playing;
    if ((probe != nullptr && probe->workflow_enabled)) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->play_screen = {(minimum.x + maximum.x) * 0.5F,
                                    (minimum.y + maximum.y) * 0.5F};
        probe->play_seen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        ui.playing = false;
        ui.scrub_time = 0.0F;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add event at playhead")) {
        std::string id = "event-" + std::to_string(clip.markers.size() + 1U);
        while (std::ranges::any_of(clip.markers, [&](const auto& marker) {
            return marker.id == id;
        })) id += "-copy";
        quick_marker = std::move(id);
    }
    if ((probe != nullptr && probe->workflow_enabled)) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->marker_screen = {(minimum.x + maximum.x) * 0.5F,
                                      (minimum.y + maximum.y) * 0.5F};
        probe->marker_seen = true;
    }
    ImGui::SameLine();
    ImGui::Text("%.2f / %.2f s", ui.scrub_time, clip.duration);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::SliderFloat("Zoom", &ui.timeline_zoom, 0.5F, 4.0F, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox("Curves", &ui.curve_view);
    if (ui.curve_view && !ui.selected_keys.empty()) {
        const auto binding = ui.selected_keys.front().binding;
        const auto selected_track = std::ranges::find(
            clip.tracks, binding, &fabric::project::AnimationTrack::binding);
        if (selected_track != clip.tracks.end()) {
            auto interpolation = selected_track->interpolation;
            auto easing = selected_track->easing;
            bool changed = false;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(105.0F);
            if (ImGui::BeginCombo(
                    "##curve-interpolation",
                    fabric::project::to_string(interpolation).data())) {
                for (const auto option : {
                         fabric::project::AnimationInterpolation::step,
                         fabric::project::AnimationInterpolation::linear,
                         fabric::project::AnimationInterpolation::cubic}) {
                    if (ImGui::Selectable(
                            fabric::project::to_string(option).data(),
                            interpolation == option)) {
                        interpolation = option;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(105.0F);
            if (ImGui::BeginCombo("##curve-easing",
                                  fabric::project::to_string(easing).data())) {
                for (const auto option : {
                         fabric::project::AnimationEasing::linear,
                         fabric::project::AnimationEasing::ease_in,
                         fabric::project::AnimationEasing::ease_out,
                         fabric::project::AnimationEasing::ease_in_out}) {
                    if (ImGui::Selectable(
                            fabric::project::to_string(option).data(),
                            easing == option)) {
                        easing = option;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (changed) {
                status = session.set_selected_animation_track_curve(
                    binding, interpolation, easing)
                    ? "Animation curve changed."
                    : "Curve change rejected; inspect diagnostics.";
            }
        }
    }
    ImGui::TextDisabled(
        "Drag selected keys to move · Alt+drag to scale around the playhead");

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

    std::optional<AnimationWorkspaceState::KeySelection> hovered_key;
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
        if (ui.curve_view && track.keys.size() > 1U) {
            const auto scalar_value = [](const fabric::project::AnimationValue& value)
                -> std::optional<float> {
                if (const auto* scalar = std::get_if<float>(&value)) return *scalar;
                if (const auto* point = std::get_if<fabric::core::Vec2>(&value))
                    return point->x;
                if (const auto* color = std::get_if<fabric::core::Color>(&value))
                    return color->red;
                if (const auto* boolean = std::get_if<bool>(&value))
                    return *boolean ? 1.0F : 0.0F;
                return std::nullopt;
            };
            std::vector<float> values;
            for (const auto& key : track.keys)
                if (const auto value = scalar_value(key.value))
                    values.push_back(*value);
            if (!values.empty()) {
                const auto [minimum, maximum] = std::minmax_element(
                    values.begin(), values.end());
                const float range = std::max(0.0001F, *maximum - *minimum);
                std::vector<ImVec2> curve;
                constexpr std::size_t samples = 48U;
                curve.reserve(samples + 1U);
                for (std::size_t sample = 0; sample <= samples; ++sample) {
                    const float time = clip.duration *
                        static_cast<float>(sample) / static_cast<float>(samples);
                    const auto evaluated = fabric::project::evaluate_animation(
                        clip, time);
                    const auto property = std::ranges::find(
                        evaluated.properties, track.binding,
                        &fabric::project::EvaluatedProperty::binding);
                    if (property == evaluated.properties.end()) continue;
                    const auto value = scalar_value(property->value);
                    if (!value) continue;
                    const float normalized = (*value - *minimum) / range;
                    curve.push_back({x_for_time(time),
                                     top + row_height - 4.0F -
                                         normalized * (row_height - 8.0F)});
                }
                if (curve.size() > 1U)
                    draw->AddPolyline(curve.data(),
                                      static_cast<int>(curve.size()),
                                      IM_COL32(115, 225, 155, 210),
                                      ImDrawFlags_None, 1.5F);
            }
        }
        for (std::size_t key_index = 0; key_index < track.keys.size(); ++key_index) {
            const AnimationWorkspaceState::KeySelection candidate{track.binding, key_index};
            const bool dragging = ui.dragging_key &&
                same_animation_key(*ui.dragging_key, candidate);
            const bool selected = std::ranges::any_of(
                ui.selected_keys, [&](const auto& value) {
                    return same_animation_key(value, candidate);
                });
            float key_time = dragging ? ui.dragging_key_time
                                      : track.keys[key_index].time;
            if (ui.scaling_keys && ui.dragging_key && selected) {
                const float denominator = ui.dragging_key_original_time -
                    ui.dragging_key_pivot_time;
                if (std::abs(denominator) > 0.0001F) {
                    const float scale = (ui.dragging_key_time -
                        ui.dragging_key_pivot_time) / denominator;
                    if (scale > 0.0F) {
                        key_time = ui.dragging_key_pivot_time +
                            (track.keys[key_index].time -
                             ui.dragging_key_pivot_time) * scale;
                    }
                }
            }
            const float x = x_for_time(key_time);
            if ((probe != nullptr && probe->workflow_enabled) &&
                track.binding.property_id == "position" && key_index == 1U) {
                probe->second_key_screen = {x, center};
                probe->second_key_seen = true;
                if (!probe->second_key_original_time)
                    probe->second_key_original_time =
                        track.keys[key_index].time;
            }
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
            const bool was_selected = std::ranges::any_of(
                ui.selected_keys, [&](const auto& value) {
                    return same_animation_key(value, *hovered_key);
                });
            if (!additive && !was_selected) ui.selected_keys.clear();
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
                ui.scaling_keys = ImGui::GetIO().KeyAlt;
                ui.dragging_key_pivot_time = ui.scrub_time;
                if (!ui.scaling_keys)
                    ui.scrub_time = track->keys[hovered_key->index].time;
                ui.dragging_key = hovered_key;
                ui.dragging_key_time = track->keys[hovered_key->index].time;
                ui.dragging_key_original_time = ui.dragging_key_time;
            }
        } else if (mouse.x >= time_start) {
            ui.scrub_time = time_for_x(mouse.x);
            if (mouse.y >= origin.y + header_height) {
                ui.box_selecting = true;
                ui.box_select_additive = ImGui::GetIO().KeyCtrl ||
                    ImGui::GetIO().KeySuper;
                ui.box_select_start = mouse;
                ui.box_select_current = mouse;
                if (!ui.box_select_additive) ui.selected_keys.clear();
            }
        }
    }
    if (ui.box_selecting && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ui.box_select_current = mouse;
    if (ui.box_selecting) {
        const ImVec2 minimum{std::min(ui.box_select_start.x,
                                     ui.box_select_current.x),
                             std::min(ui.box_select_start.y,
                                     ui.box_select_current.y)};
        const ImVec2 maximum{std::max(ui.box_select_start.x,
                                     ui.box_select_current.x),
                             std::max(ui.box_select_start.y,
                                     ui.box_select_current.y)};
        draw->AddRectFilled(minimum, maximum, IM_COL32(80, 155, 235, 40));
        draw->AddRect(minimum, maximum, IM_COL32(100, 190, 255, 230));
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            for (std::size_t track_index = 0;
                 track_index < clip.tracks.size(); ++track_index) {
                const auto& track = clip.tracks[track_index];
                const float center = origin.y + header_height + row_height *
                    (static_cast<float>(track_index) + 0.5F);
                for (std::size_t key_index = 0;
                     key_index < track.keys.size(); ++key_index) {
                    const float x = x_for_time(track.keys[key_index].time);
                    if (x < minimum.x || x > maximum.x ||
                        center < minimum.y || center > maximum.y) continue;
                    const AnimationWorkspaceState::KeySelection selected{
                        track.binding, key_index};
                    if (std::ranges::none_of(
                            ui.selected_keys, [&](const auto& candidate) {
                                return same_animation_key(candidate, selected);
                            }))
                        ui.selected_keys.push_back(selected);
                }
            }
            status = std::to_string(ui.selected_keys.size()) +
                " animation key(s) selected.";
            ui.box_selecting = false;
        }
    }
    if (ui.dragging_key && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ui.dragging_key_time = time_for_x(mouse.x);
    if (ui.dragging_key && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const auto moving = *ui.dragging_key;
        if (std::abs(ui.dragging_key_time - ui.dragging_key_original_time) <
            0.0001F) {
            ui.scrub_time = ui.dragging_key_time;
        } else {
            const float delta = ui.dragging_key_time -
                ui.dragging_key_original_time;
            std::vector<fabric::editor::AnimationKeySelection> selection;
            selection.reserve(ui.selected_keys.size());
            for (const auto& selected : ui.selected_keys) {
                selection.push_back({selected.binding, selected.index});
            }
            if (selection.empty()) {
                selection.push_back({moving.binding, moving.index});
            }
            const float denominator = ui.dragging_key_original_time -
                ui.dragging_key_pivot_time;
            const float scale = std::abs(denominator) > 0.0001F
                ? (ui.dragging_key_time - ui.dragging_key_pivot_time) /
                    denominator
                : 0.0F;
            const bool changed = ui.scaling_keys
                ? session.scale_selected_animation_keys(
                      selection, ui.dragging_key_pivot_time, scale)
                : session.move_selected_animation_keys(selection, delta);
            if (changed) {
                ui.scrub_time = ui.dragging_key_time;
                const auto moved_count = selection.size();
                if (!ui.scaling_keys) ui.selected_keys.clear();
                status = ui.scaling_keys
                    ? std::to_string(moved_count) +
                        " animation keys scaled around the playhead."
                    : moved_count == 1U
                        ? "Animation key moved."
                        : std::to_string(moved_count) +
                            " animation keys moved together.";
            } else {
                status = ui.scaling_keys
                    ? "Key scale rejected; keep the group on one side of a valid pivot and inside the clip."
                    : "Key move rejected; keep the group inside the clip.";
            }
        }
        ui.dragging_key.reset();
        ui.scaling_keys = false;
    }
    ImGui::EndChild();
    if (quick_marker) {
        status = session.insert_selected_animation_marker(
            *quick_marker, ui.scrub_time, std::nullopt)
            ? "Animation event added at playhead."
            : "Animation event rejected; inspect diagnostics.";
    }
}


} // namespace fabric::asset_studio
