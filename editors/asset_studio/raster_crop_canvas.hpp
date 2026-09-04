#pragma once

#include "import_workflow.hpp"
#include "vector_canvas.hpp"

#include <string>

namespace fabric::asset_studio {

struct RasterCropCanvasProbe {
    bool enabled{};
    bool* canvas_seen{};
    ImVec2* crop_source{};
    ImVec2* crop_target{};
};

void draw_raster_crop_canvas_panel(
    editor::ProjectSession& session,
    const AssetPreview& preview,
    CanvasUiState& canvas,
    ImVec2 available,
    std::string& status,
    RasterCropCanvasProbe* probe = nullptr);

} // namespace fabric::asset_studio
