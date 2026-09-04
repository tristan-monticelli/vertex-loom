#include "entity_rig_inspector.hpp"

#include "editor_widgets.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace fabric::asset_studio {

using editor_ui::draw_technical_tooltip;

void draw_entity_rig_inspector(
    fabric::editor::ProjectSession& session,
    const bool advanced_mode,
    std::string& status,
    const EntityNodePicker node_picker) {
    const auto* selected = session.selected_resource();
    if (selected == nullptr ||
        selected->kind != fabric::editor::StudioResourceKind::entity ||
        !session.selected_entity()) {
        return;
    }
    const auto draw_entity_node_picker =
        [&](const char* label,
            const std::span<const fabric::project::EntityNode> nodes,
            std::string& selected_id) {
            return node_picker(label, nodes, selected_id);
        };
            const auto entity = *session.selected_entity();
            const auto commit_advanced_entity =
                [&](fabric::project::EntityDefinition next) {
                    if (!session.set_selected_entity_definition(std::move(next)))
                        status = "Advanced entity edit rejected; inspect diagnostics.";
                    else
                        status = "Advanced entity section saved.";
                };
            ImGui::SeparatorText("Rig and IK");
            ImGui::TextDisabled(
                "Create chains from the Viewer toolbar, then drag their target handle.");
            if (!entity.ik_chains.empty())
                ImGui::TextDisabled(
                    "%zu IK chain(s) shown on the Entity canvas.",
                    entity.ik_chains.size());
            if (advanced_mode &&
                ImGui::CollapsingHeader(
                    "Advanced systems: constraints, IK and deformation")) {
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
                if (entity.ik_chains.empty())
                    ImGui::TextDisabled(
                        "No IK chains configured; use the guided canvas action above.");
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

} // namespace fabric::asset_studio
