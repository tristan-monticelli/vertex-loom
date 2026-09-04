#pragma once

#include "fabric/editor/project_session.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace fabric::asset_studio {

struct EntityNodePropertiesProbe {
    bool enabled{};
    std::function<void()> record_transform;
};

void draw_entity_node_properties(
    editor::ProjectSession& session, std::size_t node_index,
    project::EntityNode& node, bool advanced_mode, std::string& status,
    const EntityNodePropertiesProbe* probe = nullptr);

} // namespace fabric::asset_studio
