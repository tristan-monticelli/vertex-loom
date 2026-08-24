#include "fabric/editor/project_session.hpp"
#include "fabric/project/document_storage.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("fabric-session-editing-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_project(const std::filesystem::path& root) {
    for (const auto* directory :
         {"assets", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directory(root / directory);
    }
    const fabric::project::ProjectManifest manifest{
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "editing-project"},
        .name = "Primary",
        .directories = {},
    };
    std::ofstream output(root / "project.json", std::ios::binary);
    output << fabric::project::serialize_manifest(manifest);
}

fabric::project::ProjectManifest load_manifest_or_fail(
    const std::filesystem::path& root) {
    auto loaded = fabric::project::load_manifest(root);
    REQUIRE(loaded.ok());
    return std::move(*loaded.manifest);
}

} // namespace

TEST_CASE("project manifest edits use command history and explicit save") {
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};

    REQUIRE(session.set_pixels_per_unit(64.0, start));
    CHECK(session.manifest()->pixels_per_unit == 64.0);
    CHECK(session.dirty());
    CHECK(session.can_undo());
    CHECK(load_manifest_or_fail(project.path()).pixels_per_unit == 100.0);
    CHECK_FALSE(session.set_pixels_per_unit(
        std::numeric_limits<double>::quiet_NaN(), start));
    CHECK(session.manifest()->pixels_per_unit == 64.0);

    REQUIRE(session.undo(start + std::chrono::milliseconds{1}));
    CHECK(session.manifest()->pixels_per_unit == 100.0);
    CHECK_FALSE(session.dirty());
    REQUIRE(session.redo(start + std::chrono::milliseconds{2}));
    CHECK(session.manifest()->pixels_per_unit == 64.0);
    CHECK(session.dirty());

    REQUIRE(session.save());
    CHECK_FALSE(session.dirty());
    CHECK(load_manifest_or_fail(project.path()).pixels_per_unit == 64.0);
}

TEST_CASE("undoing to clean neutralizes a previous autosave") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    REQUIRE(session.set_project_name("Transient", start));
    REQUIRE(session.update_autosave(start + 2s) ==
            fabric::editor::AutosaveStatus::saved);
    REQUIRE(session.undo(start + 3s));
    CHECK_FALSE(session.dirty());
    REQUIRE(session.update_autosave(start + 3s) ==
            fabric::editor::AutosaveStatus::saved);
    std::filesystem::last_write_time(
        project.path() / "project.json",
        std::filesystem::file_time_type::clock::now() - 5s);

    fabric::editor::ProjectSession reopened;
    REQUIRE(reopened.open(project.path()));
    CHECK_FALSE(reopened.has_recovery());
    CHECK(reopened.manifest()->name == "Primary");
}

TEST_CASE("autosave follows inactivity and leaves primary untouched") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};

    REQUIRE(session.set_project_name("Recovered", start));
    CHECK(session.update_autosave(start + 1999ms) ==
          fabric::editor::AutosaveStatus::not_due);
    CHECK(session.update_autosave(start + 2s) ==
          fabric::editor::AutosaveStatus::saved);

    CHECK(load_manifest_or_fail(project.path()).name == "Primary");
    const auto autosave_root = project.path() / ".vertex-loom/autosave";
    auto autosaved = fabric::project::load_manifest(autosave_root);
    REQUIRE(autosaved.ok());
    CHECK(autosaved.manifest->name == "Recovered");
    CHECK(session.dirty());
}

TEST_CASE("crash recovery can be declined or accepted without overwriting") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    write_project(project.path());
    const fabric::editor::AutosaveScheduler::Clock::time_point start{};
    {
        fabric::editor::ProjectSession interrupted;
        REQUIRE(interrupted.open(project.path()));
        REQUIRE(interrupted.set_project_name("Recovered", start));
        REQUIRE(interrupted.update_autosave(start + 2s) ==
                fabric::editor::AutosaveStatus::saved);
    }
    std::filesystem::last_write_time(
        project.path() / "project.json",
        std::filesystem::file_time_type::clock::now() - 5s);

    fabric::editor::ProjectSession declined;
    REQUIRE(declined.open(project.path()));
    REQUIRE(declined.has_recovery());
    declined.decline_recovery();
    CHECK(declined.manifest()->name == "Primary");
    CHECK_FALSE(declined.dirty());
    CHECK(load_manifest_or_fail(project.path()).name == "Primary");

    fabric::editor::ProjectSession accepted;
    REQUIRE(accepted.open(project.path()));
    REQUIRE(accepted.has_recovery());
    REQUIRE(accepted.accept_recovery(start));
    CHECK(accepted.manifest()->name == "Recovered");
    CHECK(accepted.dirty());
    CHECK_FALSE(accepted.can_undo());
    CHECK(load_manifest_or_fail(project.path()).name == "Primary");

    REQUIRE(accepted.save());
    CHECK(load_manifest_or_fail(project.path()).name == "Recovered");
    CHECK_FALSE(accepted.dirty());
}

TEST_CASE("invalid autosave is diagnosed and never offered") {
    const TemporaryDirectory project;
    write_project(project.path());
    const auto autosave = project.path() / ".vertex-loom/autosave/project.json";
    std::filesystem::create_directories(autosave.parent_path());
    {
        std::ofstream output(autosave, std::ios::binary);
        output << "{not-json";
    }
    std::filesystem::last_write_time(
        project.path() / "project.json",
        std::filesystem::file_time_type::clock::now() -
            std::chrono::seconds{5});

    fabric::editor::ProjectSession session;
    REQUIRE(session.open(project.path()));
    CHECK_FALSE(session.has_recovery());
    CHECK(session.manifest()->name == "Primary");
    CHECK_FALSE(session.errors().empty());
}
