#include "fabric/project/document_storage.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("fabric-document-storage-" + std::to_string(unique));
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

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

fabric::project::ValidationReport accept_expected(
    const std::string_view contents) {
    fabric::project::ValidationReport report;
    if (contents != "valid\n") {
        report.errors.push_back({fabric::project::ErrorCode::invalid_json,
                                 "document", "unexpected test document"});
    }
    return report;
}

fabric::project::ValidationReport accept_recovery_document(
    const std::string_view contents) {
    fabric::project::ValidationReport report;
    if (contents != "primary\n" && contents != "recovered\n") {
        report.errors.push_back({fabric::project::ErrorCode::invalid_json,
                                 "document", "invalid recovery document"});
    }
    return report;
}

} // namespace

TEST_CASE("editable documents replace atomically") {
    const TemporaryDirectory project;
    std::filesystem::create_directories(project.path() / "entities");
    {
        std::ofstream output(project.path() / "entities/hero.json");
        output << "old\n";
    }

    const auto report = fabric::project::save_document_atomic(
        project.path(), "entities/hero.json", "valid\n", accept_expected);

    REQUIRE(report.ok());
    CHECK(read_text(project.path() / "entities/hero.json") == "valid\n");
    for (const auto& entry :
         std::filesystem::directory_iterator(project.path() / "entities")) {
        CHECK(entry.path().extension() != ".tmp");
    }
}

TEST_CASE("validation failure preserves the previous document") {
    const TemporaryDirectory project;
    std::filesystem::create_directories(project.path() / "entities");
    {
        std::ofstream output(project.path() / "entities/hero.json");
        output << "old\n";
    }

    const auto report = fabric::project::save_document_atomic(
        project.path(), "entities/hero.json", "invalid\n", accept_expected);

    CHECK_FALSE(report.ok());
    CHECK(read_text(project.path() / "entities/hero.json") == "old\n");
}

TEST_CASE("document storage creates safe nested parents") {
    const TemporaryDirectory project;

    const auto report = fabric::project::save_document_atomic(
        project.path(), "maps/chapter/intro.json", "valid\n", accept_expected);

    REQUIRE(report.ok());
    CHECK(read_text(project.path() / "maps/chapter/intro.json") == "valid\n");
}

TEST_CASE("document storage rejects traversal and missing validators") {
    const TemporaryDirectory project;

    const auto traversal = fabric::project::save_document_atomic(
        project.path(), "../outside.json", "valid\n", accept_expected);
    const auto no_validator = fabric::project::save_document_atomic(
        project.path(), "entities/hero.json", "valid\n", {});

    CHECK_FALSE(traversal.ok());
    CHECK_FALSE(no_validator.ok());
    CHECK_FALSE(std::filesystem::exists(project.path().parent_path() /
                                        "outside.json"));
}

TEST_CASE("document storage does not follow a parent symlink outside project") {
    const TemporaryDirectory project;
    const TemporaryDirectory outside;
    std::error_code error;
    std::filesystem::create_directory_symlink(
        outside.path(), project.path() / "linked", error);
    if (error) {
        SKIP("directory symlinks are unavailable in this environment");
    }

    const auto report = fabric::project::save_document_atomic(
        project.path(), "linked/escape.json", "valid\n", accept_expected);

    CHECK_FALSE(report.ok());
    CHECK_FALSE(std::filesystem::exists(outside.path() / "escape.json"));
}

TEST_CASE("autosaves mirror document paths and expose newer valid recovery") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    REQUIRE(fabric::project::save_document_atomic(
                project.path(), "entities/hero.json", "primary\n",
                accept_recovery_document)
                .ok());
    const auto primary_path = project.path() / "entities/hero.json";
    std::filesystem::last_write_time(
        primary_path, std::filesystem::file_time_type::clock::now() - 2s);

    REQUIRE(fabric::project::save_autosave_atomic(
                project.path(), "entities/hero.json", "recovered\n",
                accept_recovery_document)
                .ok());
    const auto expected_autosave = std::filesystem::path{".vertex-loom"} /
        "autosave/entities/hero.json";
    CHECK(fabric::project::autosave_document_path("entities/hero.json") ==
          expected_autosave);

    const auto recovery = fabric::project::inspect_recovery(
        project.path(), "entities/hero.json", accept_recovery_document);
    REQUIRE(recovery.ok());
    REQUIRE(recovery.candidate.has_value());
    CHECK(recovery.candidate->document_path == "entities/hero.json");
    CHECK(recovery.candidate->autosave_path == expected_autosave);
    CHECK(recovery.candidate->contents == "recovered\n");
    CHECK(read_text(primary_path) == "primary\n");
}

TEST_CASE("old or invalid autosaves are never recovery candidates") {
    using namespace std::chrono_literals;
    const TemporaryDirectory project;
    REQUIRE(fabric::project::save_document_atomic(
                project.path(), "maps/room.json", "primary\n",
                accept_recovery_document)
                .ok());
    REQUIRE(fabric::project::save_autosave_atomic(
                project.path(), "maps/room.json", "recovered\n",
                accept_recovery_document)
                .ok());
    const auto autosave = project.path() /
        fabric::project::autosave_document_path("maps/room.json");
    std::filesystem::last_write_time(
        autosave, std::filesystem::file_time_type::clock::now() - 2s);

    const auto old = fabric::project::inspect_recovery(
        project.path(), "maps/room.json", accept_recovery_document);
    REQUIRE(old.ok());
    CHECK_FALSE(old.candidate.has_value());

    {
        std::ofstream output(autosave, std::ios::binary | std::ios::trunc);
        output << "invalid\n";
    }
    const auto invalid = fabric::project::inspect_recovery(
        project.path(), "maps/room.json", accept_recovery_document);
    CHECK_FALSE(invalid.ok());
    CHECK_FALSE(invalid.candidate.has_value());
    CHECK(read_text(project.path() / "maps/room.json") == "primary\n");
}

TEST_CASE("newer autosave identical to primary needs no recovery") {
    const TemporaryDirectory project;
    REQUIRE(fabric::project::save_document_atomic(
                project.path(), "scenes/start.json", "primary\n",
                accept_recovery_document)
                .ok());
    REQUIRE(fabric::project::save_autosave_atomic(
                project.path(), "scenes/start.json", "primary\n",
                accept_recovery_document)
                .ok());

    const auto recovery = fabric::project::inspect_recovery(
        project.path(), "scenes/start.json", accept_recovery_document);
    REQUIRE(recovery.ok());
    CHECK_FALSE(recovery.candidate.has_value());
}

TEST_CASE("recovery rejects an autosave resolved outside the project") {
    const TemporaryDirectory project;
    const TemporaryDirectory outside;
    REQUIRE(fabric::project::save_document_atomic(
                project.path(), "entities/hero.json", "primary\n",
                accept_recovery_document)
                .ok());
    std::filesystem::create_directories(outside.path() / "autosave/entities");
    {
        std::ofstream output(outside.path() / "autosave/entities/hero.json");
        output << "recovered\n";
    }
    std::error_code error;
    std::filesystem::create_directory_symlink(
        outside.path(), project.path() / ".vertex-loom", error);
    if (error) {
        SKIP("directory symlinks are unavailable in this environment");
    }

    const auto recovery = fabric::project::inspect_recovery(
        project.path(), "entities/hero.json", accept_recovery_document);

    CHECK_FALSE(recovery.ok());
    CHECK_FALSE(recovery.candidate.has_value());
}
