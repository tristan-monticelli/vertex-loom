#pragma once

#include "fabric/editor/command_stack.hpp"
#include "fabric/project/map.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace fabric::editor {

struct MapSnapSettings {
    bool enabled{true};
    float grid_size{1.0F};
    core::Vec2 origin{};
};

class MapSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path& project_root,
                              const project::MapDocument& map);
    [[nodiscard]] bool open(const std::filesystem::path& project_root,
                            const core::ResourceId& map_id);
    [[nodiscard]] bool save();
    [[nodiscard]] bool place_instance(project::MapInstance instance,
                                       MapSnapSettings snapping = {});
    [[nodiscard]] bool remove_instance(const core::ResourceId& instance_id);
    [[nodiscard]] bool set_instance_transform(const core::ResourceId& instance_id,
                                               core::Transform transform,
                                               MapSnapSettings snapping = {});
    [[nodiscard]] bool set_instance_property(const core::ResourceId& instance_id,
                                              project::MapProperty property);
    [[nodiscard]] bool set_layer_visibility(const core::ResourceId& layer_id, bool visible);
    [[nodiscard]] bool set_layer_locked(const core::ResourceId& layer_id, bool locked);
    [[nodiscard]] bool set_layer_depth(const core::ResourceId& layer_id, float depth);
    [[nodiscard]] bool set_prefab_override(const core::ResourceId& prefab_id,
                                            project::MapProperty property);
    [[nodiscard]] static core::Vec2 snap_position(core::Vec2 position,
                                                  MapSnapSettings snapping = {}) noexcept;
    [[nodiscard]] bool declare_event(project::MapEventDefinition event);
    [[nodiscard]] bool remove_event(const core::ResourceId& event_id);
    [[nodiscard]] bool add_trigger(project::TriggerDefinition trigger);
    [[nodiscard]] bool remove_trigger(const core::ResourceId& trigger_id);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool has_map() const noexcept { return map_.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] const std::optional<project::MapDocument>& map() const noexcept { return map_; }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept { return errors_; }

private:
    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    CommandStack commands_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
