#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/scene.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::editor {

class SceneSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path& project_root,
                              const project::SceneDocument& scene);
    [[nodiscard]] bool open(const std::filesystem::path& project_root,
                            const core::ResourceId& scene_id);
    [[nodiscard]] bool save();
    [[nodiscard]] AutosaveStatus update_autosave(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool accept_recovery(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    void decline_recovery() noexcept;

    [[nodiscard]] bool set_name(std::string name);
    [[nodiscard]] bool add_map(project::SceneMapReference map);
    [[nodiscard]] bool set_map(std::size_t index,
                               project::SceneMapReference map);
    [[nodiscard]] bool remove_map(std::size_t index);
    [[nodiscard]] bool set_entry_map(
        std::optional<core::ResourceId> map_id);
    [[nodiscard]] bool add_transition(project::SceneTransition transition);
    [[nodiscard]] bool set_transition(
        std::size_t index, project::SceneTransition transition);
    [[nodiscard]] bool remove_transition(std::size_t index);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool has_scene() const noexcept { return scene_.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] bool has_recovery() const noexcept {
        return recovery_scene_.has_value();
    }
    [[nodiscard]] const std::optional<project::SceneDocument>& scene() const
        noexcept { return scene_; }
    [[nodiscard]] const std::filesystem::path& project_root() const noexcept {
        return project_root_;
    }
    [[nodiscard]] const std::optional<project::ProjectManifest>& manifest()
        const noexcept { return manifest_; }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept {
        return errors_;
    }

private:
    [[nodiscard]] bool commit(project::SceneDocument next);

    std::filesystem::path project_root_;
    std::filesystem::path document_path_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::SceneDocument> scene_;
    std::optional<project::SceneDocument> recovery_scene_;
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
