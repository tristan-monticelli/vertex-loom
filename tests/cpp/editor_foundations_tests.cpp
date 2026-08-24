#include "fabric/editor/command_stack.hpp"
#include "fabric/editor/autosave_scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

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
