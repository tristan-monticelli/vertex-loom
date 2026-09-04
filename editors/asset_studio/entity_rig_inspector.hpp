#pragma once

#include "fabric/editor/project_session.hpp"

#include <span>
#include <string>

namespace fabric::asset_studio {

using EntityNodePicker = bool (*)(
    const char*, std::span<const project::EntityNode>, std::string&);

void draw_entity_rig_inspector(
    editor::ProjectSession& session,
    bool advanced_mode,
    std::string& status,
    EntityNodePicker draw_node_picker);

} // namespace fabric::asset_studio
