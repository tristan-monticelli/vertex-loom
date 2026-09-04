#pragma once

#include "fabric/editor/project_session.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace fabric::asset_studio {

struct EntityArtworkInspectorState {
    std::optional<std::pair<std::size_t, project::EntityDrawableKind>>
        pending_drawable_kind;
    bool force_discard_modal{};
};

struct EntityArtworkInspectorProbe {
    bool enabled{};
    std::function<void(float, float)> record_kind;
    std::function<void(float, float)> record_texture;
    std::function<void(float, float)> record_cancel;
    std::function<void(float, float)> record_confirm;
    std::function<void()> record_modal;
};

using EntityArtworkResourcePicker = bool (*)(
    const char*, std::span<const editor::StudioResource>,
    editor::StudioResourceKind, std::string&, bool, bool);
using EntityArtworkKindLabel = std::string_view (*)(
    editor::StudioResourceKind);
using EntityArtworkContractKind = std::optional<editor::StudioResourceKind> (*)(
    std::string_view);
using EntityArtworkSurfaceEditor = bool (*)(
    project::ShaderSurfaceSettings&, const char*, bool);
using EntityArtworkOpenResource =
    std::function<void(const editor::StudioResource&)>;

void draw_entity_artwork_inspector(
    editor::ProjectSession& session, std::size_t node_index,
    project::EntityNode& node, bool advanced_mode,
    EntityArtworkInspectorState& state, std::string& status,
    EntityArtworkResourcePicker draw_resource_picker,
    EntityArtworkKindLabel resource_kind_label,
    EntityArtworkContractKind contract_kind,
    EntityArtworkSurfaceEditor draw_surface_editor,
    const EntityArtworkOpenResource& open_resource,
    const EntityArtworkInspectorProbe* probe = nullptr);

} // namespace fabric::asset_studio
