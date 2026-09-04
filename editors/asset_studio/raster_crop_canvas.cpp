#include "raster_crop_canvas.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

namespace fabric::asset_studio {

void draw_raster_crop_canvas_panel(
    editor::ProjectSession& session,
    const AssetPreview& preview,
    CanvasUiState& canvas,
    const ImVec2 available,
    std::string& status,
    RasterCropCanvasProbe* probe) {
    if (!session.imported_texture() || preview.texture == 0U) return;
    const auto& texture = session.imported_texture()->asset;
    if (canvas.crop_resource_id != texture.document.id.value) {
        canvas.crop_resource_id = texture.document.id.value;
        canvas.crop_drag.reset();
    }
    ImGui::InvisibleButton("Raster crop canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const auto mark_probe = [&](const ImVec2 source, const ImVec2 target) {
        if (probe == nullptr || !probe->enabled) return;
        if (probe->canvas_seen != nullptr) *probe->canvas_seen = true;
        if (probe->crop_source != nullptr) *probe->crop_source = source;
        if (probe->crop_target != nullptr) *probe->crop_target = target;
    };
    const auto maximum = ImGui::GetItemRectMax();
    mark_probe({maximum.x - 20.0F, maximum.y - 20.0F},
               {maximum.x - 44.0F, maximum.y - 44.0F});
    const ImVec2 origin = ImGui::GetItemRectMin();
    const float source_width = static_cast<float>(texture.width);
    const float source_height = static_cast<float>(texture.height);
    const float fit_scale = std::max(
        0.001F, std::min((available.x - 32.0F) / source_width,
                         (available.y - 32.0F) / source_height));
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
        canvas.zoom = std::clamp(
            canvas.zoom * (ImGui::GetIO().MouseWheel > 0.0F ? 1.15F
                                                             : 1.0F / 1.15F),
            0.1F, 20.0F);
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const auto delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        canvas.pan.x += delta.x;
        canvas.pan.y += delta.y;
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }
    const float scale = fit_scale * canvas.zoom;
    const ImVec2 image_size{source_width * scale, source_height * scale};
    const ImVec2 image_min{origin.x + (available.x - image_size.x) * 0.5F + canvas.pan.x,
                           origin.y + (available.y - image_size.y) * 0.5F + canvas.pan.y};
    const ImVec2 image_max{image_min.x + image_size.x, image_min.y + image_size.y};
    auto view = texture.view.value_or(project::RasterView{
        .crop = {{0.0F, 0.0F}, {source_width, source_height}},
    });
    const auto crop_screen_rect = [&](const project::RasterView& value) {
        return std::pair{
            ImVec2{image_min.x + value.crop.origin.x * scale,
                   image_min.y + value.crop.origin.y * scale},
            ImVec2{image_min.x + (value.crop.origin.x + value.crop.size.x) * scale,
                   image_min.y + (value.crop.origin.y + value.crop.size.y) * scale}};
    };
    auto [crop_min, crop_max] = crop_screen_rect(view);
    mark_probe(crop_max, {crop_max.x - 24.0F, crop_max.y - 24.0F});
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto is_near = [&](const ImVec2 point) {
        return std::hypot(mouse.x - point.x, mouse.y - point.y) <= 11.0F;
    };
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 top_right{crop_max.x, crop_min.y};
        const ImVec2 bottom_left{crop_min.x, crop_max.y};
        if (is_near(crop_min)) canvas.crop_drag = editor::RasterCropDrag::top_left;
        else if (is_near(top_right)) canvas.crop_drag = editor::RasterCropDrag::top_right;
        else if (is_near(bottom_left)) canvas.crop_drag = editor::RasterCropDrag::bottom_left;
        else if (is_near(crop_max)) canvas.crop_drag = editor::RasterCropDrag::bottom_right;
        else if (mouse.x >= crop_min.x && mouse.x <= crop_max.x &&
                 mouse.y >= crop_min.y && mouse.y <= crop_max.y)
            canvas.crop_drag = editor::RasterCropDrag::move;
        if (canvas.crop_drag) {
            canvas.crop_start_mouse = mouse;
            canvas.crop_start_view = view;
        }
    }
    if (canvas.crop_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const core::Vec2 delta{(mouse.x - canvas.crop_start_mouse.x) / scale,
                               (mouse.y - canvas.crop_start_mouse.y) / scale};
        const auto candidate = editor::drag_raster_crop(
            canvas.crop_start_view, *canvas.crop_drag, delta,
            texture.width, texture.height);
        if (candidate.crop != view.crop) {
            if (session.set_selected_texture_view(candidate)) {
                view = candidate;
                status = "Raster crop changed; source pixels are unchanged.";
            } else status = "Raster crop rejected; inspect diagnostics.";
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) canvas.crop_drag.reset();
    std::tie(crop_min, crop_max) = crop_screen_rect(view);
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin, {origin.x + available.x, origin.y + available.y}, true);
    draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(preview.texture)),
                        image_min, image_max, {0.0F, 1.0F}, {1.0F, 0.0F});
    constexpr ImU32 shade = IM_COL32(8, 10, 14, 170);
    draw_list->AddRectFilled(image_min, {image_max.x, crop_min.y}, shade);
    draw_list->AddRectFilled({image_min.x, crop_max.y}, image_max, shade);
    draw_list->AddRectFilled({image_min.x, crop_min.y}, {crop_min.x, crop_max.y}, shade);
    draw_list->AddRectFilled({crop_max.x, crop_min.y}, {image_max.x, crop_max.y}, shade);
    draw_list->AddRect(crop_min, crop_max, IM_COL32(244, 190, 80, 255), 0.0F,
                       ImDrawFlags_None, 2.0F);
    for (const ImVec2 handle : {crop_min, ImVec2{crop_max.x, crop_min.y},
                                ImVec2{crop_min.x, crop_max.y}, crop_max})
        draw_list->AddRectFilled({handle.x - 5.0F, handle.y - 5.0F},
                                 {handle.x + 5.0F, handle.y + 5.0F},
                                 IM_COL32(250, 220, 145, 255));
    draw_list->AddText({image_min.x + 8.0F, image_min.y + 8.0F},
        IM_COL32(245, 245, 245, 255),
        (std::to_string(static_cast<int>(view.crop.size.x)) + " x " +
         std::to_string(static_cast<int>(view.crop.size.y)) + " px").c_str());
    draw_list->PopClipRect();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
        "Drag inside to move the crop. Drag a corner to resize it. The source image is never rewritten.");
}

} // namespace fabric::asset_studio
