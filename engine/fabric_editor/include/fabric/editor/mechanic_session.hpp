#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/physics/mechanic_simulation.hpp"
#include "fabric/project/mechanic_graph.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace fabric::editor {

class MechanicSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path& project_root,
                              const project::MapDocument& map,
                              const project::MechanicGraph& graph);
    [[nodiscard]] bool open(const std::filesystem::path& project_root,
                            const project::MapDocument& map,
                            const core::ResourceId& graph_id);
    [[nodiscard]] bool save();
    [[nodiscard]] AutosaveStatus update_autosave(
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    [[nodiscard]] bool accept_recovery(
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    void decline_recovery() noexcept;

    [[nodiscard]] bool add_node(project::MechanicNodeKind kind, std::string id);
    [[nodiscard]] bool remove_node(const core::ResourceId& node_id);
    [[nodiscard]] bool set_node_property(const core::ResourceId& node_id,
                                         std::string property_id,
                                         project::MechanicValue value);
    [[nodiscard]] bool connect(project::MechanicConnection connection);
    [[nodiscard]] bool disconnect(std::size_t connection_index);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    void play() noexcept { simulation_.play(); }
    void pause() noexcept { simulation_.pause(); }
    [[nodiscard]] bool step_once() { return simulation_.step_once(); }
    [[nodiscard]] bool reset_preview();
    [[nodiscard]] bool set_preview_sensor_active(
        const core::ResourceId& node_id, bool active) noexcept {
        return simulation_.set_sensor_active(node_id, active);
    }
    [[nodiscard]] bool set_preview_event_active(
        const core::ResourceId& event_id, bool active) noexcept {
        return simulation_.set_event_active(event_id, active);
    }
    [[nodiscard]] bool place_preview_character(
        physics::MechanicPreviewCharacterConfig config) {
        return simulation_.place_preview_character(config);
    }
    [[nodiscard]] bool set_preview_character_velocity(
        core::Vec2 velocity) noexcept {
        return simulation_.set_preview_character_velocity(velocity);
    }
    [[nodiscard]] bool update_preview(float frame_seconds) {
        return simulation_.update(frame_seconds);
    }

    [[nodiscard]] bool has_graph() const noexcept { return graph_.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] bool has_recovery() const noexcept { return recovery_graph_.has_value(); }
    [[nodiscard]] const std::optional<project::MechanicGraph>& graph() const noexcept {
        return graph_;
    }
    [[nodiscard]] const physics::MechanicSimulation& simulation() const noexcept {
        return simulation_;
    }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept {
        return errors_;
    }
    [[nodiscard]] const std::vector<project::Error>& preview_errors() const noexcept {
        return preview_errors_;
    }

private:
    [[nodiscard]] bool commit(project::MechanicGraph next);
    void rebuild_preview();

    std::filesystem::path project_root_;
    std::filesystem::path document_path_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    std::optional<project::MechanicGraph> graph_;
    std::optional<project::MechanicGraph> recovery_graph_;
    CommandStack commands_;
    AutosaveScheduler autosave_;
    physics::MechanicSimulation simulation_;
    std::vector<project::Error> errors_;
    std::vector<project::Error> preview_errors_;
};

} // namespace fabric::editor
