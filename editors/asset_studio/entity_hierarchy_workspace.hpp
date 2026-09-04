#pragma once

#include "fabric/editor/project_session.hpp"
#include "preview_canvas.hpp"

#include <functional>
#include <string>
#include <utility>

namespace fabric::asset_studio {

struct ResourceDragPayload {
    int kind{};
    char id[256]{};
};

struct EntityHierarchyProbe {
    bool enabled{};
    int target_mode{};
    std::function<void(float, float)> record_target;
    std::function<void()> record_applied;
};

void draw_entity_hierarchy_workspace(
    editor::ProjectSession& session, CanvasUiState& canvas,
    bool advanced_mode, std::string& status,
    const EntityHierarchyProbe* probe = nullptr);

} // namespace fabric::asset_studio
