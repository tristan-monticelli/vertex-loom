#include "visual_composition_layer_panel.hpp"

#include "editor_widgets.hpp"

#include <imgui.h>

#include <ranges>
#include <string>
#include <utility>

namespace fabric::asset_studio {

void draw_visual_composition_layer_panel(
    editor::ProjectSession& session,
    VisualCompositionLayerPanelState& state,
    std::string& status) {
    const auto selected = session.selected_visual_composition();
    if (!selected) return;
    if (state.document_id != selected->document.id.value) {
        state.document_id = selected->document.id.value;
        state.selected_layer_id = selected->layers.empty()
            ? std::string{} : selected->layers.front().id;
        state.add_layer_kind = project::VisualLayerKind::raster;
        state.add_resource_id.clear();
    }
    if (!selected->layers.empty() && !std::ranges::any_of(
            selected->layers, [&](const auto& layer) {
                return layer.id == state.selected_layer_id;
            })) {
        state.selected_layer_id = selected->layers.front().id;
    }

    ImGui::SeparatorText("Layer tree");
    for (const auto& layer : selected->layers) {
        ImGui::PushID(layer.id.c_str());
        const auto flags = ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            (layer.id == state.selected_layer_id
                 ? ImGuiTreeNodeFlags_Selected : 0);
        ImGui::TreeNodeEx("layer", flags, "%s  ·  %s",
                          layer.name.c_str(),
                          std::string(project::to_string(layer.kind)).c_str());
        if (ImGui::IsItemClicked()) state.selected_layer_id = layer.id;
        ImGui::PopID();
    }

    ImGui::SeparatorText("Add layer");
    const auto kind_label = std::string(
        project::to_string(state.add_layer_kind));
    if (ImGui::BeginCombo("Layer type", kind_label.c_str())) {
        for (const auto kind : {
                 project::VisualLayerKind::raster,
                 project::VisualLayerKind::vector,
                 project::VisualLayerKind::component,
                 project::VisualLayerKind::textured_path}) {
            const bool is_selected = state.add_layer_kind == kind;
            const auto label = std::string(project::to_string(kind));
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                state.add_layer_kind = kind;
                state.add_resource_id.clear();
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const auto accepts_kind = [&](const auto& resource) {
        using Kind = editor::StudioResourceKind;
        return (state.add_layer_kind == project::VisualLayerKind::raster &&
                resource.kind == Kind::texture) ||
            (state.add_layer_kind == project::VisualLayerKind::vector &&
             resource.kind == Kind::vector) ||
            (state.add_layer_kind == project::VisualLayerKind::component &&
             resource.kind == Kind::visual_component) ||
            (state.add_layer_kind == project::VisualLayerKind::textured_path &&
             resource.kind == Kind::textured_path);
    };
    const auto add_resource = std::ranges::find_if(
        session.resources(), [&](const auto& resource) {
            return accepts_kind(resource) &&
                resource.id.value == state.add_resource_id;
        });
    const char* add_resource_label = add_resource == session.resources().end()
        ? "Choose a resource..." : add_resource->name.c_str();
    if (ImGui::BeginCombo("Resource", add_resource_label)) {
        for (const auto& resource : session.resources()) {
            if (!accepts_kind(resource)) continue;
            const bool is_selected = resource.id.value == state.add_resource_id;
            if (ImGui::Selectable(resource.name.c_str(), is_selected))
                state.add_resource_id = resource.id.value;
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(add_resource == session.resources().end());
    if (ImGui::Button("Add selected resource")) {
        auto candidate = *selected;
        auto id = add_resource->id.value;
        const auto base = id;
        for (std::size_t suffix = 2U; std::ranges::any_of(
                 candidate.layers, [&](const auto& layer) {
                     return layer.id == id;
                 }); ++suffix) {
            id = base + "-" + std::to_string(suffix);
        }
        const auto expected_type =
            state.add_layer_kind == project::VisualLayerKind::raster ? "texture" :
            state.add_layer_kind == project::VisualLayerKind::vector ? "vector" :
            state.add_layer_kind == project::VisualLayerKind::component
                ? "visualComponent" : "texturedPath";
        project::VisualCompositionLayer layer{
            .id = id,
            .name = add_resource->name,
            .kind = state.add_layer_kind,
            .resource = {add_resource->id, expected_type},
            .z_order = static_cast<float>(candidate.layers.size())};
        if (state.add_layer_kind == project::VisualLayerKind::component)
            layer.component_instance = project::VisualComponentInstance{};
        candidate.layers.push_back(std::move(layer));
        if (session.set_selected_visual_composition(std::move(candidate))) {
            state.selected_layer_id = id;
            status = "Visual composition layer added.";
        } else {
            status = "Visual composition layer rejected; inspect diagnostics.";
        }
    }
    ImGui::EndDisabled();
    const bool no_resource = add_resource == session.resources().end();
    editor_ui::draw_disabled_reason(
        no_resource, "Choose a compatible indexed resource first.");

    const auto current = session.selected_visual_composition();
    if (!current) return;
    const auto selected_layer = std::ranges::find(
        current->layers, state.selected_layer_id,
        &project::VisualCompositionLayer::id);
    if (selected_layer == current->layers.end()) return;
    if (ImGui::Button("Duplicate layer")) {
        auto candidate = *current;
        auto copy = *selected_layer;
        const auto base = copy.id + "-copy";
        copy.id = base;
        for (std::size_t suffix = 2U; std::ranges::any_of(
                 candidate.layers, [&](const auto& layer) {
                     return layer.id == copy.id;
                 }); ++suffix) {
            copy.id = base + "-" + std::to_string(suffix);
        }
        copy.name += " copy";
        const auto copy_id = copy.id;
        const auto insertion = static_cast<std::size_t>(
            std::ranges::distance(candidate.layers.begin(), selected_layer));
        candidate.layers.insert(candidate.layers.begin() +
                                static_cast<std::ptrdiff_t>(insertion + 1U),
                                std::move(copy));
        if (session.set_selected_visual_composition(std::move(candidate))) {
            state.selected_layer_id = copy_id;
            status = "Visual composition layer duplicated.";
        } else {
            status = "Visual composition layer rejected; inspect diagnostics.";
        }
    }
}

} // namespace fabric::asset_studio
