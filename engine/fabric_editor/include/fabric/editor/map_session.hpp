#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
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
    [[nodiscard]] AutosaveStatus update_autosave(
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    [[nodiscard]] bool accept_recovery(
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    void decline_recovery() noexcept;
    [[nodiscard]] bool place_instance(project::MapInstance instance,
                                       MapSnapSettings snapping = {});
    [[nodiscard]] bool remove_instance(const core::ResourceId& instance_id);
    [[nodiscard]] bool remove_instances(const std::vector<core::ResourceId>& instance_ids);
    [[nodiscard]] bool duplicate_instance(const core::ResourceId& instance_id,
                                          core::Vec2 offset = {1.0F, 1.0F},
                                          MapSnapSettings snapping = {});
    [[nodiscard]] bool reorder_instance(const core::ResourceId& instance_id,
                                        std::size_t target_index);
    [[nodiscard]] bool set_instance_transform(const core::ResourceId& instance_id,
                                               core::Transform transform,
                                               MapSnapSettings snapping = {});
    [[nodiscard]] bool set_instance_layer(const core::ResourceId& instance_id,
                                           const core::ResourceId& layer_id);
    [[nodiscard]] bool set_instances_layer(const std::vector<core::ResourceId>& instance_ids,
                                            const core::ResourceId& layer_id);
    [[nodiscard]] bool set_instance_property(const core::ResourceId& instance_id,
                                              project::MapProperty property);
    [[nodiscard]] bool translate_instances(
        const std::vector<core::ResourceId>& instance_ids, core::Vec2 delta,
        MapSnapSettings snapping = {});
    [[nodiscard]] bool set_layer_visibility(const core::ResourceId& layer_id, bool visible);
    [[nodiscard]] bool set_layer_locked(const core::ResourceId& layer_id, bool locked);
    [[nodiscard]] bool set_layer_depth(const core::ResourceId& layer_id, float depth);
    [[nodiscard]] bool set_prefab_override(const core::ResourceId& prefab_id,
                                            project::MapProperty property);
    [[nodiscard]] std::vector<project::MapProperty> effective_instance_properties(
        const core::ResourceId& instance_id) const;
    [[nodiscard]] static core::Vec2 snap_position(core::Vec2 position,
                                                  MapSnapSettings snapping = {}) noexcept;
    [[nodiscard]] bool declare_event(project::MapEventDefinition event);
    [[nodiscard]] bool remove_event(const core::ResourceId& event_id);
    [[nodiscard]] bool set_event_payload(const core::ResourceId& event_id,
                                         std::vector<project::MapProperty> payload);
    [[nodiscard]] bool add_trigger(project::TriggerDefinition trigger);
    [[nodiscard]] bool remove_trigger(const core::ResourceId& trigger_id);
    [[nodiscard]] bool set_trigger(std::size_t trigger_index,
                                   project::TriggerDefinition trigger);
    [[nodiscard]] bool set_collision_shape(std::size_t collision_index,
                                           project::CollisionShape shape);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool has_map() const noexcept { return map_.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] bool has_recovery() const noexcept { return recovery_map_.has_value(); }
    [[nodiscard]] const std::optional<project::MapDocument>& map() const noexcept { return map_; }
    [[nodiscard]] const std::filesystem::path& project_root() const noexcept {
        return project_root_;
    }
    [[nodiscard]] const std::optional<project::ProjectManifest>& manifest() const noexcept {
        return manifest_;
    }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept { return errors_; }

private:
    std::filesystem::path project_root_;
    std::filesystem::path map_document_path_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    std::optional<project::MapDocument> recovery_map_;
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
