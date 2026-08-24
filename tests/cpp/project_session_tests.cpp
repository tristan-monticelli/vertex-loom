#include "fabric/editor/project_session.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string_view suffix) {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("fabric-editor-test-" + std::to_string(unique) + "-" +
                 std::string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_valid_project(const std::filesystem::path& root) {
    for (const auto* directory : {"assets", "entities", "maps", "scenes", "schemas"}) {
        std::filesystem::create_directory(root / directory);
    }
    const fabric::project::ProjectManifest manifest{
        .schema_version = fabric::project::current_schema_version,
        .id = {.value = "studio-project"},
        .name = "Studio Project",
        .directories = {},
    };
    std::ofstream output(root / "project.json", std::ios::binary);
    output << fabric::project::serialize_manifest(manifest);
}

void session_opens_a_valid_project() {
    const TemporaryDirectory valid{"valid"};
    write_valid_project(valid.path());

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "valid project did not open");
    require(session.has_project(), "session did not retain the project");
    require(session.manifest()->name == "Studio Project",
            "session retained the wrong manifest");
    require(session.errors().empty(), "successful open retained errors");
}

void failed_open_preserves_the_active_project() {
    const TemporaryDirectory valid{"valid"};
    const TemporaryDirectory invalid{"invalid"};
    write_valid_project(valid.path());

    fabric::editor::ProjectSession session;
    require(session.open(valid.path()), "valid project did not open");
    require(!session.open(invalid.path()), "invalid project opened");
    require(session.has_project(), "failed open cleared the active project");
    require(session.project_root() == valid.path(),
            "failed open replaced the active project path");
    require(!session.errors().empty(), "failed open produced no diagnostics");
}

} // namespace

int main() {
    session_opens_a_valid_project();
    failed_open_preserves_the_active_project();
    return 0;
}
