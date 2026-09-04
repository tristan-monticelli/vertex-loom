#pragma once

#include "vector_canvas.hpp"

#include "fabric/editor/project_session.hpp"

#include <imgui.h>

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace fabric::asset_studio {

using EntityTransformCommit = std::function<bool(
    std::vector<std::pair<std::size_t, fabric::core::Transform>>)>;

using EntityParticleCommit = std::function<bool(std::size_t, fabric::core::Vec2)>;

void draw_packet_preview_canvas(
    CanvasUiState& canvas, ImVec2 available, std::string_view label,
    fabric::editor::ProjectSession* editable_session = nullptr,
    EntityTransformCommit transform_commit = {},
    EntityParticleCommit particle_commit = {});

} // namespace fabric::asset_studio
