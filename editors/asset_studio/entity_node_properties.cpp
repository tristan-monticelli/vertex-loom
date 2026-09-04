#include "entity_node_properties.hpp"

#include "editor_widgets.hpp"

#include <imgui.h>

#include <optional>
#include <utility>

namespace fabric::asset_studio {

void draw_entity_node_properties(
    editor::ProjectSession& session, const std::size_t node_index,
    project::EntityNode& node, const bool advanced_mode, std::string& status,
    const EntityNodePropertiesProbe* probe) {
    const auto entity = session.selected_entity();
    if (!entity || node_index >= entity->nodes.size()) return;
    const auto commit = [&](const project::EntityNode& changed) {
        if (session.set_selected_entity_node(node_index, changed)) {
            status = "Entity node changed.";
        } else {
            status = "Entity change rejected; inspect diagnostics.";
        }
    };

    bool locked = node.locked;
    if (ImGui::Checkbox("Locked", &locked)) {
        node.locked = locked;
        commit(node);
    }
    ImGui::BeginDisabled(node.locked);
    bool visible = node.visible;
    if (ImGui::Checkbox("Visible", &visible)) {
        node.visible = visible;
        commit(node);
    }
    std::string name = node.name;
    if (editor_ui::draw_resource_name_field("Node name", name, 360.0F)) {
        node.name = std::move(name);
        commit(node);
    }

    ImGui::SeparatorText("Transform");
    float position[]{node.transform.position.x, node.transform.position.y};
    if (ImGui::InputFloat2("Position", position)) {
        node.transform.position = {position[0], position[1]};
        commit(node);
    }
    if (probe != nullptr && probe->enabled && probe->record_transform)
        probe->record_transform();
    editor_ui::draw_technical_tooltip(
        "Entity node translation in project world units.");
    float rotation = node.transform.rotation_degrees;
    if (ImGui::InputFloat("Rotation", &rotation, 1.0F, 10.0F,
                          "%.2f deg")) {
        node.transform.rotation_degrees = rotation;
        commit(node);
    }
    editor_ui::draw_technical_tooltip(
        "Entity node rotation around its pivot, in degrees.");
    float scale[]{node.transform.scale.x, node.transform.scale.y};
    if (ImGui::InputFloat2("Scale", scale)) {
        node.transform.scale = {scale[0], scale[1]};
        commit(node);
    }
    editor_ui::draw_technical_tooltip("Entity node scale multiplier.");

    if (advanced_mode) {
        ImGui::SeparatorText("Advanced node settings");
        const auto parent_label = [&](const std::optional<std::string>& parent) {
            if (!parent) return std::string{"None"};
            for (const auto& candidate : entity->nodes)
                if (candidate.id == *parent) return candidate.name;
            return std::string{"Missing: "} + *parent;
        };
        if (ImGui::BeginCombo("Parent", parent_label(node.parent).c_str())) {
            if (ImGui::Selectable("None", !node.parent.has_value())) {
                node.parent.reset();
                commit(node);
            }
            for (const auto& candidate : entity->nodes) {
                if (candidate.id == node.id) continue;
                const bool selected =
                    node.parent && *node.parent == candidate.id;
                if (ImGui::Selectable(candidate.name.c_str(), selected)) {
                    node.parent = candidate.id;
                    commit(node);
                }
            }
            ImGui::EndCombo();
        }
        float pivot[]{node.transform.pivot.x, node.transform.pivot.y};
        if (ImGui::InputFloat2("Entity pivot (world units)", pivot)) {
            node.transform.pivot = {pivot[0], pivot[1]};
            commit(node);
        }
        editor_ui::draw_technical_tooltip(
            "Entity node pivot in project world units.");
        float z_order = node.z_order;
        if (ImGui::InputFloat("Z order (world units)", &z_order, 0.1F, 1.0F,
                              "%.2f")) {
            node.z_order = z_order;
            commit(node);
        }
        editor_ui::draw_technical_tooltip(
            "Draw order; larger values render later.");
    }
    ImGui::EndDisabled();
}

} // namespace fabric::asset_studio
