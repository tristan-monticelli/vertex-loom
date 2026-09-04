#pragma once

#include "fabric/editor/project_session.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace fabric::asset_studio {

struct VisualComponentInspectorState {
    std::string document_id;
    std::string selected_anchor_id;
    std::string selected_parameter_id;
};

using VisualComponentResourcePicker = bool (*)(
    const char*, std::span<const editor::StudioResource>,
    editor::StudioResourceKind, std::string&, bool, bool);

void draw_visual_component_inspector(
    editor::ProjectSession& session,
    VisualComponentInspectorState& state,
    std::string& status,
    VisualComponentResourcePicker resource_picker);

} // namespace fabric::asset_studio
