#include "animation_inspector.hpp"

#include "editor_widgets.hpp"
#include "fabric/project/audio.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fabric::asset_studio {

using editor_ui::draw_disabled_reason;
using editor_ui::draw_technical_tooltip;

void draw_animation_inspector(
    fabric::editor::ProjectSession& session,
    AnimationWorkspaceState& state,
    const bool show_advanced_ids,
    std::string& status,
    const AnimationInspectorResourcePicker resource_picker,
    const AnimationResourceKindLabel resource_kind_label,
    AnimationInspectorProbe* probe) {
    const auto* selected = session.selected_resource();
    if (selected == nullptr ||
        selected->kind != fabric::editor::StudioResourceKind::animation ||
        !session.selected_animation()) {
        return;
    }
    const auto draw_project_resource_picker =
        [&](const char* label,
            const std::span<const fabric::editor::StudioResource> resources,
            const fabric::editor::StudioResourceKind expected_kind,
            std::string& selected_id, const bool optional,
            const bool show_details = true) {
            return resource_picker(label, resources, expected_kind, selected_id,
                                   optional, show_details);
        };
            auto& clip = *session.selected_animation();
            ImGui::SeparatorText("Animation");
            ImGui::TextWrapped(
                "Choose a node, move the playhead, then key the property you want to animate.");
            std::string preview_entity_id = clip.preview_entity
                ? clip.preview_entity->id.value : std::string{};
            if (draw_project_resource_picker(
                    "Preview with", session.resources(),
                    fabric::editor::StudioResourceKind::entity,
                    preview_entity_id, true, false)) {
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
            ImGui::SetItemTooltip("Total clip duration; key times are constrained to this range.");
            bool loop = clip.loop;
            if (ImGui::Checkbox("Loop", &loop)) {
                if (!session.set_selected_animation_loop(loop)) {
                    status = "Animation loop rejected; inspect diagnostics.";
                }
            }
            state.scrub_time = std::clamp(state.scrub_time, 0.0F,
                                                  std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Playhead", &state.scrub_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            if ((probe != nullptr && probe->workflow_enabled)) {
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->playhead_target_screen = {
                    minimum.x + (maximum.x - minimum.x) * 0.75F,
                    (minimum.y + maximum.y) * 0.5F};
                probe->playhead_seen = true;
            }
            ImGui::SetItemTooltip("Preview time used to evaluate the animation clip.");
            ImGui::Checkbox("Auto-key while moving in the Viewer",
                            &state.auto_key);
            if ((probe != nullptr && probe->workflow_enabled)) {
                const auto minimum = ImGui::GetItemRectMin();
                const auto maximum = ImGui::GetItemRectMax();
                probe->auto_key_screen = {
                    (minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F};
                probe->auto_key_seen = true;
            }
            ImGui::TextDisabled(state.auto_key
                ? "Viewer edits now create keys at the playhead."
                : "Turn this on to animate directly in the Viewer.");
            if (ImGui::CollapsingHeader("Timeline options")) {
                ImGui::Checkbox("Snap key times", &state.snap_keys);
                ImGui::SetNextItemWidth(-1.0F);
                ImGui::InputFloat("Snap interval (seconds)",
                                  &state.key_snap_interval,
                                  0.05F, 0.5F, "%.2f s");
                draw_technical_tooltip(
                    "Key times are rounded to this interval when snapping is enabled.");
            }
            state.key_snap_interval = std::max(0.01F,
                                                      state.key_snap_interval);
            const auto snap_key_time = [&](const float time) {
                if (!state.snap_keys) return time;
                return std::round(time / state.key_snap_interval) *
                    state.key_snap_interval;
            };
            const auto evaluated = fabric::project::evaluate_animation(
                clip, state.scrub_time);
            if (ImGui::CollapsingHeader("Evaluated values")) {
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
            }
            if (session.selected_entity() &&
                !session.selected_entity()->nodes.empty()) {
                const auto& nodes = session.selected_entity()->nodes;
                auto selected_node = std::ranges::find(
                    nodes, state.node_id,
                    &fabric::project::EntityNode::id);
                if (selected_node == nodes.end()) {
                    state.node_id = nodes.front().id;
                    selected_node = nodes.begin();
                }
                ImGui::SeparatorText("Animate selected node");
                const char* selected_node_label = selected_node->name.c_str();
                if (ImGui::BeginCombo("Node", selected_node_label)) {
                    for (const auto& candidate : nodes) {
                        if (ImGui::Selectable(candidate.name.c_str(),
                                              candidate.id == state.node_id)) {
                            state.node_id = candidate.id;
                            selected_node = std::ranges::find(
                                nodes, state.node_id,
                                &fabric::project::EntityNode::id);
                        }
                        if (show_advanced_ids) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", candidate.id.c_str());
                        }
                    }
                    ImGui::EndCombo();
                }
                if ((probe != nullptr && probe->enabled))
                    probe->node_picker_seen = true;
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
                            .node_id = state.node_id,
                            .component_id = "transform",
                            .property_id = std::string{property}};
                        const bool clicked = ImGui::Button(label);
                        if ((probe != nullptr && probe->workflow_enabled) &&
                            property == "position") {
                            const auto minimum = ImGui::GetItemRectMin();
                            const auto maximum = ImGui::GetItemRectMax();
                            probe->workflow_position_key_screen = {
                                (minimum.x + maximum.x) * 0.5F,
                                (minimum.y + maximum.y) * 0.5F};
                            probe->workflow_position_key_seen = true;
                        }
                        if ((probe != nullptr && probe->enabled) &&
                            property == "rotationDegrees") {
                            const auto minimum = ImGui::GetItemRectMin();
                            const auto maximum = ImGui::GetItemRectMax();
                            probe->quick_key_screen = {
                                (minimum.x + maximum.x) * 0.5F,
                                (minimum.y + maximum.y) * 0.5F};
                            probe->quick_key_seen = true;
                        }
                        if (!clicked) return;
                        if (session.set_selected_animation_key(
                                binding, state.scrub_time,
                                current_value(binding, std::move(value)),
                                state.interpolation,
                                fabric::editor::AutosaveScheduler::Clock::now(),
                                state.composition, state.easing)) {
                            state.component_id = "transform";
                            state.property_id = std::string{property};
                            status = std::string{label} + " key set at playhead.";
                        } else {
                            status = "Quick key rejected; inspect diagnostics.";
                        }
                    };
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
                ImGui::TextWrapped(
                    "Creates the track when missing and captures the current value.");
            }
            if (ImGui::CollapsingHeader("Markers and audio cues")) {
            ImGui::InputText("Marker id", &state.marker_id);
            state.marker_time = std::clamp(state.marker_time, 0.0F,
                                                   std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Marker time (seconds)", &state.marker_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            draw_technical_tooltip("Timeline position at which the marker is stored.");
            ImGui::Checkbox("Trigger audio event", &state.marker_audio_enabled);
            if (state.marker_audio_enabled) {
                draw_project_resource_picker(
                    "Audio document##animation-marker", session.resources(),
                    fabric::editor::StudioResourceKind::audio,
                    state.marker_audio_id, false);
                std::optional<fabric::project::AudioDocument> marker_audio;
                if (!state.marker_audio_id.empty()) {
                    const auto loaded = fabric::project::load_audio(
                        session.project_root(), *session.manifest(),
                        fabric::project::audio_document_path(
                            *session.manifest(),
                            {.value = state.marker_audio_id}));
                    if (loaded.ok()) marker_audio = *loaded.audio;
                }
                const char* event_preview = state.marker_audio_event_id.empty()
                    ? "Choose an event..."
                    : state.marker_audio_event_id.c_str();
                if (ImGui::BeginCombo("Audio event", event_preview)) {
                    if (marker_audio)
                        for (const auto& event : marker_audio->events)
                            if (ImGui::Selectable(
                                    event.id.c_str(),
                                    event.id == state.marker_audio_event_id))
                                state.marker_audio_event_id = event.id;
                    ImGui::EndCombo();
                }
            }
            const bool marker_audio_valid = !state.marker_audio_enabled ||
                (!state.marker_audio_id.empty() &&
                 !state.marker_audio_event_id.empty());
            ImGui::BeginDisabled(state.marker_id.empty() ||
                                 !marker_audio_valid);
            if (ImGui::Button("Add marker")) {
                std::optional<fabric::project::AnimationAudioCue> audio;
                if (state.marker_audio_enabled)
                    audio = fabric::project::AnimationAudioCue{
                        .audio = {{.value = state.marker_audio_id}, "audio"},
                        .event_id = state.marker_audio_event_id};
                if (session.insert_selected_animation_marker(
                        state.marker_id, state.marker_time,
                        std::move(audio))) {
                    status = "Animation marker added.";
                } else {
                    status = "Animation marker rejected; inspect diagnostics.";
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(state.marker_id.empty(),
                                 "Enter a marker id before adding a marker.");
            draw_disabled_reason(!marker_audio_valid,
                                 "Choose an audio document and one of its events.");
            bool marker_removed = false;
            for (const auto& marker : clip.markers) {
                if (marker.audio)
                    ImGui::BulletText("%s · %.2f s · %s/%s", marker.id.c_str(),
                                      marker.time,
                                      marker.audio->audio.id.value.c_str(),
                                      marker.audio->event_id.c_str());
                else
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
            }
            if (ImGui::CollapsingHeader("Advanced key authoring")) {
            ImGui::SeparatorText("Set key");
            const std::vector<fabric::project::EntityNode>* target_nodes = nullptr;
            const fabric::project::EntityNode* selected_node = nullptr;
            if (session.selected_entity() &&
                !session.selected_entity()->nodes.empty()) {
                target_nodes = &session.selected_entity()->nodes;
                const auto selected_iterator = std::ranges::find(
                    *target_nodes, state.node_id,
                    &fabric::project::EntityNode::id);
                selected_node = selected_iterator == target_nodes->end()
                    ? nullptr : &*selected_iterator;
                const char* node_label = selected_node == nullptr
                    ? "Choose target node..." : selected_node->name.c_str();
                if (ImGui::BeginCombo("Target node", node_label)) {
                    for (const auto& target_node : *target_nodes) {
                        if (ImGui::Selectable(target_node.name.c_str(),
                                target_node.id == state.node_id))
                            state.node_id = target_node.id;
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", target_node.id.c_str());
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled(
                    "Choose a preview entity to bind one of its nodes.");
                state.node_id.clear();
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
                        return descriptor->component_id == state.component_id &&
                            descriptor->property_id == state.property_id;
                    });
                const char* current_label = current == descriptors.end()
                    ? "Choose an entity property..." : (*current)->display_path.c_str();
                if (ImGui::BeginCombo("Entity property", current_label)) {
                    for (const auto* descriptor : descriptors) {
                        if (ImGui::Selectable(descriptor->display_path.c_str(),
                                              descriptor == (current == descriptors.end()
                                                  ? nullptr : *current))) {
                            state.component_id = descriptor->component_id;
                            state.property_id = descriptor->property_id;
                            if (descriptor->value_kind == PropertyKind::vec2)
                                state.key_kind = 0;
                            else if (descriptor->value_kind == PropertyKind::color)
                                state.key_kind = 2;
                            else state.key_kind = 1;
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
            if (ImGui::Combo("Binding preset", &state.binding_preset,
                             binding_presets,
                             static_cast<int>(std::size(binding_presets)))) {
                switch (state.binding_preset) {
                case 1:
                    state.component_id = "transform";
                    state.property_id = "position";
                    state.key_kind = 0;
                    break;
                case 2:
                    state.component_id = "transform";
                    state.property_id = "rotationDegrees";
                    state.key_kind = 1;
                    break;
                case 3:
                    state.component_id = "transform";
                    state.property_id = "scale";
                    state.key_kind = 0;
                    break;
                case 4:
                    state.component_id = "material";
                    state.property_id = "opacity";
                    state.key_kind = 1;
                    break;
                case 5:
                    state.component_id = "material";
                    state.property_id = "color";
                    state.key_kind = 2;
                    break;
                case 6:
                    state.component_id = "fill";
                    state.property_id = "color";
                    state.key_kind = 2;
                    break;
                case 7:
                    state.component_id = "imageFill";
                    state.property_id = "opacity";
                    state.key_kind = 1;
                    break;
                case 8:
                    state.component_id = "imageFill";
                    state.property_id = "position";
                    state.key_kind = 0;
                    break;
                case 9:
                    state.component_id = "imageFill";
                    state.property_id = "scale";
                    state.key_kind = 0;
                    break;
                case 10:
                    state.component_id = "imageFill";
                    state.property_id = "rotationDegrees";
                    state.key_kind = 1;
                    break;
                case 11:
                    state.component_id = "imageFill";
                    state.property_id = "pivot";
                    state.key_kind = 0;
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
                        resource.id.value == state.visual_component_id;
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
                        resource.id.value == state.visual_component_id;
                    if (ImGui::Selectable(resource.name.c_str(),
                                          selected_component))
                        state.visual_component_id = resource.id.value;
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
                                    state.component_id &&
                                descriptor->property_id ==
                                    state.property_id;
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
                                state.component_id =
                                    descriptor->component_id;
                                state.property_id =
                                    descriptor->property_id;
                                using Kind =
                                    fabric::project::PropertyValueKind;
                                if (descriptor->value_kind == Kind::vec2)
                                    state.key_kind = 0;
                                else if (descriptor->value_kind == Kind::color)
                                    state.key_kind = 2;
                                else if (descriptor->value_kind == Kind::boolean)
                                    state.key_kind = 3;
                                else if (descriptor->value_kind == Kind::resource)
                                    state.key_kind = 4;
                                else state.key_kind = 1;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            state.key_time = std::clamp(state.key_time, 0.0F,
                                                std::max(0.0F, clip.duration));
            ImGui::SliderFloat("Key time (seconds)", &state.key_time, 0.0F,
                               std::max(0.01F, clip.duration), "%.2f s");
            ImGui::SetItemTooltip("Time position at which the new key is inserted.");
            ImGui::Combo("Key type", &state.key_kind,
                         "Vec2\0Scalar\0Color\0Boolean\0Resource\0");
            bool auto_key_changed = false;
            if (state.key_kind == 0) {
                auto_key_changed = ImGui::InputFloat2("Vec2 value (property units)",
                                                      state.key_value);
                draw_technical_tooltip(
                    "Vector value written to the selected animatable property.");
            } else if (state.key_kind == 1) {
                auto_key_changed = ImGui::InputFloat("Scalar value (property units)",
                                                     &state.key_scalar);
                draw_technical_tooltip(
                    "Scalar value written to the selected animatable property.");
            } else if (state.key_kind == 2) {
                auto_key_changed = ImGui::ColorEdit4("Color value",
                                                     state.key_color);
            } else if (state.key_kind == 3) {
                auto_key_changed = ImGui::Checkbox("Boolean value",
                                                    &state.key_boolean);
            } else {
                const auto selected_resource = std::ranges::find_if(
                    session.resources(), [&](const auto& resource) {
                        return resource.id.value == state.key_resource_id;
                    });
                const char* resource_label =
                    selected_resource == session.resources().end()
                    ? (state.key_resource_id.empty()
                        ? "Choose a resource..." : "Missing resource")
                    : selected_resource->name.c_str();
                if (ImGui::BeginCombo("Resource value", resource_label)) {
                    for (const auto& resource : session.resources()) {
                        const bool selected =
                            resource.id.value == state.key_resource_id;
                        if (ImGui::Selectable(resource.name.c_str(), selected)) {
                            state.key_resource_id = resource.id.value;
                            auto_key_changed = true;
                        }
                        ImGui::SameLine();
                        const auto kind_label = std::string(
                            resource_kind_label(resource.kind));
                        ImGui::TextDisabled("%s · %s",
                                           kind_label.c_str(),
                                           resource.id.value.c_str());
                    }
                    ImGui::EndCombo();
                }
            }
            if (state.interpolation ==
                    fabric::project::AnimationInterpolation::cubic &&
                state.key_kind != 3 && state.key_kind != 4) {
                ImGui::Checkbox("Custom tangents", &state.tangents_enabled);
                if (state.tangents_enabled) {
                    if (state.key_kind == 0) {
                        ImGui::InputFloat2("In tangent (property units)",
                                           state.key_in_tangent);
                        draw_technical_tooltip(
                            "Incoming cubic tangent in the selected property units.");
                        ImGui::InputFloat2("Out tangent (property units)",
                                           state.key_out_tangent);
                        draw_technical_tooltip(
                            "Outgoing cubic tangent in the selected property units.");
                    } else if (state.key_kind == 1) {
                        ImGui::InputFloat("In tangent (property units)",
                                          &state.key_in_tangent_scalar);
                        draw_technical_tooltip(
                            "Incoming cubic tangent in the selected property units.");
                        ImGui::InputFloat("Out tangent (property units)",
                                          &state.key_out_tangent_scalar);
                        draw_technical_tooltip(
                            "Outgoing cubic tangent in the selected property units.");
                    } else {
                        ImGui::InputFloat4("In tangent (color channels)",
                                           state.key_in_tangent_color);
                        draw_technical_tooltip(
                            "Incoming cubic tangent for the color channels.");
                        ImGui::InputFloat4("Out tangent (color channels)",
                                           state.key_out_tangent_color);
                        draw_technical_tooltip(
                            "Outgoing cubic tangent for the color channels.");
                    }
                }
            }
            ImGui::SeparatorText("A → B segment");
            state.segment_start_time = std::clamp(
                state.segment_start_time, 0.0F,
                std::max(0.0F, clip.duration));
            state.segment_end_time = std::clamp(
                state.segment_end_time, 0.0F,
                std::max(0.0F, clip.duration));
            ImGui::InputFloat("A time (seconds)", &state.segment_start_time,
                              0.1F, 1.0F, "%.2f s");
            draw_technical_tooltip("Start time of the source segment.");
            ImGui::InputFloat("B time (seconds)", &state.segment_end_time,
                              0.1F, 1.0F, "%.2f s");
            draw_technical_tooltip("End time of the destination segment.");
            if (state.key_kind == 0) {
                ImGui::InputFloat2("A value (world units)", state.segment_start_value);
                ImGui::InputFloat2("B value (world units)", state.segment_end_value);
            } else if (state.key_kind == 1) {
                ImGui::InputFloat("A value (scalar)", &state.segment_start_scalar);
                ImGui::InputFloat("B value (scalar)", &state.segment_end_scalar);
            } else if (state.key_kind == 2) {
                ImGui::ColorEdit4("A value", state.segment_start_color);
                ImGui::ColorEdit4("B value", state.segment_end_color);
            } else if (state.key_kind == 3) {
                ImGui::Checkbox("A value", &state.segment_start_boolean);
                ImGui::Checkbox("B value", &state.segment_end_boolean);
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
                draw_segment_resource("A resource", state.segment_start_resource_id);
                draw_segment_resource("B resource", state.segment_end_resource_id);
            }
            ImGui::BeginDisabled(state.node_id.empty() ||
                                 state.component_id.empty() ||
                                 state.property_id.empty() ||
                                 state.segment_start_time >=
                                     state.segment_end_time ||
                                 (state.key_kind == 4 &&
                                  (state.segment_start_resource_id.empty() ||
                                   state.segment_end_resource_id.empty())));
            if (ImGui::Button("Create A → B keys")) {
                const auto segment_value = [&](const bool start) {
                    if (state.key_kind == 0)
                        return fabric::project::AnimationValue{
                            fabric::core::Vec2{
                                (start ? state.segment_start_value
                                       : state.segment_end_value)[0],
                                (start ? state.segment_start_value
                                       : state.segment_end_value)[1]}};
                    if (state.key_kind == 1)
                        return fabric::project::AnimationValue{
                            start ? state.segment_start_scalar
                                  : state.segment_end_scalar};
                    if (state.key_kind == 2)
                        return fabric::project::AnimationValue{
                            fabric::core::Color{
                                (start ? state.segment_start_color
                                       : state.segment_end_color)[0],
                                (start ? state.segment_start_color
                                       : state.segment_end_color)[1],
                                (start ? state.segment_start_color
                                       : state.segment_end_color)[2],
                                (start ? state.segment_start_color
                                       : state.segment_end_color)[3]}};
                    if (state.key_kind == 3)
                        return fabric::project::AnimationValue{
                            start ? state.segment_start_boolean
                                  : state.segment_end_boolean};
                    return fabric::project::AnimationValue{
                        fabric::project::ResourceReference{
                            {.value = start
                                ? state.segment_start_resource_id
                                : state.segment_end_resource_id},
                            "resource"}};
                };
                const bool created = session.set_selected_animation_segment(
                    {.node_id = state.node_id,
                     .component_id = state.component_id,
                     .property_id = state.property_id},
                    state.segment_start_time,
                    segment_value(true), state.segment_end_time,
                    segment_value(false), state.interpolation,
                    fabric::editor::AutosaveScheduler::Clock::now(),
                    state.composition, state.easing);
                status = created ? "Animation A → B segment created."
                                 : "Animation segment rejected; inspect diagnostics.";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(
                state.node_id.empty() ||
                    state.component_id.empty() ||
                    state.property_id.empty() ||
                    state.segment_start_time >= state.segment_end_time ||
                    (state.key_kind == 4 &&
                     (state.segment_start_resource_id.empty() ||
                      state.segment_end_resource_id.empty())),
                "Choose a target node, component, property, an increasing A/B time range and resource values when required.");
            const auto interpolation_label = std::string(
                fabric::project::to_string(state.interpolation));
            if (ImGui::BeginCombo("Interpolation", interpolation_label.c_str())) {
                for (const auto option : {
                         fabric::project::AnimationInterpolation::step,
                         fabric::project::AnimationInterpolation::linear,
                         fabric::project::AnimationInterpolation::cubic}) {
                    const bool selected_option = option == state.interpolation;
                    const auto label = std::string(fabric::project::to_string(option));
                    if (ImGui::Selectable(label.c_str(), selected_option)) {
                        state.interpolation = option;
                    }
                }
                ImGui::EndCombo();
            }
            const auto easing_label = std::string(
                fabric::project::to_string(state.easing));
            if (ImGui::BeginCombo("Easing", easing_label.c_str())) {
                for (const auto option : {
                         fabric::project::AnimationEasing::linear,
                         fabric::project::AnimationEasing::ease_in,
                         fabric::project::AnimationEasing::ease_out,
                         fabric::project::AnimationEasing::ease_in_out}) {
                    const bool selected_option = option == state.easing;
                    const auto label = std::string(
                        fabric::project::to_string(option));
                    if (ImGui::Selectable(label.c_str(), selected_option))
                        state.easing = option;
                }
                ImGui::EndCombo();
            }
            const auto composition_label = std::string(
                fabric::project::to_string(state.composition));
            if (ImGui::BeginCombo("Composition", composition_label.c_str())) {
                for (const auto option : {
                         fabric::project::AnimationComposition::replace,
                         fabric::project::AnimationComposition::additive}) {
                    const bool selected_option = option == state.composition;
                    const auto label = std::string(fabric::project::to_string(option));
                    if (ImGui::Selectable(label.c_str(), selected_option)) {
                        state.composition = option;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(state.node_id.empty() ||
                                 state.component_id.empty() ||
                                 state.property_id.empty() ||
                                  (state.key_kind == 4 &&
                                  state.key_resource_id.empty()));
            const auto set_key = [&]() {
                fabric::project::AnimationValue value;
                if (state.key_kind == 0) {
                    value = fabric::core::Vec2{state.key_value[0],
                                               state.key_value[1]};
                } else if (state.key_kind == 1) {
                    value = state.key_scalar;
                } else if (state.key_kind == 2) {
                    value = fabric::core::Color{state.key_color[0],
                                                state.key_color[1],
                                                state.key_color[2],
                                                state.key_color[3]};
                } else if (state.key_kind == 3) {
                    value = state.key_boolean;
                } else {
                    value = fabric::project::ResourceReference{
                        {.value = state.key_resource_id}, "resource"};
                }
                std::optional<fabric::project::AnimationValue> in_tangent;
                std::optional<fabric::project::AnimationValue> out_tangent;
                if (state.tangents_enabled && state.key_kind == 0) {
                    in_tangent = fabric::core::Vec2{state.key_in_tangent[0],
                                                   state.key_in_tangent[1]};
                    out_tangent = fabric::core::Vec2{state.key_out_tangent[0],
                                                    state.key_out_tangent[1]};
                } else if (state.tangents_enabled && state.key_kind == 1) {
                    in_tangent = state.key_in_tangent_scalar;
                    out_tangent = state.key_out_tangent_scalar;
                } else if (state.tangents_enabled && state.key_kind == 2) {
                    in_tangent = fabric::core::Color{state.key_in_tangent_color[0],
                                                     state.key_in_tangent_color[1],
                                                     state.key_in_tangent_color[2],
                                                     state.key_in_tangent_color[3]};
                    out_tangent = fabric::core::Color{state.key_out_tangent_color[0],
                                                      state.key_out_tangent_color[1],
                                                      state.key_out_tangent_color[2],
                                                      state.key_out_tangent_color[3]};
                }
                if (session.set_selected_animation_key(
                        {.node_id = state.node_id,
                         .component_id = state.component_id,
                         .property_id = state.property_id},
                        state.auto_key
                            ? state.scrub_time
                            : state.key_time,
                        std::move(value),
                        state.interpolation,
                        fabric::editor::AutosaveScheduler::Clock::now(),
                        state.composition, state.easing,
                        std::move(in_tangent), std::move(out_tangent))) {
                    status = "Animation key set.";
                } else {
                    status = "Animation key rejected; inspect diagnostics.";
                }
            };
            if (ImGui::Button("Set key") ||
                (state.auto_key && auto_key_changed)) {
                set_key();
            }
            ImGui::EndDisabled();
            draw_disabled_reason(state.node_id.empty() ||
                                     state.component_id.empty() ||
                                     state.property_id.empty() ||
                                     (state.key_kind == 4 &&
                                      state.key_resource_id.empty()),
                                 "Choose a target node, component, property and resource value when required.");
            ImGui::SeparatorText("Tracks");
            if (ImGui::Button("Select all keys")) {
                state.selected_keys.clear();
                for (const auto& track : clip.tracks)
                    for (std::size_t key_index = 0;
                         key_index < track.keys.size(); ++key_index)
                        state.selected_keys.push_back(
                            {track.binding, key_index});
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear key selection"))
                state.selected_keys.clear();
            ImGui::SameLine();
            ImGui::BeginDisabled(state.selected_keys.empty());
            if (ImGui::Button("Copy selected keys")) {
                state.key_clipboard.clear();
                for (const auto& selected : state.selected_keys) {
                    const auto track = std::ranges::find(
                        clip.tracks, selected.binding,
                        &fabric::project::AnimationTrack::binding);
                    if (track == clip.tracks.end() ||
                        selected.index >= track->keys.size()) continue;
                    state.key_clipboard.push_back({
                        selected.binding, track->keys[selected.index],
                        track->interpolation, track->composition, track->easing});
                }
                status = state.key_clipboard.empty()
                    ? "No valid animation keys selected."
                    : "Animation keys copied.";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(state.selected_keys.empty(),
                                 "Select at least one valid animation key to copy.");
            ImGui::SameLine();
            ImGui::BeginDisabled(state.key_clipboard.empty());
            if (ImGui::Button("Paste at key time")) {
                const auto first = std::ranges::min_element(
                    state.key_clipboard, {},
                    [](const auto& entry) { return entry.key.time; });
                const auto first_time = first->key.time;
                bool pasted = false;
                for (const auto& entry : state.key_clipboard) {
                    const auto time = snap_key_time(
                        state.key_time + entry.key.time - first_time);
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
            draw_disabled_reason(state.key_clipboard.empty(),
                                 "Copy animation keys before pasting them.");
            if (!state.key_clipboard.empty())
                ImGui::SameLine(), ImGui::TextDisabled(
                    "%zu copied", state.key_clipboard.size());
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
                        state.selected_keys, [&](const auto& candidate) {
                            return candidate.binding == track.binding &&
                                   candidate.index == key_index;
                        });
                    if (ImGui::Checkbox("##selected", &selected)) {
                        const auto found = std::ranges::find_if(
                            state.selected_keys,
                            [&](const auto& candidate) {
                                return candidate.binding == track.binding &&
                                       candidate.index == key_index;
                            });
                        if (selected && found == state.selected_keys.end())
                            state.selected_keys.push_back(
                                {track.binding, key_index});
                        else if (!selected &&
                                 found != state.selected_keys.end())
                            state.selected_keys.erase(found);
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
                            state.selected_keys.clear();
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
}

} // namespace fabric::asset_studio
