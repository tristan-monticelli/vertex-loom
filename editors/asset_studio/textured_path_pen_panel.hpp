#pragma once

#include "fabric/editor/project_session.hpp"

#include <cstddef>
#include <string>

namespace fabric::asset_studio {

struct TexturedPathPenPanelState {
    std::string document_id;
    std::size_t selected_command{};
};

void draw_textured_path_pen_panel(
    editor::ProjectSession& session,
    TexturedPathPenPanelState& state,
    std::string& status);

} // namespace fabric::asset_studio
