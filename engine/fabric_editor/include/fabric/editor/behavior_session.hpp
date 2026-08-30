#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/behavior_graph.hpp"
#include "fabric/runtime/behavior_evaluator.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fabric::editor {

class BehaviorSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path&,
                              const project::BehaviorGraph&);
    [[nodiscard]] bool open(const std::filesystem::path&,
                            const core::ResourceId&);
    [[nodiscard]] bool save();
    [[nodiscard]] AutosaveStatus update_autosave(
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    [[nodiscard]] bool accept_recovery(
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    void decline_recovery() noexcept;

    [[nodiscard]] bool add_node(std::string type, std::string id);
    [[nodiscard]] bool duplicate_node(const core::ResourceId&, std::string new_id);
    [[nodiscard]] bool remove_node(const core::ResourceId&);
    [[nodiscard]] bool set_node_property(const core::ResourceId&, std::string,
                                         project::BehaviorValue);
    [[nodiscard]] bool connect(project::BehaviorConnection);
    [[nodiscard]] bool disconnect(const core::ResourceId&);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] std::vector<runtime::BehaviorAction> preview(
        const runtime::BehaviorSignal&, float fixed_step_seconds);
    void reset_preview();

    [[nodiscard]] bool has_graph() const noexcept { return graph_.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] bool has_recovery() const noexcept { return recovery_.has_value(); }
    [[nodiscard]] const std::optional<project::BehaviorGraph>& graph() const noexcept { return graph_; }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept { return errors_; }
    [[nodiscard]] const std::vector<runtime::BehaviorTraceEntry>& trace() const noexcept;

private:
    [[nodiscard]] bool commit(project::BehaviorGraph);
    void rebuild_preview();

    std::filesystem::path root_;
    std::filesystem::path path_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::BehaviorGraph> graph_;
    std::optional<project::BehaviorGraph> recovery_;
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::unique_ptr<runtime::BehaviorEvaluator> evaluator_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
