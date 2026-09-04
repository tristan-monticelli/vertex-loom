#pragma once

#include "fabric/editor/project_session.hpp"

#include <optional>
#include <string>

namespace fabric::asset_studio {

struct RasterViewInspectorState {
    std::string document_id;
    project::RasterView draft;
    std::optional<project::RasterView> source;
};

void draw_raster_view_inspector(
    editor::ProjectSession& session,
    RasterViewInspectorState& state,
    std::string& status);

} // namespace fabric::asset_studio
