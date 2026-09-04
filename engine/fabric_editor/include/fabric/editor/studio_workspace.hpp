#pragma once

#include "fabric/editor/editor_context.hpp"
#include "fabric/editor/project_session.hpp"

namespace fabric::editor {

[[nodiscard]] constexpr EditorWorkspace workspace_for(
    const StudioResourceKind kind) noexcept {
    switch (kind) {
    case StudioResourceKind::texture:
    case StudioResourceKind::vector:
    case StudioResourceKind::material:
    case StudioResourceKind::textured_path:
    case StudioResourceKind::visual_composition:
    case StudioResourceKind::visual_component:
    case StudioResourceKind::audio:
        return EditorWorkspace::visual;
    case StudioResourceKind::entity:
    case StudioResourceKind::transformation:
        return EditorWorkspace::entity;
    case StudioResourceKind::animation:
        return EditorWorkspace::animation;
    case StudioResourceKind::input:
    case StudioResourceKind::behavior:
    case StudioResourceKind::mechanic:
        return EditorWorkspace::logic;
    case StudioResourceKind::map:
        return EditorWorkspace::map;
    case StudioResourceKind::scene:
        return EditorWorkspace::scene;
    case StudioResourceKind::replay:
        return EditorWorkspace::publish;
    }
    return EditorWorkspace::visual;
}

} // namespace fabric::editor
