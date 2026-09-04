#pragma once

#include "fabric/editor/project_session.hpp"

#include <cstddef>
#include <string>

namespace fabric::asset_studio {

struct VisualCompositionLayerPanelState {
    std::string document_id;
    std::string selected_layer_id;
    project::VisualLayerKind add_layer_kind{
        project::VisualLayerKind::raster};
    std::string add_resource_id;
};

void draw_visual_composition_layer_panel(
    editor::ProjectSession& session,
    VisualCompositionLayerPanelState& state,
    std::string& status);

} // namespace fabric::asset_studio
