#include "fabric/project/document_storage.hpp"

#include "asset_storage.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>
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

std::filesystem::path temporary_path(
    const std::filesystem::path& destination) {
    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
    return destination.parent_path() /
           ("." + destination.filename().string() + "." +
            std::to_string(timestamp) + "." +
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

bool is_root_or_within(const std::filesystem::path& canonical_root,
                       const std::filesystem::path& candidate) {
    return candidate == canonical_root ||
           detail::is_within(canonical_root, candidate);
}

bool prepare_parent(const std::filesystem::path& canonical_root,
                    const std::filesystem::path& destination_parent,
                    std::error_code& error) {
    auto existing_ancestor = destination_parent;
    while (!std::filesystem::exists(existing_ancestor, error)) {
        if (error || existing_ancestor == existing_ancestor.parent_path()) {
            return false;
        }
        existing_ancestor = existing_ancestor.parent_path();
    }
    const auto canonical_ancestor = std::filesystem::weakly_canonical(
        existing_ancestor, error);
    if (error || !is_root_or_within(canonical_root, canonical_ancestor)) {
        return false;
    }

    std::filesystem::create_directories(destination_parent, error);
    if (error) {
        return false;
    }
    const auto canonical_parent = std::filesystem::weakly_canonical(
        destination_parent, error);
    return !error && is_root_or_within(canonical_root, canonical_parent);
}

} // namespace

ValidationReport save_document_atomic(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    const std::string_view serialized_document,
    const DocumentValidator& validator) {
    ValidationReport report;
    if (!validator) {
        add_error(report.errors, ErrorCode::invalid_json, "document",
                  "a document validator is required");
        return report;
    }
    report = validator(serialized_document);
    if (!report.ok()) {
        return report;
    }
    if (!detail::is_portable_relative_path(document_path)) {
        add_error(report.errors, ErrorCode::invalid_path, "document",
                  "document path must be portable and project-relative");
        return report;
    }

    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(project_root, filesystem_error)) {
        add_error(report.errors, ErrorCode::missing_directory, "project",
                  "project root is not an accessible directory");
        return report;
    }
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return report;
    }

    const auto destination = project_root / document_path;
    if (!prepare_parent(canonical_root, destination.parent_path(),
                        filesystem_error)) {
        add_error(report.errors, ErrorCode::invalid_path, "document",
                  "document parent must resolve inside the project");
        return report;
    }

    const auto temporary = temporary_path(destination);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            add_error(report.errors, ErrorCode::io_error, "document",
                      "cannot create the temporary document");
            return report;
        }
        output.write(serialized_document.data(),
                     static_cast<std::streamsize>(serialized_document.size()));
        output.flush();
        if (!output) {
            add_error(report.errors, ErrorCode::io_error, "document",
                      "failed while writing the temporary document");
        }
    }

    if (report.ok() &&
        !replace_file(temporary, destination, filesystem_error)) {
        add_error(report.errors, ErrorCode::io_error, "document",
                  "cannot atomically replace the document");
    }
    if (!report.ok()) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
    }
    return report;
}

} // namespace fabric::project
