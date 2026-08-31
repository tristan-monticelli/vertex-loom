#pragma once

#include "vector_canvas.hpp"

#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <string_view>

namespace fabric::asset_studio {

void draw_packet_preview_canvas(
    CanvasUiState& canvas, ImVec2 available, std::string_view label,
    fabric::editor::ProjectSession* editable_session = nullptr);

} // namespace fabric::asset_studio
