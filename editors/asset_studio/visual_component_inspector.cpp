#include "visual_component_inspector.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdint>
#include <ranges>
#include <utility>
#include <variant>

namespace fabric::asset_studio {

void draw_visual_component_inspector(
    editor::ProjectSession& session,
    VisualComponentInspectorState& state,
    std::string& status,
    const VisualComponentResourcePicker resource_picker) {
    const auto selected = session.selected_visual_component();
    if (!selected) return;
    if (state.document_id != selected->document.id.value) {
        state.document_id = selected->document.id.value;
        state.selected_anchor_id = selected->anchors.empty()
            ? std::string{} : selected->anchors.front().id;
        state.selected_parameter_id = selected->parameters.empty()
            ? std::string{} : selected->parameters.front().id;
    }
    if (!selected->anchors.empty() && !std::ranges::any_of(
            selected->anchors, [&](const auto& anchor) {
                return anchor.id == state.selected_anchor_id;
            })) {
        state.selected_anchor_id = selected->anchors.front().id;
    }
    if (!selected->parameters.empty() && !std::ranges::any_of(
            selected->parameters, [&](const auto& parameter) {
                return parameter.id == state.selected_parameter_id;
            })) {
        state.selected_parameter_id = selected->parameters.front().id;
    }

    const auto commit = [&](project::VisualComponent candidate,
                            const char* success) {
        const bool changed =
            session.set_selected_visual_component(std::move(candidate));
        status = changed
            ? success
            : "Visual component change rejected; inspect diagnostics.";
        return changed;
    };
    const bool is_beam_component = std::ranges::any_of(
        selected->parameters, [](const auto& parameter) {
            return parameter.target.node_id == "beam" &&
                parameter.target.component_id == "shader";
        });
    if (!is_beam_component ||
        ImGui::CollapsingHeader("Advanced component structure")) {
        ImGui::SeparatorText("Anchors");
        for (std::size_t index = 0; index < selected->anchors.size(); ++index) {
            const auto& anchor = selected->anchors[index];
            if (ImGui::Selectable(anchor.name.c_str(),
                                  state.selected_anchor_id == anchor.id)) {
                state.selected_anchor_id = anchor.id;
            }
        }
        const auto selected_anchor = std::ranges::find(
            selected->anchors, state.selected_anchor_id,
            &project::VisualComponentAnchor::id);
        if (selected_anchor != selected->anchors.end()) {
            const auto anchor_index = static_cast<std::size_t>(
                std::ranges::distance(selected->anchors.begin(),
                                      selected_anchor));
            if (ImGui::Button("Duplicate anchor")) {
                auto candidate = *selected;
                auto copy = candidate.anchors[anchor_index];
                const auto base = copy.id + "-copy";
                copy.id = base;
                std::size_t suffix = 2U;
                while (std::ranges::any_of(
                    candidate.anchors, [&](const auto& anchor) {
                        return anchor.id == copy.id;
                    })) {
                    copy.id = base + "-" + std::to_string(suffix++);
                }
                copy.name += " copy";
                const auto copy_id = copy.id;
                candidate.anchors.push_back(std::move(copy));
                const bool duplicated = commit(
                    std::move(candidate), "Visual anchor duplicated.");
                if (duplicated) state.selected_anchor_id = copy_id;
            }
            const auto current = session.selected_visual_component();
            if (current) {
                const auto anchor = std::ranges::find(
                    current->anchors, state.selected_anchor_id,
                    &project::VisualComponentAnchor::id);
                if (anchor == current->anchors.end()) return;
                const auto current_anchor_index = static_cast<std::size_t>(
                    std::ranges::distance(current->anchors.begin(), anchor));
                auto edited_anchor = *anchor;
                if (ImGui::DragFloat2("Anchor position (world units)",
                                      &edited_anchor.position.x, 0.05F)) {
                    auto candidate = *current;
                    candidate.anchors[current_anchor_index] =
                        std::move(edited_anchor);
                    commit(std::move(candidate), "Visual anchor moved.");
                }
                ImGui::SetItemTooltip(
                    "Position of the visual component anchor in project world units.");
            }
        }
    }

    const auto current = session.selected_visual_component();
    if (!current) return;
    ImGui::SeparatorText(is_beam_component ? "Beam appearance" : "Parameters");
    if (is_beam_component) {
        for (std::size_t index = 0; index < current->parameters.size(); ++index) {
            auto parameter = current->parameters[index];
            bool changed = false;
            ImGui::PushID(static_cast<int>(index));
            if (auto* reference = std::get_if<project::ResourceReference>(
                    &parameter.default_value)) {
                std::string texture_id = reference->id.value;
                if (resource_picker(
                        parameter.name.c_str(), session.resources(),
                        editor::StudioResourceKind::texture,
                        texture_id, false, true)) {
                    *reference = {{.value = std::move(texture_id)}, "texture"};
                    changed = true;
                }
            } else if (auto* color = std::get_if<core::Color>(
                           &parameter.default_value)) {
                changed = ImGui::ColorEdit4(parameter.name.c_str(), &color->red);
            } else if (auto* value = std::get_if<std::string>(
                           &parameter.default_value)) {
                if (parameter.id == "color-mode") {
                    const char* label = *value == "preserve"
                        ? "Source intacte" : "Recoloration";
                    if (ImGui::BeginCombo("Traitement des couleurs", label)) {
                        if (ImGui::Selectable("Recoloration",
                                              *value == "recolor")) {
                            *value = "recolor";
                            changed = true;
                        }
                        if (ImGui::Selectable("Source intacte",
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
                auto candidate = *session.selected_visual_component();
                candidate.parameters[index] = std::move(parameter);
                commit(std::move(candidate), "Visual parameter changed.");
            }
            ImGui::PopID();
        }
        ImGui::TextDisabled("Orientation follows the path from start to end.");
    } else {
        for (std::size_t index = 0; index < current->parameters.size(); ++index) {
            const auto& parameter = current->parameters[index];
            if (ImGui::Selectable(parameter.name.c_str(),
                                  state.selected_parameter_id == parameter.id)) {
                state.selected_parameter_id = parameter.id;
            }
        }
        const auto selected_parameter = std::ranges::find(
            current->parameters, state.selected_parameter_id,
            &project::VisualComponentParameter::id);
        if (selected_parameter != current->parameters.end()) {
            const auto parameter_index = static_cast<std::size_t>(
                std::ranges::distance(current->parameters.begin(),
                                      selected_parameter));
            auto parameter = *selected_parameter;
            bool changed = ImGui::Checkbox("Animatable", &parameter.animatable);
            ImGui::TextDisabled("Target %s.%s.%s",
                parameter.target.node_id.c_str(),
                parameter.target.component_id.c_str(),
                parameter.target.property_id.c_str());
            if (auto* value = std::get_if<float>(&parameter.default_value)) {
                changed |= ImGui::DragFloat("Default", value, 0.05F);
                ImGui::SetItemTooltip(
                    "Default value used when this component parameter is not overridden; its unit follows the parameter schema.");
            } else if (auto* value = std::get_if<std::int64_t>(
                           &parameter.default_value)) {
                changed |= ImGui::InputScalar("Default", ImGuiDataType_S64, value);
                ImGui::SetItemTooltip(
                    "Default integer used when this component parameter is not overridden; its unit follows the parameter schema.");
            } else if (auto* value = std::get_if<bool>(
                           &parameter.default_value)) {
                changed |= ImGui::Checkbox("Default", value);
            } else if (auto* value = std::get_if<std::string>(
                           &parameter.default_value)) {
                changed |= ImGui::InputText("Default", value);
            } else if (auto* value = std::get_if<core::Vec2>(
                           &parameter.default_value)) {
                changed |= ImGui::DragFloat2("Default", &value->x, 0.05F);
                ImGui::SetItemTooltip(
                    "Default vector used when this component parameter is not overridden; its unit follows the parameter schema.");
            } else if (auto* value = std::get_if<core::Color>(
                           &parameter.default_value)) {
                changed |= ImGui::ColorEdit4("Default", &value->red);
            } else if (const auto* value =
                           std::get_if<project::ResourceReference>(
                               &parameter.default_value)) {
                ImGui::TextDisabled("Default %s (%s)",
                    value->id.value.c_str(), value->expected_type.c_str());
            }
            if (changed) {
                auto candidate = *session.selected_visual_component();
                candidate.parameters[parameter_index] =
                    std::move(parameter);
                commit(std::move(candidate), "Visual parameter changed.");
            }
        }
    }
    if (const auto updated = session.selected_visual_component()) {
        ImGui::TextDisabled("%zu variant(s)", updated->variants.size());
    }
}

} // namespace fabric::asset_studio
