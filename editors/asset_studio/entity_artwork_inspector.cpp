#include "entity_artwork_inspector.hpp"

#include "editor_widgets.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/visual_component.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdint>
#include <ranges>
#include <utility>

namespace fabric::asset_studio {
namespace {

using Kind = project::EntityDrawableKind;

editor::StudioResourceKind resource_kind(const Kind kind) {
    if (kind == Kind::texture) return editor::StudioResourceKind::texture;
    if (kind == Kind::vector) return editor::StudioResourceKind::vector;
    return editor::StudioResourceKind::visual_component;
}

const char* expected_type(const Kind kind) {
    if (kind == Kind::texture) return "texture";
    if (kind == Kind::vector) return "vector";
    return "visualComponent";
}

void record_item(const std::function<void(float, float)>& record) {
    if (!record) return;
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    record((minimum.x + maximum.x) * 0.5F,
           (minimum.y + maximum.y) * 0.5F);
}

} // namespace

void draw_entity_artwork_inspector(
    editor::ProjectSession& session, const std::size_t node_index,
    project::EntityNode& node, const bool advanced_mode,
    EntityArtworkInspectorState& state, std::string& status,
    const EntityArtworkResourcePicker resource_picker,
    const EntityArtworkKindLabel resource_kind_label,
    const EntityArtworkContractKind contract_kind,
    const EntityArtworkSurfaceEditor surface_editor,
    const EntityArtworkOpenResource& open_resource,
    const EntityArtworkInspectorProbe* probe) {
    const auto entity = session.selected_entity();
    if (!entity || node_index >= entity->nodes.size()) return;
    const auto commit = [&](const project::EntityNode& changed) {
        status = session.set_selected_entity_node(node_index, changed)
            ? "Entity node changed."
            : "Entity change rejected; inspect diagnostics.";
    };
    const auto apply_kind = [&](project::EntityNode& changed, const Kind kind) {
        changed.drawable.kind = kind;
        if (kind == Kind::none) {
            changed.drawable.resource.reset();
            changed.drawable.material.reset();
            changed.drawable.component_instance.reset();
            return true;
        }
        const auto wanted = resource_kind(kind);
        const auto first = std::ranges::find_if(
            session.resources(), [&](const auto& resource) {
                return resource.kind == wanted;
            });
        if (first == session.resources().end()) return false;
        const bool reference_exists = changed.drawable.resource &&
            std::ranges::any_of(session.resources(), [&](const auto& resource) {
                return resource.kind == wanted &&
                    resource.id == changed.drawable.resource->id;
            });
        if (!changed.drawable.resource ||
            changed.drawable.resource->expected_type != expected_type(kind) ||
            !reference_exists) {
            changed.drawable.resource =
                project::ResourceReference{first->id, expected_type(kind)};
        }
        if (kind == Kind::visual_component) {
            changed.drawable.material.reset();
            if (!changed.drawable.component_instance)
                changed.drawable.component_instance =
                    project::VisualComponentInstance{};
        } else {
            changed.drawable.component_instance.reset();
        }
        return true;
    };

    ImGui::BeginDisabled(node.locked);
    if (node.drawable.material && ImGui::CollapsingHeader("Appearance effects")) {
        const auto loaded = project::load_material(
            session.project_root(), *session.manifest(),
            project::material_document_path(*session.manifest(),
                                            node.drawable.material->id));
        if (loaded.ok() && loaded.asset->shader) {
            auto appearance = *loaded.asset;
            bool changed = surface_editor(
                *appearance.shader, "entity-material-effects", false);
            changed |= ImGui::SliderFloat(
                "Appearance opacity", &appearance.shader->opacity, 0.0F, 1.0F);
            changed |= ImGui::SliderFloat(
                "Appearance intensity", &appearance.shader->intensity, 0.0F,
                4.0F);
            if (changed && !session.set_referenced_material(
                               node.drawable.material->id,
                               std::move(appearance))) {
                status = "Appearance change rejected; inspect diagnostics.";
            }
        }
    }

    ImGui::SeparatorText("Artwork");
    if (node.drawable.kind != Kind::none && node.drawable.resource) {
        const auto wanted = resource_kind(node.drawable.kind);
        const bool reference_exists = std::ranges::any_of(
            session.resources(), [&](const auto& resource) {
                return resource.kind == wanted &&
                    resource.id == node.drawable.resource->id;
            });
        if (!reference_exists) {
            ImGui::TextColored(
                {0.95F, 0.65F, 0.25F, 1.0F}, "Missing %s: %s",
                resource_kind_label(wanted).data(),
                node.drawable.resource->id.value.c_str());
            if (ImGui::Button("Repair with first compatible resource")) {
                if (apply_kind(node, node.drawable.kind)) commit(node);
                else status = "No compatible resource is available for repair.";
            }
        }
    }

    const auto drawable_label = std::string(project::to_string(node.drawable.kind));
    if (ImGui::BeginCombo("Drawable type", drawable_label.c_str())) {
        for (const auto kind : {Kind::none, Kind::texture, Kind::vector,
                               Kind::visual_component}) {
            const auto label = std::string(project::to_string(kind));
            const auto first = kind == Kind::none ? session.resources().end()
                : std::ranges::find_if(session.resources(), [&](const auto& resource) {
                      return resource.kind == resource_kind(kind);
                  });
            const bool available =
                kind == Kind::none || first != session.resources().end();
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(label.c_str(), node.drawable.kind == kind)) {
                const bool has_overrides = node.drawable.component_instance &&
                    !node.drawable.component_instance->overrides.empty();
                if (has_overrides && kind != Kind::visual_component) {
                    state.pending_drawable_kind = std::pair{node_index, kind};
                    ImGui::OpenPopup("Discard incompatible overrides?");
                } else if (!apply_kind(node, kind)) {
                    status = "Drawable kind unavailable; inspect diagnostics.";
                } else {
                    commit(node);
                }
            }
            if (probe && probe->enabled && kind == Kind::texture)
                record_item(probe->record_texture);
            ImGui::EndDisabled();
            editor_ui::draw_disabled_reason(
                !available, "Add an indexed resource of this drawable kind first.");
        }
        ImGui::EndCombo();
    }
    if (probe && probe->enabled) record_item(probe->record_kind);

    if (state.force_discard_modal) {
        ImGui::OpenPopup("Discard incompatible overrides?");
        state.force_discard_modal = false;
    }
    if (ImGui::BeginPopupModal("Discard incompatible overrides?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool valid = state.pending_drawable_kind &&
            state.pending_drawable_kind->first < entity->nodes.size();
        std::size_t override_count = 0U;
        if (valid) {
            const auto& pending = entity->nodes[state.pending_drawable_kind->first];
            if (pending.drawable.component_instance)
                override_count = pending.drawable.component_instance->overrides.size();
            ImGui::Text("Change drawable kind and discard %zu override(s)?",
                        override_count);
            ImGui::TextDisabled(
                "Overrides belong to the current visual component and cannot be applied to the new kind.");
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Discard overrides and change")) {
            auto changed = entity->nodes[state.pending_drawable_kind->first];
            if (apply_kind(changed, state.pending_drawable_kind->second) &&
                session.set_selected_entity_node(
                    state.pending_drawable_kind->first, std::move(changed))) {
                status = "Drawable changed; incompatible overrides discarded.";
                state.pending_drawable_kind.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        if (probe && probe->enabled) record_item(probe->record_confirm);
        ImGui::EndDisabled();
        editor_ui::draw_disabled_reason(
            !valid, "Select a valid drawable kind before discarding overrides.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.pending_drawable_kind.reset();
            ImGui::CloseCurrentPopup();
        }
        if (probe && probe->enabled) {
            record_item(probe->record_cancel);
            if (probe->record_modal) probe->record_modal();
        }
        ImGui::EndPopup();
    }

    if (node.drawable.kind != Kind::none) {
        const auto wanted = resource_kind(node.drawable.kind);
        std::string artwork_id = node.drawable.resource
            ? node.drawable.resource->id.value : std::string{};
        if (resource_picker("Artwork", session.resources(), wanted, artwork_id,
                            false, true)) {
            node.drawable.resource = project::ResourceReference{
                {.value = artwork_id}, expected_type(node.drawable.kind)};
            if (node.drawable.kind == Kind::visual_component)
                node.drawable.component_instance =
                    project::VisualComponentInstance{};
            commit(node);
        }
        const auto artwork = std::ranges::find_if(
            session.resources(), [&](const auto& resource) {
                return resource.kind == wanted &&
                    resource.id.value == artwork_id;
            });
        ImGui::BeginDisabled(artwork == session.resources().end());
        if (ImGui::Button("Open artwork") &&
            artwork != session.resources().end()) open_resource(*artwork);
        ImGui::EndDisabled();
        editor_ui::draw_disabled_reason(
            artwork == session.resources().end(),
            "Choose an existing artwork resource first.");
        ImGui::SameLine();
        if (ImGui::Button("Clear drawable")) {
            node.drawable = {};
            commit(node);
        }
    }

    if (node.drawable.kind == Kind::texture || node.drawable.kind == Kind::vector) {
        std::string material_id = node.drawable.material
            ? node.drawable.material->id.value : std::string{};
        if (resource_picker("Material", session.resources(),
                            editor::StudioResourceKind::material, material_id,
                            true, true)) {
            node.drawable.material = material_id.empty()
                ? std::optional<project::ResourceReference>{}
                : std::optional<project::ResourceReference>{
                      project::ResourceReference{{.value = material_id},
                                                 "material"}};
            commit(node);
        }
    }

    if (advanced_mode && node.drawable.kind == Kind::visual_component &&
        node.drawable.resource) {
        const auto component = project::load_visual_component(
            session.project_root(), *session.manifest(),
            project::visual_component_document_path(
                *session.manifest(), node.drawable.resource->id));
        if (component.ok()) {
            auto instance = node.drawable.component_instance.value_or(
                project::VisualComponentInstance{});
            const auto variant_name = [&] {
                if (!instance.variant_id) return std::string{"Default"};
                const auto found = std::ranges::find(
                    component.asset->variants, *instance.variant_id,
                    &project::VisualComponentVariant::id);
                return found == component.asset->variants.end()
                    ? std::string{"Missing: "} + *instance.variant_id
                    : found->name;
            }();
            if (ImGui::BeginCombo("Variant", variant_name.c_str())) {
                if (ImGui::Selectable("Default", !instance.variant_id)) {
                    instance.variant_id.reset();
                    node.drawable.component_instance = instance;
                    commit(node);
                }
                for (const auto& variant : component.asset->variants) {
                    if (ImGui::Selectable(variant.name.c_str(),
                                          instance.variant_id == variant.id)) {
                        instance.variant_id = variant.id;
                        node.drawable.component_instance = instance;
                        commit(node);
                    }
                }
                ImGui::EndCombo();
            }
            const auto anchor_name = [&] {
                if (!instance.anchor_id) return std::string{"Default"};
                const auto found = std::ranges::find(
                    component.asset->anchors, *instance.anchor_id,
                    &project::VisualComponentAnchor::id);
                return found == component.asset->anchors.end()
                    ? std::string{"Missing: "} + *instance.anchor_id
                    : found->name;
            }();
            if (ImGui::BeginCombo("Anchor", anchor_name.c_str())) {
                if (ImGui::Selectable("Default", !instance.anchor_id)) {
                    instance.anchor_id.reset();
                    node.drawable.component_instance = instance;
                    commit(node);
                }
                for (const auto& anchor : component.asset->anchors) {
                    if (ImGui::Selectable(anchor.name.c_str(),
                                          instance.anchor_id == anchor.id)) {
                        instance.anchor_id = anchor.id;
                        node.drawable.component_instance = instance;
                        commit(node);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Text("%zu override(s)", instance.overrides.size());
            for (std::size_t index = 0; index < instance.overrides.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextUnformatted(instance.overrides[index].parameter_id.c_str());
                auto override = instance.overrides[index];
                bool changed = false;
                if (auto* value = std::get_if<float>(&override.value))
                    changed = ImGui::InputFloat("Value", value);
                else if (auto* value = std::get_if<std::int64_t>(&override.value))
                    changed = ImGui::InputScalar("Value", ImGuiDataType_S64, value);
                else if (auto* value = std::get_if<bool>(&override.value))
                    changed = ImGui::Checkbox("Value", value);
                else if (auto* value = std::get_if<std::string>(&override.value))
                    changed = ImGui::InputText("Value", value);
                else if (auto* value = std::get_if<core::Vec2>(&override.value))
                    changed = ImGui::InputFloat2("Value", &value->x);
                else if (auto* value = std::get_if<core::Color>(&override.value))
                    changed = ImGui::ColorEdit4("Value", &value->red);
                else if (auto* value =
                             std::get_if<project::ResourceReference>(&override.value)) {
                    if (const auto kind = contract_kind(value->expected_type)) {
                        auto id = value->id.value;
                        if (resource_picker("Value", session.resources(), *kind,
                                            id, false, true)) {
                            value->id = {.value = id};
                            changed = true;
                        }
                    } else {
                        ImGui::TextDisabled("Unsupported resource contract: %s",
                                            value->expected_type.c_str());
                    }
                }
                if (changed) {
                    instance.overrides[index] = std::move(override);
                    node.drawable.component_instance = instance;
                    commit(node);
                }
                if (ImGui::SmallButton("Remove override")) {
                    instance.overrides.erase(instance.overrides.begin() +
                                             static_cast<std::ptrdiff_t>(index));
                    node.drawable.component_instance = instance;
                    commit(node);
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
                        commit(node);
                    }
                    ImGui::EndDisabled();
                    editor_ui::draw_disabled_reason(
                        exists,
                        "This component parameter already has an override.");
                }
                ImGui::EndCombo();
            }
        }
    }
    ImGui::EndDisabled();
    editor_ui::draw_disabled_reason(
        node.locked, "Unlock the node to edit its artwork.");
}

} // namespace fabric::asset_studio
