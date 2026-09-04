#include "entity_hierarchy_workspace.hpp"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <utility>

namespace fabric::asset_studio {
namespace {

using DrawablePayload =
    std::pair<project::EntityDrawableKind, const char*>;

std::optional<DrawablePayload> drawable_from_payload(
    const ResourceDragPayload& payload) {
    switch (static_cast<editor::StudioResourceKind>(payload.kind)) {
    case editor::StudioResourceKind::texture:
        return DrawablePayload{project::EntityDrawableKind::texture, "texture"};
    case editor::StudioResourceKind::vector:
        return DrawablePayload{project::EntityDrawableKind::vector, "vector"};
    case editor::StudioResourceKind::visual_component:
        return DrawablePayload{project::EntityDrawableKind::visual_component,
                               "visualComponent"};
    default: return std::nullopt;
    }
}

void record_target(const EntityHierarchyProbe* probe) {
    if (probe == nullptr || !probe->record_target) return;
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    probe->record_target((minimum.x + maximum.x) * 0.5F,
                         (minimum.y + maximum.y) * 0.5F);
}

void record_applied(const EntityHierarchyProbe* probe) {
    if (probe != nullptr && probe->record_applied) probe->record_applied();
}

} // namespace

void draw_entity_hierarchy_workspace(
    editor::ProjectSession& session, CanvasUiState& canvas,
    const bool advanced_mode, std::string& status,
    const EntityHierarchyProbe* probe) {
    if (!session.selected_entity()) return;
    const auto& entity = *session.selected_entity();
    const auto apply_resource_to_node =
        [&](const std::size_t node_index, const ResourceDragPayload& payload) {
            if (node_index >= session.selected_entity()->nodes.size()) return false;
            const auto drawable = drawable_from_payload(payload);
            if (!drawable) return false;
            auto changed = session.selected_entity()->nodes[node_index];
            if (changed.drawable.component_instance &&
                !changed.drawable.component_instance->overrides.empty())
                return false;
            changed.drawable.kind = drawable->first;
            changed.drawable.resource = project::ResourceReference{
                {.value = payload.id}, drawable->second};
            changed.drawable.material.reset();
            changed.drawable.component_instance.reset();
            if (drawable->first == project::EntityDrawableKind::visual_component)
                changed.drawable.component_instance =
                    project::VisualComponentInstance{};
            return session.set_selected_entity_node(node_index,
                                                    std::move(changed));
        };
    const auto add_dropped_node =
        [&](const std::optional<std::string>& parent,
            const ResourceDragPayload& payload) {
            const auto drawable = drawable_from_payload(payload);
            if (!drawable) return false;
            project::EntityNode added{
                .id = "node-" + std::to_string(entity.nodes.size() + 1U),
                .name = "Node " + std::to_string(entity.nodes.size() + 1U),
                .parent = parent};
            while (std::ranges::any_of(entity.nodes, [&](const auto& candidate) {
                return candidate.id == added.id;
            }))
                added.id += "-copy";
            added.drawable.kind = drawable->first;
            added.drawable.resource = project::ResourceReference{
                {.value = payload.id}, drawable->second};
            if (drawable->first == project::EntityDrawableKind::visual_component)
                added.drawable.component_instance =
                    project::VisualComponentInstance{};
            const bool added_ok =
                session.add_selected_entity_node(std::move(added));
            if (added_ok) {
                canvas.selected_node = entity.nodes.size() - 1U;
                canvas.selected_entity_nodes = {canvas.selected_node};
            }
            return added_ok;
        };

    ImGui::SeparatorText("Entity hierarchy");
    if (!entity.nodes.empty())
        canvas.selected_node =
            std::min(canvas.selected_node, entity.nodes.size() - 1U);
    if (entity.nodes.empty()) {
        if (ImGui::Button("Add root node")) {
            project::EntityNode node{.id = "node-1", .name = "Node 1"};
            if (session.add_selected_entity_node(std::move(node))) {
                canvas.selected_node = 0U;
                canvas.selected_entity_nodes = {0U};
                status = "Entity root node added.";
            } else {
                status = "Entity node rejected; inspect diagnostics.";
            }
        }
        ImGui::SameLine();
        ImGui::Button("Drop artwork as root");
        if (probe != nullptr && probe->enabled && probe->target_mode == 1)
            record_target(probe);
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload(
                    "VERTEX_LOOM_RESOURCE");
                payload && payload->DataSize == sizeof(ResourceDragPayload)) {
                if (add_dropped_node(
                        std::nullopt,
                        *static_cast<const ResourceDragPayload*>(payload->Data))) {
                    if (probe != nullptr && probe->enabled &&
                        probe->target_mode == 1)
                        record_applied(probe);
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
        return;
    }

    if (ImGui::Button("Add child")) {
        const auto& parent = entity.nodes[canvas.selected_node];
        project::EntityNode node{
            .id = "node-" + std::to_string(entity.nodes.size() + 1U),
            .name = "Node " + std::to_string(entity.nodes.size() + 1U),
            .parent = parent.id};
        while (std::ranges::any_of(entity.nodes, [&](const auto& candidate) {
            return candidate.id == node.id;
        }))
            node.id += "-copy";
        if (session.add_selected_entity_node(std::move(node))) {
            canvas.selected_node = entity.nodes.size() - 1U;
            canvas.selected_entity_nodes = {canvas.selected_node};
            status = "Entity child added.";
        } else {
            status = "Entity node rejected; inspect diagnostics.";
        }
    }
    ImGui::Button("Drop artwork as child", {-1.0F, 0.0F});
    if (probe != nullptr && probe->enabled && probe->target_mode == 2)
        record_target(probe);
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(
                "VERTEX_LOOM_RESOURCE");
            payload && payload->DataSize == sizeof(ResourceDragPayload)) {
            if (add_dropped_node(
                    entity.nodes[canvas.selected_node].id,
                    *static_cast<const ResourceDragPayload*>(payload->Data))) {
                if (probe != nullptr && probe->enabled &&
                    probe->target_mode == 2)
                    record_applied(probe);
                status = "Artwork dropped on new child node.";
            } else {
                status = "Artwork kind cannot be used on an entity node.";
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::TextDisabled("Textures, vectors or visual components");

    bool request_delete = false;
    if (ImGui::Button("Node actions...")) ImGui::OpenPopup("Node actions");
    if (ImGui::BeginPopup("Node actions")) {
        if (ImGui::MenuItem("Duplicate")) {
            if (session.duplicate_selected_entity_node(canvas.selected_node)) {
                canvas.selected_node = entity.nodes.size() - 1U;
                canvas.selected_entity_nodes = {canvas.selected_node};
                status = "Entity node duplicated.";
            } else {
                status = "Entity node rejected; inspect diagnostics.";
            }
        }
        if (ImGui::MenuItem("Move up", nullptr, false,
                            canvas.selected_node > 0U) &&
            session.move_selected_entity_node(canvas.selected_node,
                                              canvas.selected_node - 1U)) {
            --canvas.selected_node;
            canvas.selected_entity_nodes = {canvas.selected_node};
            status = "Entity node moved.";
        }
        if (ImGui::MenuItem("Move down", nullptr, false,
                            canvas.selected_node + 1U < entity.nodes.size()) &&
            session.move_selected_entity_node(canvas.selected_node,
                                              canvas.selected_node + 1U)) {
            ++canvas.selected_node;
            canvas.selected_entity_nodes = {canvas.selected_node};
            status = "Entity node moved.";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete...")) request_delete = true;
        ImGui::EndPopup();
    }
    if (request_delete) ImGui::OpenPopup("Delete entity node?");
    if (ImGui::BeginPopupModal("Delete entity node?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& pending = entity.nodes[canvas.selected_node];
        const auto child_count =
            std::ranges::count_if(entity.nodes, [&](const auto& candidate) {
                return candidate.parent && *candidate.parent == pending.id;
            });
        ImGui::Text("Delete '%s'?", pending.name.c_str());
        ImGui::TextWrapped(
            "This node has %zu direct child(ren). Nodes with children are protected until they are reparented.",
            child_count);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4{0.62F, 0.16F, 0.14F, 1.0F});
        if (ImGui::Button("Delete node") &&
            session.remove_selected_entity_node(canvas.selected_node)) {
            canvas.selected_node =
                canvas.selected_node == 0U ? 0U : canvas.selected_node - 1U;
            canvas.selected_entity_nodes = {canvas.selected_node};
            status = "Entity node deleted.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::TextWrapped(
        "Cmd/Ctrl-click selects several nodes; drag a node onto another to reparent it.");
    const std::function<void(const std::optional<std::string>&)> draw_children =
        [&](const auto& parent_id) {
            for (std::size_t node_index = 0; node_index < entity.nodes.size();
                 ++node_index) {
                const auto& candidate = entity.nodes[node_index];
                if (candidate.parent != parent_id) continue;
                ImGui::PushID(candidate.id.c_str());
                const bool has_children =
                    std::ranges::any_of(entity.nodes, [&](const auto& child) {
                        return child.parent && *child.parent == candidate.id;
                    });
                const bool selected = std::ranges::find(
                    canvas.selected_entity_nodes, node_index) !=
                    canvas.selected_entity_nodes.end();
                auto flags = ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_OpenOnDoubleClick |
                    ImGuiTreeNodeFlags_SpanAvailWidth;
                if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf;
                if (selected) flags |= ImGuiTreeNodeFlags_Selected;
                const bool open = ImGui::TreeNodeEx(candidate.name.c_str(), flags);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    const bool additive =
                        ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
                    const auto found = std::ranges::find(
                        canvas.selected_entity_nodes, node_index);
                    if (additive &&
                        found != canvas.selected_entity_nodes.end() &&
                        canvas.selected_entity_nodes.size() > 1U) {
                        canvas.selected_entity_nodes.erase(found);
                        canvas.selected_node = canvas.selected_entity_nodes.back();
                    } else {
                        if (!additive) canvas.selected_entity_nodes.clear();
                        if (std::ranges::find(canvas.selected_entity_nodes,
                                              node_index) ==
                            canvas.selected_entity_nodes.end())
                            canvas.selected_entity_nodes.push_back(node_index);
                        canvas.selected_node = node_index;
                    }
                }
                if (!candidate.locked && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("VERTEX_LOOM_ENTITY_NODE",
                                              &node_index, sizeof(node_index));
                    ImGui::Text("Move %s", candidate.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (probe != nullptr && probe->enabled &&
                    probe->target_mode == 0 && candidate.id == "root")
                    record_target(probe);
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(
                            "VERTEX_LOOM_RESOURCE");
                        payload &&
                        payload->DataSize == sizeof(ResourceDragPayload)) {
                        if (apply_resource_to_node(
                                node_index,
                                *static_cast<const ResourceDragPayload*>(
                                    payload->Data))) {
                            canvas.selected_node = node_index;
                            canvas.selected_entity_nodes = {node_index};
                            if (probe != nullptr && probe->enabled &&
                                probe->target_mode == 0 &&
                                entity.nodes[node_index].id == "root")
                                record_applied(probe);
                            status = "Artwork dropped on existing node.";
                        } else if (session.selected_entity()
                                       ->nodes[node_index]
                                       .drawable.component_instance &&
                                   !session.selected_entity()
                                        ->nodes[node_index]
                                        .drawable.component_instance
                                        ->overrides.empty()) {
                            status = "Drop rejected: confirm incompatible overrides first.";
                        } else {
                            status = "Artwork kind cannot be used on an entity node.";
                        }
                    }
                    if (const auto* payload = ImGui::AcceptDragDropPayload(
                            "VERTEX_LOOM_ENTITY_NODE");
                        payload && payload->DataSize == sizeof(std::size_t)) {
                        const auto moved_index =
                            *static_cast<const std::size_t*>(payload->Data);
                        if (moved_index != node_index &&
                            moved_index < entity.nodes.size()) {
                            auto moved = entity.nodes[moved_index];
                            moved.parent = candidate.id;
                            status = session.set_selected_entity_node(
                                moved_index, std::move(moved))
                                ? "Entity node reparented."
                                : "Reparenting rejected; a hierarchy cycle is not allowed.";
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (advanced_mode) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s%s", candidate.id.c_str(),
                                        candidate.locked ? " · locked" : "");
                } else if (candidate.locked) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("locked");
                }
                if (open) {
                    draw_children(candidate.id);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        };
    draw_children(std::nullopt);
    if (canvas.selected_entity_nodes.size() > 1U)
        ImGui::Text("%zu nodes selected · canvas moves them together",
                    canvas.selected_entity_nodes.size());
}

} // namespace fabric::asset_studio
