#include "fabric/project/document_storage.hpp"

#include "asset_storage.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fabric::project {
namespace {

constexpr std::uintmax_t maximum_document_bytes = 268'435'456;

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

StoredDocumentResult load_document_from_relative_path(
    const std::filesystem::path& project_root,
    const std::filesystem::path& relative_path,
    const DocumentValidator& validator) {
    StoredDocumentResult result;
    if (!validator) {
        add_error(result.errors, ErrorCode::invalid_json, "document",
                  "a document validator is required");
        return result;
    }
    if (!detail::is_portable_relative_path(relative_path)) {
        add_error(result.errors, ErrorCode::invalid_path, "document",
                  "document path must be portable and project-relative");
        return result;
    }

    std::error_code filesystem_error;
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error ||
        !std::filesystem::is_directory(canonical_root, filesystem_error)) {
        add_error(result.errors, ErrorCode::missing_directory, "project",
                  "project root is not an accessible directory");
        return result;
    }
    filesystem_error.clear();
    const auto canonical_document = std::filesystem::weakly_canonical(
        project_root / relative_path, filesystem_error);
    if (filesystem_error ||
        !detail::is_within(canonical_root, canonical_document)) {
        add_error(result.errors, ErrorCode::invalid_path, "document",
                  "document must resolve inside the project");
        return result;
    }
    if (!std::filesystem::is_regular_file(canonical_document,
                                          filesystem_error)) {
        add_error(result.errors, ErrorCode::missing_file, "document",
                  "document is not a readable regular file");
        return result;
    }
    const auto size = std::filesystem::file_size(canonical_document,
                                                 filesystem_error);
    if (filesystem_error || size > maximum_document_bytes) {
        add_error(result.errors, ErrorCode::io_error, "document",
                  "document exceeds the storage safety limit");
        return result;
    }

    std::ifstream input(canonical_document, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::io_error, "document",
                  "cannot open the document");
        return result;
    }
    std::string contents{std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        add_error(result.errors, ErrorCode::io_error, "document",
                  "failed while reading the document");
        return result;
    }
    auto validation = validator(contents);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.contents = std::move(contents);
    return result;
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
    if (serialized_document.size() > maximum_document_bytes) {
        add_error(report.errors, ErrorCode::io_error, "document",
                  "document exceeds the storage safety limit");
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

StoredDocumentResult load_document(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    const DocumentValidator& validator) {
    return load_document_from_relative_path(project_root, document_path,
                                            validator);
}

std::filesystem::path autosave_document_path(
    const std::filesystem::path& document_path) {
    return std::filesystem::path{".vertex-loom"} / "autosave" /
           document_path;
}

ValidationReport save_autosave_atomic(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    const std::string_view serialized_document,
    const DocumentValidator& validator) {
    ValidationReport report;
    if (!detail::is_portable_relative_path(document_path)) {
        add_error(report.errors, ErrorCode::invalid_path, "document",
                  "autosaved document path must be project-relative");
        return report;
    }
    return save_document_atomic(project_root,
                                autosave_document_path(document_path),
                                serialized_document, validator);
}

RecoveryResult inspect_recovery(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    const DocumentValidator& validator) {
    RecoveryResult result;
    auto primary = load_document(project_root, document_path, validator);
    if (!primary.ok()) {
        result.errors = std::move(primary.errors);
        return result;
    }

    const auto autosave_path = autosave_document_path(document_path);
    std::error_code filesystem_error;
    const bool autosave_exists = std::filesystem::exists(
        project_root / autosave_path, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "autosave",
                  "cannot inspect the autosave");
        return result;
    }
    if (!autosave_exists) {
        return result;
    }

    auto autosave = load_document_from_relative_path(
        project_root, autosave_path, validator);
    if (!autosave.ok()) {
        result.errors = std::move(autosave.errors);
        return result;
    }
    const auto primary_time = std::filesystem::last_write_time(
        project_root / document_path, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "document",
                  "cannot inspect the document modification time");
        return result;
    }
    const auto autosave_time = std::filesystem::last_write_time(
        project_root / autosave_path, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "autosave",
                  "cannot inspect the autosave modification time");
        return result;
    }
    if (autosave_time > primary_time) {
        result.candidate = RecoveryCandidate{
            .document_path = document_path,
            .autosave_path = autosave_path,
            .contents = std::move(*autosave.contents),
        };
    }
    return result;
}

} // namespace fabric::project
