#include "fabric/editor/behavior_session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {
fabric::project::ProjectManifest manifest() {
    fabric::project::ProjectManifest value;
    value.id = {.value = "behavior-session"};
    value.name = "Behavior Session";
    return value;
}

fabric::project::BehaviorGraph graph() {
    fabric::project::BehaviorGraph value;
    value.document.id = {.value = "actor-logic"};
    value.document.name = "Actor Logic";
    return value;
}

std::filesystem::path temporary_root() {
    return std::filesystem::temp_directory_path() /
        ("fabric-behavior-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}
}

TEST_CASE("behavior session authors previews saves and reloads a graph") {
    const auto root = temporary_root();
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::BehaviorSession session;
    REQUIRE(session.create(root, graph()));
    REQUIRE(session.add_node("action_source", "player-input"));
    REQUIRE(session.set_node_property({.value = "player-input"}, "semantic_id",
                                      std::string{"advance"}));
    REQUIRE(session.add_node("move", "advance-actor"));
    REQUIRE(session.connect({.id = "advance", .from_node = "player-input",
                             .from_port = "out", .to_node = "advance-actor",
                             .to_port = "in"}));
    const auto actions = session.preview(
        {fabric::runtime::BehaviorSignalSource::action, "advance", {}},
        1.0F / 60.0F);
    REQUIRE(actions.size() == 1U);
    CHECK(actions.front().kind == fabric::runtime::BehaviorActionKind::move);
    CHECK_FALSE(session.trace().empty());
    REQUIRE(session.undo());
    CHECK(session.graph()->connections.empty());
    REQUIRE(session.redo());
    REQUIRE(session.save());

    fabric::editor::BehaviorSession reopened;
    REQUIRE(reopened.open(root, {.value = "actor-logic"}));
    CHECK(reopened.graph()->nodes.size() == 2U);
    CHECK(reopened.graph()->connections.size() == 1U);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("behavior autosave recovery preserves the main document") {
    using namespace std::chrono_literals;
    const auto root = temporary_root();
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::BehaviorSession session;
    REQUIRE(session.create(root, graph()));
    REQUIRE(session.add_node("ai_source", "monster-ai"));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    CHECK(session.update_autosave(start) == fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + 2s) == fabric::editor::AutosaveStatus::saved);

    fabric::editor::BehaviorSession reopened;
    REQUIRE(reopened.open(root, {.value = "actor-logic"}));
    REQUIRE(reopened.has_recovery());
    CHECK(reopened.graph()->nodes.empty());
    REQUIRE(reopened.accept_recovery(start + 3s));
    CHECK(reopened.graph()->nodes.size() == 1U);
    CHECK(reopened.dirty());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("behavior duplication is undoable and keeps stable ids") {
    const auto root = temporary_root();
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::BehaviorSession session;
    REQUIRE(session.create(root, graph()));
    REQUIRE(session.add_node("event_source", "event"));
    REQUIRE(session.duplicate_node({.value = "event"}, "event-copy"));
    REQUIRE(session.graph()->nodes.size() == 2U);
    CHECK(session.graph()->nodes[0].ports == session.graph()->nodes[1].ports);
    REQUIRE(session.undo());
    CHECK(session.graph()->nodes.size() == 1U);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
