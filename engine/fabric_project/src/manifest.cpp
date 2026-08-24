#include "fabric/project/manifest.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool read_string(const Json& object, const char* key, std::string& destination,
                 std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto iterator = object.find(key);
    const std::string field = prefix.empty() ? key : std::string(prefix) + "." + key;
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_manifest, field,
                  "expected a JSON string");
        return false;
    }
    destination = iterator->get<std::string>();
    return true;
}

bool is_portable_relative_path(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    if (value.empty() || value == "." || path.is_absolute() ||
        value.starts_with('/') || value.starts_with('\\') ||
        (value.size() >= 2 && value[1] == ':') || value.find('\\') != std::string::npos) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view to_string(const ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::io_error: return "io_error";
    case ErrorCode::invalid_json: return "invalid_json";
    case ErrorCode::invalid_manifest: return "invalid_manifest";
    case ErrorCode::unsupported_schema_version: return "unsupported_schema_version";
    case ErrorCode::invalid_resource_id: return "invalid_resource_id";
    case ErrorCode::invalid_path: return "invalid_path";
    case ErrorCode::missing_file: return "missing_file";
    case ErrorCode::missing_directory: return "missing_directory";
    }
    return "unknown_error";
}

ValidationReport validate_manifest(const ProjectManifest& manifest) {
    ValidationReport report;
    if (manifest.schema_version != current_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only schema version 1 is supported");
    }
    if (!core::ResourceId::is_valid(manifest.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be 1-128 lowercase ASCII letters, digits, dots, underscores or hyphens, and start and end with a letter or digit");
    }
    if (manifest.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_manifest, "name",
                  "must not be empty");
    }

    const std::array directories{
        std::pair{"directories.assets", &manifest.directories.assets},
        std::pair{"directories.entities", &manifest.directories.entities},
        std::pair{"directories.maps", &manifest.directories.maps},
        std::pair{"directories.scenes", &manifest.directories.scenes},
        std::pair{"directories.schemas", &manifest.directories.schemas},
    };
    for (const auto& [field, path] : directories) {
        if (!is_portable_relative_path(*path)) {
            add_error(report.errors, ErrorCode::invalid_path, field,
                      "must be a portable relative path without traversal");
        }
    }
    return report;
}

ManifestResult parse_manifest(const std::string_view json_text) {
    ManifestResult result;
    MigrationResult migration = migrate_manifest(json_text);
    if (!migration.ok()) {
        result.errors = std::move(migration.errors);
        return result;
    }
    const Json document = Json::parse(*migration.json);
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_manifest, "project.json",
                  "top-level value must be an object");
        return result;
    }

    ProjectManifest manifest;
    const auto version = document.find("schemaVersion");
    if (version == document.end() || !version->is_number_unsigned() ||
        version->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
        add_error(result.errors, ErrorCode::invalid_manifest, "schemaVersion",
                  "expected an unsigned 32-bit integer");
    } else {
        manifest.schema_version = version->get<std::uint32_t>();
    }

    read_string(document, "id", manifest.id.value, result.errors);
    read_string(document, "name", manifest.name, result.errors);

    const auto directories = document.find("directories");
    if (directories == document.end() || !directories->is_object()) {
        add_error(result.errors, ErrorCode::invalid_manifest, "directories",
                  "expected a JSON object");
    } else {
        std::string path;
        if (read_string(*directories, "assets", path, result.errors, "directories")) manifest.directories.assets = path;
        if (read_string(*directories, "entities", path, result.errors, "directories")) manifest.directories.entities = path;
        if (read_string(*directories, "maps", path, result.errors, "directories")) manifest.directories.maps = path;
        if (read_string(*directories, "scenes", path, result.errors, "directories")) manifest.directories.scenes = path;
        if (read_string(*directories, "schemas", path, result.errors, "directories")) manifest.directories.schemas = path;
    }

    if (!result.errors.empty()) {
        return result;
    }
    ValidationReport validation = validate_manifest(manifest);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.manifest = std::move(manifest);
    return result;
}

std::string serialize_manifest(const ProjectManifest& manifest) {
    const Json document = {
        {"schemaVersion", manifest.schema_version},
        {"id", manifest.id.value},
        {"name", manifest.name},
        {"directories", {
            {"assets", manifest.directories.assets.generic_string()},
            {"entities", manifest.directories.entities.generic_string()},
            {"maps", manifest.directories.maps.generic_string()},
            {"scenes", manifest.directories.scenes.generic_string()},
            {"schemas", manifest.directories.schemas.generic_string()},
        }},
    };
    return document.dump(2) + '\n';
}

} // namespace fabric::project
