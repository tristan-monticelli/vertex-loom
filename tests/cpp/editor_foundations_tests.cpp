#include "fabric/editor/command_stack.hpp"
#include "fabric/editor/editor_action_registry.hpp"
#include "fabric/editor/editor_context.hpp"
#include "fabric/editor/session_transition.hpp"
#include "fabric/editor/autosave_scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string>

namespace {

class ValueCommand final : public fabric::editor::Command {
public:
    ValueCommand(int& target, const int next, std::string merge_key = {})
        : target_(target), before_(target), after_(next),
          merge_key_(std::move(merge_key)) {}

    bool execute() override {
        target_ = after_;
        return true;
    }

    bool undo() override {
        target_ = before_;
        return true;
    }

    bool merge_with(const Command& newer) override {
        const auto* value = dynamic_cast<const ValueCommand*>(&newer);
        if (value == nullptr || merge_key_.empty() ||
            merge_key_ != value->merge_key_ || &target_ != &value->target_) {
            return false;
        }
        after_ = value->after_;
        return true;
    }

private:
    int& target_;
    int before_;
    int after_;
    std::string merge_key_;
};

class FailingCommand final : public fabric::editor::Command {
public:
    bool execute() override { return false; }
    bool undo() override { return false; }
};

} // namespace

TEST_CASE("command stack executes undo and redo") {
    int value = 1;
    fabric::editor::CommandStack commands;

    REQUIRE(commands.execute(std::make_unique<ValueCommand>(value, 2)));
    CHECK(value == 2);
    CHECK(commands.dirty());
    CHECK(commands.can_undo());
    CHECK_FALSE(commands.can_redo());

    REQUIRE(commands.undo());
    CHECK(value == 1);
    CHECK_FALSE(commands.dirty());
    REQUIRE(commands.redo());
    CHECK(value == 2);
    CHECK(commands.dirty());
}

TEST_CASE("failed commands leave history unchanged") {
    fabric::editor::CommandStack commands;

    CHECK_FALSE(commands.execute(std::make_unique<FailingCommand>()));
    CHECK(commands.size() == 0);
    CHECK_FALSE(commands.dirty());
    CHECK_FALSE(commands.can_undo());
}

TEST_CASE("continuous commands merge before undo") {
    int value = 0;
    fabric::editor::CommandStack commands;

    REQUIRE(commands.execute(
        std::make_unique<ValueCommand>(value, 1, "inspector.position.x")));
    REQUIRE(commands.execute(
        std::make_unique<ValueCommand>(value, 2, "inspector.position.x")));
    REQUIRE(commands.execute(
        std::make_unique<ValueCommand>(value, 3, "inspector.position.x")));

    CHECK(value == 3);
    CHECK(commands.size() == 1);
    REQUIRE(commands.undo());
    CHECK(value == 0);
    REQUIRE(commands.redo());
    CHECK(value == 3);
}

TEST_CASE("a clean boundary prevents merging across a save") {
    int value = 0;
    fabric::editor::CommandStack commands;
    REQUIRE(commands.execute(
        std::make_unique<ValueCommand>(value, 1, "property")));
    commands.mark_clean();

    REQUIRE(commands.execute(
        std::make_unique<ValueCommand>(value, 2, "property")));
    CHECK(commands.size() == 2);
    CHECK(commands.dirty());
    REQUIRE(commands.undo());
    CHECK(value == 1);
    CHECK_FALSE(commands.dirty());
}

TEST_CASE("a divergent edit discards redo and unreachable clean state") {
    int value = 0;
    fabric::editor::CommandStack commands;
    REQUIRE(commands.execute(std::make_unique<ValueCommand>(value, 1)));
    REQUIRE(commands.execute(std::make_unique<ValueCommand>(value, 2)));
    commands.mark_clean();
    REQUIRE(commands.undo());
    REQUIRE(commands.undo());
    CHECK(commands.dirty());

    REQUIRE(commands.execute(std::make_unique<ValueCommand>(value, 7)));
    CHECK(value == 7);
    CHECK(commands.size() == 1);
    CHECK(commands.dirty());
    CHECK_FALSE(commands.can_redo());
}

TEST_CASE("mark dirty represents recovered state without fake history") {
    fabric::editor::CommandStack commands;
    commands.mark_dirty();

    CHECK(commands.dirty());
    CHECK_FALSE(commands.can_undo());
    commands.mark_clean();
    CHECK_FALSE(commands.dirty());
}

TEST_CASE("session transitions require an explicit unsaved decision") {
    fabric::editor::SessionTransitionGuard guard;
    guard.request(fabric::editor::SessionAction::open_project, false);
    REQUIRE(guard.take_ready() ==
            fabric::editor::SessionAction::open_project);
    CHECK_FALSE(guard.pending());

    guard.request(fabric::editor::SessionAction::quit, true);
    REQUIRE(guard.confirmation_required());
    CHECK_FALSE(guard.take_ready().has_value());
    CHECK_FALSE(guard.resolve(fabric::editor::UnsavedDecision::save, false)
                    .has_value());
    CHECK(guard.confirmation_required());
    CHECK(guard.resolve(fabric::editor::UnsavedDecision::save) ==
          fabric::editor::SessionAction::quit);

    guard.request(fabric::editor::SessionAction::create_project, true);
    CHECK_FALSE(guard.resolve(fabric::editor::UnsavedDecision::cancel)
                    .has_value());
    CHECK_FALSE(guard.pending());

    guard.request(fabric::editor::SessionAction::open_project, true);
    CHECK(guard.resolve(fabric::editor::UnsavedDecision::discard) ==
          fabric::editor::SessionAction::open_project);
}

TEST_CASE("failed transition saves keep every requested action recoverable") {
    using fabric::editor::SessionAction;
    using fabric::editor::UnsavedDecision;

    for (const auto action : {SessionAction::create_project,
                              SessionAction::open_project,
                              SessionAction::quit}) {
        fabric::editor::SessionTransitionGuard guard;
        guard.request(action, true);
        CHECK(guard.pending());
        CHECK(guard.confirmation_required());
        CHECK_FALSE(guard.resolve(UnsavedDecision::save, false).has_value());
        CHECK(guard.pending());
        CHECK(guard.confirmation_required());
        CHECK(guard.resolve(UnsavedDecision::discard) == action);
        CHECK_FALSE(guard.pending());
    }
}

TEST_CASE("clean transitions release create open and quit immediately") {
    using fabric::editor::SessionAction;
    for (const auto action : {SessionAction::create_project,
                              SessionAction::open_project,
                              SessionAction::quit}) {
        fabric::editor::SessionTransitionGuard guard;
        guard.request(action, false);
        CHECK_FALSE(guard.confirmation_required());
        CHECK(guard.take_ready() == action);
        CHECK_FALSE(guard.pending());
    }
}

TEST_CASE("every project transition handles clean and dirty outcomes") {
    using fabric::editor::SessionAction;
    using fabric::editor::UnsavedDecision;
    constexpr auto actions = std::array{
        SessionAction::open_project,
        SessionAction::create_project,
        SessionAction::quit};

    for (const auto action : actions) {
        fabric::editor::SessionTransitionGuard clean;
        clean.request(action, false);
        CHECK(clean.take_ready() == action);

        fabric::editor::SessionTransitionGuard dirty;
        dirty.request(action, true);
        CHECK(dirty.confirmation_required());
        CHECK(dirty.resolve(UnsavedDecision::save, true) == action);

        fabric::editor::SessionTransitionGuard failed_save;
        failed_save.request(action, true);
        CHECK_FALSE(failed_save.resolve(UnsavedDecision::save, false)
                        .has_value());
        CHECK(failed_save.pending());
        CHECK(failed_save.resolve(UnsavedDecision::cancel).has_value() == false);

        failed_save.request(action, true);
        CHECK(failed_save.resolve(UnsavedDecision::discard) == action);
    }
}

TEST_CASE("autosave becomes due after two seconds of inactivity") {
    using namespace std::chrono_literals;
    fabric::editor::AutosaveScheduler scheduler;
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};

    scheduler.mark_changed(start);
    CHECK(scheduler.pending());
    CHECK_FALSE(scheduler.due(start + 1999ms));
    CHECK(scheduler.due(start + 2s));
    scheduler.mark_saved();
    CHECK_FALSE(scheduler.pending());
    CHECK_FALSE(scheduler.due(start + 1h));
}

TEST_CASE("continuous changes cannot postpone autosave past thirty seconds") {
    using namespace std::chrono_literals;
    fabric::editor::AutosaveScheduler scheduler;
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};

    scheduler.mark_changed(start);
    for (int second = 1; second < 30; ++second) {
        scheduler.mark_changed(start + std::chrono::seconds{second});
        CHECK_FALSE(scheduler.due(
            start + std::chrono::seconds{second}));
    }
    CHECK(scheduler.due(start + 30s));
}

TEST_CASE("editor context restores document workspace and stable selection") {
    using fabric::core::ResourceId;
    using fabric::editor::EditorContext;
    using fabric::editor::EditorWorkspace;

    EditorContext context;
    REQUIRE(context.open_document(ResourceId{.value = "hero-entity"},
                                  EditorWorkspace::entity));
    REQUIRE(context.set_selection(ResourceId{.value = "body-component"}));
    REQUIRE(context.open_document(ResourceId{.value = "walk-animation"},
                                  EditorWorkspace::animation));
    REQUIRE(context.set_selection(ResourceId{.value = "left-foot-track"}));

    REQUIRE(context.go_back());
    REQUIRE(context.active_document() != nullptr);
    CHECK(context.active_document()->id.value == "hero-entity");
    CHECK(context.active_document()->workspace == EditorWorkspace::entity);
    REQUIRE(context.active_document()->selection_id.has_value());
    CHECK(context.active_document()->selection_id->value == "body-component");

    REQUIRE(context.go_forward());
    REQUIRE(context.active_document() != nullptr);
    CHECK(context.active_document()->id.value == "walk-animation");
    CHECK(context.active_document()->workspace == EditorWorkspace::animation);
    REQUIRE(context.active_document()->selection_id.has_value());
    CHECK(context.active_document()->selection_id->value == "left-foot-track");
    CHECK(context.open_documents().size() == 2);
}

TEST_CASE("editor context discards forward history after new navigation") {
    using fabric::core::ResourceId;
    using fabric::editor::EditorContext;
    using fabric::editor::EditorWorkspace;

    EditorContext context;
    REQUIRE(context.open_document(ResourceId{.value = "entity-a"},
                                  EditorWorkspace::entity));
    REQUIRE(context.open_document(ResourceId{.value = "animation-a"},
                                  EditorWorkspace::animation));
    REQUIRE(context.open_document(ResourceId{.value = "behavior-a"},
                                  EditorWorkspace::logic));
    REQUIRE(context.go_back());
    REQUIRE(context.navigate(ResourceId{.value = "scene-a"},
                             EditorWorkspace::scene));

    CHECK_FALSE(context.can_go_forward());
    CHECK_FALSE(context.go_forward());
    REQUIRE(context.active_document() != nullptr);
    CHECK(context.active_document()->id.value == "scene-a");
}

TEST_CASE("editor context preserves selection and active document identity") {
    using fabric::core::ResourceId;
    using fabric::editor::EditorContext;
    using fabric::editor::EditorWorkspace;

    EditorContext context;
    REQUIRE(context.open_document(ResourceId{.value = "entity-a"},
                                  EditorWorkspace::entity));
    REQUIRE(context.set_selection(ResourceId{.value = "component-a"}));
    REQUIRE(context.open_document(ResourceId{.value = "entity-b"},
                                  EditorWorkspace::entity));
    REQUIRE(context.open_document(ResourceId{.value = "entity-a"},
                                  EditorWorkspace::animation));

    REQUIRE(context.active_document() != nullptr);
    REQUIRE(context.active_document()->selection_id.has_value());
    CHECK(context.active_document()->selection_id->value == "component-a");
    REQUIRE(context.close_document(ResourceId{.value = "entity-b"}));
    REQUIRE(context.active_document() != nullptr);
    CHECK(context.active_document()->id.value == "entity-a");
    REQUIRE(context.active_document()->selection_id.has_value());
    CHECK(context.active_document()->selection_id->value == "component-a");
}

TEST_CASE("editor context rejects invalid transient identifiers") {
    fabric::editor::EditorContext context;

    CHECK_FALSE(context.open_document(
        fabric::core::ResourceId{.value = "invalid id"}));
    CHECK(context.active_document() == nullptr);
    CHECK(context.open_documents().empty());
}

TEST_CASE("editor action registry exposes availability and disabled reason") {
    using fabric::editor::EditorActionAvailability;
    using fabric::editor::EditorActionDefinition;
    using fabric::editor::EditorActionInvocation;
    using fabric::editor::EditorActionRegistry;

    bool dirty = false;
    int saves = 0;
    EditorActionRegistry actions;
    REQUIRE(actions.register_action(EditorActionDefinition{
        .id = "save",
        .label = "Enregistrer",
        .shortcut = "Cmd+S",
        .availability = [&dirty] {
            return EditorActionAvailability{
                .enabled = dirty,
                .disabled_reason = dirty ? "" : "Aucune modification",
            };
        },
        .execute = [&saves] {
            ++saves;
            return true;
        },
    }));

    CHECK_FALSE(actions.availability("save").enabled);
    CHECK(actions.availability("save").disabled_reason ==
          "Aucune modification");
    CHECK(actions.invoke("save") == EditorActionInvocation::disabled);
    CHECK(saves == 0);

    dirty = true;
    CHECK(actions.availability("save").enabled);
    CHECK(actions.availability("save").disabled_reason.empty());
    CHECK(actions.invoke("save") == EditorActionInvocation::invoked);
    CHECK(saves == 1);
}

TEST_CASE("editor action registry rejects duplicates and reports unknown ids") {
    using fabric::editor::EditorActionDefinition;
    using fabric::editor::EditorActionInvocation;
    fabric::editor::EditorActionRegistry actions;
    const auto action = [] { return true; };

    REQUIRE(actions.register_action(EditorActionDefinition{
        .id = "preview",
        .label = "Preview",
        .execute = action,
    }));
    CHECK_FALSE(actions.register_action(EditorActionDefinition{
        .id = "preview",
        .label = "Duplicate",
        .execute = action,
    }));
    CHECK(actions.invoke("missing") == EditorActionInvocation::unknown);
    CHECK_FALSE(actions.availability("missing").enabled);
    CHECK_FALSE(actions.availability("missing").disabled_reason.empty());
}
