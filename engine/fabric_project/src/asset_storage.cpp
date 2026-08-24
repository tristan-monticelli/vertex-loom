#include "asset_storage.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace fabric::project::detail {
namespace {

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

std::filesystem::path temporary_path(const std::filesystem::path& destination) {
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

bool publish_no_replace(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    if (::link(temporary.c_str(), destination.c_str()) != 0) {
        error = std::error_code(errno, std::generic_category());
        return false;
    }
    static_cast<void>(::unlink(temporary.c_str()));
    return true;
#endif
}

} // namespace

bool is_portable_relative_path(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    if (value.empty() || value == "." || path.is_absolute() ||
        value.starts_with('/') || value.starts_with('\\') ||
        (value.size() >= 2 && value[1] == ':') ||
        value.find('\\') != std::string::npos) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool is_within(const std::filesystem::path& root,
               const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative == "." || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

ValidationReport publish_asset_files(
    const std::filesystem::path& project_root,
    const std::filesystem::path& asset_directory,
    const std::filesystem::path& source_relative,
    const std::filesystem::path& document_relative,
    const std::filesystem::path& validated_source,
    const std::string_view serialized_document,
    const std::string_view asset_kind) {
    ValidationReport report;
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(validated_source, filesystem_error)) {
        add_error(report.errors, ErrorCode::missing_file, "source",
                  "validated source is not a readable file");
        return report;
    }

    const auto destination_directory = project_root / asset_directory;
    std::filesystem::create_directories(destination_directory, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, std::string(asset_kind),
                  "cannot create the asset directory");
        return report;
    }
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return report;
    }
    filesystem_error.clear();
    const auto canonical_directory = std::filesystem::weakly_canonical(
        destination_directory, filesystem_error);
    if (filesystem_error || !is_within(canonical_root, canonical_directory)) {
        add_error(report.errors, ErrorCode::invalid_path,
                  std::string(asset_kind),
                  "asset directory must resolve inside the project");
        return report;
    }

    const auto source_destination = project_root / source_relative;
    const auto document_destination = project_root / document_relative;
    const bool source_exists = std::filesystem::exists(
        source_destination, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, "source",
                  "cannot inspect the source destination");
        return report;
    }
    filesystem_error.clear();
    const bool document_exists = std::filesystem::exists(
        document_destination, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, std::string(asset_kind),
                  "cannot inspect the document destination");
        return report;
    }
    if (source_exists || document_exists) {
        add_error(report.errors, ErrorCode::asset_already_exists, "id",
                  std::string(asset_kind) + " identifier already exists");
        return report;
    }

    const auto source_temporary = temporary_path(source_destination);
    if (!std::filesystem::copy_file(validated_source, source_temporary,
                                    std::filesystem::copy_options::none,
                                    filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(source_temporary, cleanup_error);
        add_error(report.errors, ErrorCode::io_error, "source",
                  "cannot stage the asset source");
        return report;
    }
    const auto document_temporary = temporary_path(document_destination);
    {
        std::ofstream output(document_temporary,
                             std::ios::binary | std::ios::trunc);
        output << serialized_document;
        output.flush();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(source_temporary, cleanup_error);
            std::filesystem::remove(document_temporary, cleanup_error);
            add_error(report.errors, ErrorCode::io_error,
                      std::string(asset_kind),
                      "cannot stage the asset document");
            return report;
        }
    }
    if (!publish_no_replace(source_temporary, source_destination,
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(source_temporary, cleanup_error);
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(report.errors, ErrorCode::io_error, "source",
                  "cannot publish the asset source");
        return report;
    }
    if (!publish_no_replace(document_temporary, document_destination,
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(report.errors, ErrorCode::io_error,
                  std::string(asset_kind),
                  "cannot publish the asset document");
    }
    return report;
}

ValidationReport publish_asset_bundle(
    const std::filesystem::path& project_root,
    const std::filesystem::path& asset_directory,
    const std::filesystem::path& source_relative,
    const std::filesystem::path& generated_relative,
    const std::filesystem::path& document_relative,
    const std::filesystem::path& validated_source,
    const std::span<const std::uint8_t> generated_contents,
    const std::string_view serialized_document,
    const std::string_view asset_kind) {
    ValidationReport report;
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(validated_source, filesystem_error)) {
        add_error(report.errors, ErrorCode::missing_file, "source",
                  "validated source is not a readable file");
        return report;
    }
    if (generated_contents.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlas",
                  "generated asset content must not be empty");
        return report;
    }

    const auto destination_directory = project_root / asset_directory;
    std::filesystem::create_directories(destination_directory, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, std::string(asset_kind),
                  "cannot create the asset directory");
        return report;
    }
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(report.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return report;
    }
    filesystem_error.clear();
    const auto canonical_directory = std::filesystem::weakly_canonical(
        destination_directory, filesystem_error);
    if (filesystem_error || !is_within(canonical_root, canonical_directory)) {
        add_error(report.errors, ErrorCode::invalid_path,
                  std::string(asset_kind),
                  "asset directory must resolve inside the project");
        return report;
    }

    const std::array destinations{
        project_root / source_relative,
        project_root / generated_relative,
        project_root / document_relative,
    };
    for (const auto& destination : destinations) {
        filesystem_error.clear();
        if (std::filesystem::exists(destination, filesystem_error)) {
            add_error(report.errors, ErrorCode::asset_already_exists, "id",
                      std::string(asset_kind) + " identifier already exists");
            return report;
        }
        if (filesystem_error) {
            add_error(report.errors, ErrorCode::io_error,
                      std::string(asset_kind),
                      "cannot inspect an asset destination");
            return report;
        }
    }

    const auto source_temporary = temporary_path(destinations[0]);
    const auto generated_temporary = temporary_path(destinations[1]);
    const auto document_temporary = temporary_path(destinations[2]);
    if (!std::filesystem::copy_file(validated_source, source_temporary,
                                    std::filesystem::copy_options::none,
                                    filesystem_error)) {
        add_error(report.errors, ErrorCode::io_error, "source",
                  "cannot stage the asset source");
        return report;
    }
    {
        std::ofstream output(generated_temporary,
                             std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(generated_contents.data()),
            static_cast<std::streamsize>(generated_contents.size()));
        output.flush();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(source_temporary, cleanup_error);
            std::filesystem::remove(generated_temporary, cleanup_error);
            add_error(report.errors, ErrorCode::io_error, "atlas",
                      "cannot stage the generated asset");
            return report;
        }
    }
    {
        std::ofstream output(document_temporary,
                             std::ios::binary | std::ios::trunc);
        output << serialized_document;
        output.flush();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(source_temporary, cleanup_error);
            std::filesystem::remove(generated_temporary, cleanup_error);
            std::filesystem::remove(document_temporary, cleanup_error);
            add_error(report.errors, ErrorCode::io_error,
                      std::string(asset_kind),
                      "cannot stage the asset document");
            return report;
        }
    }

    if (!publish_no_replace(source_temporary, destinations[0],
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(source_temporary, cleanup_error);
        std::filesystem::remove(generated_temporary, cleanup_error);
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(report.errors, ErrorCode::io_error, "source",
                  "cannot publish the asset source");
        return report;
    }
    if (!publish_no_replace(generated_temporary, destinations[1],
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(generated_temporary, cleanup_error);
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(report.errors, ErrorCode::io_error, "atlas",
                  "cannot publish the generated asset");
        return report;
    }
    if (!publish_no_replace(document_temporary, destinations[2],
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(report.errors, ErrorCode::io_error,
                  std::string(asset_kind),
                  "cannot publish the asset document");
    }
    return report;
}

} // namespace fabric::project::detail
