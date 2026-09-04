#pragma once

#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <span>
#include <string>

namespace fabric::asset_studio {

struct EntityRigInspectorProbe {
    bool enabled{};
    bool starter_mesh_seen{};
    bool starter_mesh_clicked{};
    ImVec2 starter_mesh_screen{};
};

using EntityNodePicker = bool (*)(
    const char*, std::span<const project::EntityNode>, std::string&);

void draw_entity_rig_inspector(
    editor::ProjectSession& session,
    bool advanced_mode,
    std::string& status,
    EntityNodePicker draw_node_picker,
    EntityRigInspectorProbe* probe = nullptr);

} // namespace fabric::asset_studio
