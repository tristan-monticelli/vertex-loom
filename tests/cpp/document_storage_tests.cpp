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
