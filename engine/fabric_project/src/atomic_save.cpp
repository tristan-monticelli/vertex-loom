#include "fabric/project/manifest.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fabric::project {
namespace {

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

std::filesystem::path temporary_manifest_path(
    const std::filesystem::path& project_root) {
    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
    return project_root /
           (".project.json." + std::to_string(timestamp) + "." +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) +
            ".tmp");
}

bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace

ValidationReport save_manifest_atomic(const std::filesystem::path& project_root,
                                      const ProjectManifest& manifest) {
    ValidationReport report = validate_manifest(manifest);
    if (!report.ok()) {
        return report;
    }

    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(project_root, filesystem_error)) {
        add_error(report.errors, ErrorCode::missing_directory, "project",
                  "project root is not an accessible directory");
        return report;
    }

    const auto temporary_path = temporary_manifest_path(project_root);
    {
        std::ofstream output(temporary_path,
                             std::ios::binary | std::ios::trunc);
        if (!output) {
            add_error(report.errors, ErrorCode::io_error, "project.json",
                      "cannot create temporary manifest");
            return report;
        }
        output << serialize_manifest(manifest);
        output.flush();
        if (!output) {
            add_error(report.errors, ErrorCode::io_error, "project.json",
                      "failed while writing temporary manifest");
        }
    }

    if (report.ok() &&
        !replace_file(temporary_path, project_root / "project.json",
                      filesystem_error)) {
        add_error(report.errors, ErrorCode::io_error, "project.json",
                  "cannot atomically replace project manifest");
    }
    if (!report.ok()) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
    }
    return report;
}

} // namespace fabric::project
