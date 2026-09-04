#include "raster_view_inspector.hpp"

#include <imgui.h>

#include <algorithm>

namespace fabric::asset_studio {

void draw_raster_view_inspector(
    editor::ProjectSession& session,
    RasterViewInspectorState& state,
    std::string& status) {
    const auto imported = session.imported_texture();
    if (!imported) return;
    const auto& texture = imported->asset;
    const auto& view = texture.view;
    if (state.document_id != texture.document.id.value ||
        (state.source != view && !ImGui::IsAnyItemActive())) {
        state.document_id = texture.document.id.value;
        state.draft = view.value_or(project::RasterView{
            .crop = {{0.0F, 0.0F},
                     {static_cast<float>(texture.width),
                      static_cast<float>(texture.height)}}});
        state.source = view;
    }

    float crop_origin[2]{state.draft.crop.origin.x, state.draft.crop.origin.y};
    float crop_size[2]{state.draft.crop.size.x, state.draft.crop.size.y};
    float crop_pivot[2]{state.draft.pivot.x, state.draft.pivot.y};
    float position[2]{state.draft.transform.position.x,
                      state.draft.transform.position.y};
    float scale[2]{state.draft.transform.scale.x, state.draft.transform.scale.y};
    float rotation = state.draft.transform.rotation_degrees;
    ImGui::InputFloat2("Crop origin (pixels)", crop_origin);
    ImGui::SetItemTooltip("Top-left crop origin measured in source pixels.");
    ImGui::InputFloat2("Crop size (pixels)", crop_size);
    ImGui::SetItemTooltip("Crop width and height measured in source pixels.");
    ImGui::InputFloat2("Pivot (normalized)", crop_pivot);
    ImGui::SetItemTooltip("Normalized pivot used by the raster view transform.");
    ImGui::InputFloat2("View position (world units)", position);
    ImGui::SetItemTooltip("Raster view translation in project world units.");
    ImGui::InputFloat("View rotation (degrees)", &rotation);
    ImGui::SetItemTooltip("Raster view rotation around its normalized pivot.");
    ImGui::InputFloat2("View scale (factor)", scale);
    ImGui::SetItemTooltip("Raster view scale multiplier on each axis.");
    if (ImGui::Button("Apply crop/view")) {
        state.draft.crop.origin = {crop_origin[0], crop_origin[1]};
        state.draft.crop.size = {crop_size[0], crop_size[1]};
        state.draft.pivot = {crop_pivot[0], crop_pivot[1]};
        state.draft.transform.position = {position[0], position[1]};
        state.draft.transform.rotation_degrees = rotation;
        state.draft.transform.scale = {scale[0], scale[1]};
        if (session.set_selected_texture_view(state.draft)) {
            state.source = state.draft;
            status = "Raster view saved in the document.";
        } else {
            status = "Raster view rejected; inspect diagnostics.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset full source")) {
        if (session.reset_selected_texture_view()) {
            state.document_id.clear();
            state.source.reset();
            status = "Raster view reset to the full source.";
        } else {
            status = "Raster view reset failed; inspect diagnostics.";
        }
    }
}

} // namespace fabric::asset_studio
