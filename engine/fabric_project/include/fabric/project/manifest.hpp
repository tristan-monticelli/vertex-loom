#pragma once

#include "fabric/core/resource_id.hpp"
#include "fabric/core/types.hpp"

#include <cstdint>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::project {

inline constexpr std::uint32_t current_schema_version = 2;
inline constexpr double default_pixels_per_unit = 100.0;

enum class ErrorCode {
    io_error,
    invalid_json,
    invalid_manifest,
    unsupported_schema_version,
    invalid_resource_id,
    invalid_path,
    missing_file,
    missing_directory,
    directory_not_empty,
    invalid_asset,
    asset_already_exists,
    duplicate_resource,
    missing_resource,
    resource_type_mismatch,
    resource_cycle,
};

struct Error {
    ErrorCode code;
    std::string field;
    std::string message;

    friend bool operator==(const Error&, const Error&) = default;
};

struct ProjectDirectories {
    std::filesystem::path assets{"assets"};
    std::filesystem::path entities{"entities"};
    std::filesystem::path maps{"maps"};
    std::filesystem::path scenes{"scenes"};
    std::filesystem::path schemas{"schemas"};

    friend bool operator==(const ProjectDirectories&, const ProjectDirectories&) = default;
};

struct RuntimeCharacterSettings {
    bool enabled{};
    std::optional<core::Vec2> spawn;
    std::array<std::string, 3> actions{};

    friend bool operator==(const RuntimeCharacterSettings&, const RuntimeCharacterSettings&) = default;
};

struct RuntimeCameraSettings {
    bool follow_character{};
    std::optional<core::Rect> limits;

    friend bool operator==(const RuntimeCameraSettings&, const RuntimeCameraSettings&) = default;
};

struct RuntimeSettings {
    RuntimeCharacterSettings character;
    RuntimeCameraSettings camera;
    std::optional<core::ResourceId> audio;

    friend bool operator==(const RuntimeSettings&, const RuntimeSettings&) = default;
};

struct ProjectManifest {
    std::uint32_t schema_version{current_schema_version};
    core::ResourceId id;
    std::string name;
    double pixels_per_unit{default_pixels_per_unit};
    ProjectDirectories directories;
    std::optional<RuntimeSettings> runtime;

    friend bool operator==(const ProjectManifest&, const ProjectManifest&) = default;
};

struct ValidationReport {
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

struct ManifestResult {
    std::optional<ProjectManifest> manifest;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return manifest.has_value() && errors.empty();
    }
};

struct MigrationResult {
    std::optional<std::string> json;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept {
        return json.has_value() && errors.empty();
    }
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;
[[nodiscard]] ValidationReport validate_manifest(const ProjectManifest& manifest);
[[nodiscard]] MigrationResult migrate_manifest(std::string_view json_text);
[[nodiscard]] ManifestResult parse_manifest(std::string_view json_text);
[[nodiscard]] std::string serialize_manifest(const ProjectManifest& manifest);
[[nodiscard]] ManifestResult load_manifest(const std::filesystem::path& project_root);
[[nodiscard]] ManifestResult load_project(const std::filesystem::path& project_root);
[[nodiscard]] ManifestResult create_project(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest);
[[nodiscard]] ValidationReport save_manifest_atomic(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest);
[[nodiscard]] ValidationReport validate_project(const std::filesystem::path& project_root);

} // namespace fabric::project
