#include "fabric/editor/mechanic_session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {

fabric::project::ProjectManifest manifest() {
    return {.schema_version = fabric::project::current_schema_version,
            .id = {.value = "mechanic-session"},
            .name = "Mechanic Session"};
}

fabric::project::MapDocument map() {
    fabric::project::MapDocument result;
    result.document.id = {.value = "mechanic-map"};
    result.document.name = "Mechanic Map";
    result.events = {{{.value = "platform-start"}, {}}};
    return result;
}

fabric::project::MechanicGraph empty_graph() {
    fabric::project::MechanicGraph result;
    result.document.id = {.value = "studio-mechanic"};
    result.document.name = "Studio Mechanic";
    return result;
}

std::filesystem::path temporary_root() {
    return std::filesystem::temp_directory_path() /
        ("fabric-mechanic-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
}

void author_rotating_body(fabric::editor::MechanicSession& session) {
    using Kind = fabric::project::MechanicNodeKind;
    REQUIRE(session.add_node(Kind::body, "platform"));
    REQUIRE(session.set_node_property({.value = "platform"}, "size",
                                      fabric::core::Vec2{4.0F, 0.5F}));
    REQUIRE(session.add_node(Kind::pivot, "anchor"));
    REQUIRE(session.set_node_property({.value = "anchor"}, "position",
                                      fabric::core::Vec2{}));
    REQUIRE(session.connect({"platform", "body", "anchor", "body"}));
    REQUIRE(session.add_node(Kind::joint, "hinge"));
    REQUIRE(session.connect({"platform", "body", "hinge", "body-a"}));
    REQUIRE(session.connect({"anchor", "pivot", "hinge", "pivot"}));
    REQUIRE(session.add_node(Kind::motor, "drive"));
    REQUIRE(session.set_node_property({.value = "drive"}, "speed", 90.0F));
    REQUIRE(session.set_node_property({.value = "drive"}, "max-torque", 100.0F));
    REQUIRE(session.connect({"hinge", "joint", "drive", "joint"}));
}

} // namespace

TEST_CASE("mechanic session authors, inspects and simulates a graph undoably") {
    const auto root = temporary_root();
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MechanicSession session;
    REQUIRE(session.create(root, map(), empty_graph()));
    author_rotating_body(session);
    REQUIRE(session.preview_errors().empty());
    REQUIRE(session.simulation().valid());
    REQUIRE_FALSE(session.simulation().playing());
    REQUIRE(session.step_once());
    CHECK(session.simulation().step_count() == 1U);
    REQUIRE(session.simulation().body_states().size() == 1U);
    session.play();
    REQUIRE(session.update_preview(1.0F / 30.0F));
    CHECK(session.simulation().step_count() == 3U);
    session.pause();
    REQUIRE(session.reset_preview());
    CHECK(session.simulation().step_count() == 0U);

    REQUIRE(session.undo());
    CHECK_FALSE(session.preview_errors().empty());
    REQUIRE(session.redo());
    CHECK(session.preview_errors().empty());
    REQUIRE(session.save());

    fabric::editor::MechanicSession reopened;
    REQUIRE(reopened.open(root, map(), {.value = "studio-mechanic"}));
    CHECK(reopened.graph()->nodes.size() == 4U);
    CHECK(reopened.graph()->connections.size() == 4U);
    CHECK(reopened.simulation().valid());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("mechanic session autosave recovery never replaces the main graph") {
    using namespace std::chrono_literals;
    const auto root = temporary_root();
    REQUIRE(fabric::project::create_project(root, manifest()).ok());
    fabric::editor::MechanicSession session;
    REQUIRE(session.create(root, map(), empty_graph()));
    REQUIRE(session.add_node(fabric::project::MechanicNodeKind::body, "platform"));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    CHECK(session.update_autosave(start) == fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + 2s) == fabric::editor::AutosaveStatus::saved);

    fabric::editor::MechanicSession reopened;
    REQUIRE(reopened.open(root, map(), {.value = "studio-mechanic"}));
    REQUIRE(reopened.has_recovery());
    CHECK(reopened.graph()->nodes.empty());
    REQUIRE(reopened.accept_recovery(start + 3s));
    CHECK(reopened.graph()->nodes.size() == 1U);
    CHECK(reopened.dirty());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
