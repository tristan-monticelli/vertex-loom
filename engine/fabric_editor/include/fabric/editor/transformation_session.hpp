#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/entity_transformation.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace fabric::editor {

class TransformationSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path&,
                              const project::EntityTransformation&);
    [[nodiscard]] bool open(const std::filesystem::path&,
                            const core::ResourceId&);
    [[nodiscard]] bool save();
    [[nodiscard]] AutosaveStatus update_autosave(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool accept_recovery(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    void decline_recovery() noexcept;

    [[nodiscard]] bool set_source(core::ResourceId);
    [[nodiscard]] bool set_destination(core::ResourceId);
    [[nodiscard]] bool set_policy(project::EntityTransferPolicy);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool has_transformation() const noexcept {
        return transformation_.has_value();
    }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] bool has_recovery() const noexcept { return recovery_.has_value(); }
    [[nodiscard]] const std::optional<project::EntityTransformation>&
    transformation() const noexcept { return transformation_; }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept {
        return errors_;
    }

private:
    [[nodiscard]] bool commit(project::EntityTransformation);
    [[nodiscard]] project::ValidationReport validate(
        const project::EntityTransformation&) const;

    std::filesystem::path root_;
    std::filesystem::path path_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::EntityTransformation> transformation_;
    std::optional<project::EntityTransformation> recovery_;
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
