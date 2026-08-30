#pragma once

#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

using DocumentValidator =
    std::function<ValidationReport(std::string_view serialized_document)>;

struct StoredDocumentResult {
    std::optional<std::string> contents;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return contents.has_value() && errors.empty();
    }
};

struct RecoveryCandidate {
    std::filesystem::path document_path;
    std::filesystem::path autosave_path;
    std::string contents;
};

struct RecoveryResult {
    std::optional<RecoveryCandidate> candidate;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] ValidationReport save_document_atomic(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    std::string_view serialized_document,
    const DocumentValidator& validator);
[[nodiscard]] StoredDocumentResult load_document(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    const DocumentValidator& validator);
[[nodiscard]] ValidationReport rename_document_display_name(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    std::string name,
    const DocumentValidator& validator);
[[nodiscard]] std::filesystem::path autosave_document_path(
    const std::filesystem::path& document_path);
[[nodiscard]] ValidationReport save_autosave_atomic(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    std::string_view serialized_document,
    const DocumentValidator& validator);
[[nodiscard]] RecoveryResult inspect_recovery(
    const std::filesystem::path& project_root,
    const std::filesystem::path& document_path,
    const DocumentValidator& validator);

} // namespace fabric::project
