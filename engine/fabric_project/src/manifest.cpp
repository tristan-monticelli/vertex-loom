#include "fabric/project/manifest.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
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

bool read_number(const Json& object, const char* key, double& destination,
                 std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number()) {
        add_error(errors, ErrorCode::invalid_manifest, key,
                  "expected a JSON number");
        return false;
    }
    destination = iterator->get<double>();
    return true;
}

bool read_bool(const Json& object, const char* key, bool& destination,
               std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto iterator = object.find(key);
    const std::string field = prefix.empty() ? key : std::string(prefix) + "." + key;
    if (iterator == object.end() || !iterator->is_boolean()) {
        add_error(errors, ErrorCode::invalid_manifest, field,
                  "expected a JSON boolean");
        return false;
    }
    destination = iterator->get<bool>();
    return true;
}

bool read_vec2(const Json& object, const char* key, core::Vec2& destination,
               std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto iterator = object.find(key);
    const std::string field = prefix.empty() ? key : std::string(prefix) + "." + key;
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_manifest, field,
                  "expected an object with x and y numbers");
        return false;
    }
    const auto x = iterator->find("x");
    const auto y = iterator->find("y");
    if (x == iterator->end() || !x->is_number() ||
        y == iterator->end() || !y->is_number()) {
        add_error(errors, ErrorCode::invalid_manifest, field,
                  "expected an object with x and y numbers");
        return false;
    }
    destination = {x->get<float>(), y->get<float>()};
    return true;
}

bool read_rect(const Json& object, const char* key, core::Rect& destination,
               std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto iterator = object.find(key);
    const std::string field = prefix.empty() ? key : std::string(prefix) + "." + key;
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_manifest, field,
                  "expected an object with x, y, width and height numbers");
        return false;
    }
    const auto x = iterator->find("x");
    const auto y = iterator->find("y");
    const auto width = iterator->find("width");
    const auto height = iterator->find("height");
    if (x == iterator->end() || !x->is_number() ||
        y == iterator->end() || !y->is_number() ||
        width == iterator->end() || !width->is_number() ||
        height == iterator->end() || !height->is_number()) {
        add_error(errors, ErrorCode::invalid_manifest, field,
                  "expected an object with x, y, width and height numbers");
        return false;
    }
    destination = {{x->get<float>(), y->get<float>()},
                   {width->get<float>(), height->get<float>()}};
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
    case ErrorCode::directory_not_empty: return "directory_not_empty";
    case ErrorCode::invalid_asset: return "invalid_asset";
    case ErrorCode::asset_already_exists: return "asset_already_exists";
    case ErrorCode::duplicate_resource: return "duplicate_resource";
    case ErrorCode::missing_resource: return "missing_resource";
    case ErrorCode::resource_type_mismatch: return "resource_type_mismatch";
    case ErrorCode::resource_cycle: return "resource_cycle";
    }
    return "unknown_error";
}

ValidationReport validate_manifest(const ProjectManifest& manifest) {
    ValidationReport report;
    if (manifest.schema_version != current_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only schema version 2 is supported");
    }
    if (!core::ResourceId::is_valid(manifest.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be 1-128 lowercase ASCII letters, digits, dots, underscores or hyphens, and start and end with a letter or digit");
    }
    if (manifest.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_manifest, "name",
                  "must not be empty");
    }
    if (!std::isfinite(manifest.pixels_per_unit) ||
        manifest.pixels_per_unit <= 0.0) {
        add_error(report.errors, ErrorCode::invalid_manifest,
                  "pixelsPerUnit", "must be finite and greater than zero");
    }
    if (manifest.default_stroke_texture &&
        !core::ResourceId::is_valid(manifest.default_stroke_texture->value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id,
                  "defaultStrokeTexture", "must be a valid texture identifier");
    }

    if (manifest.runtime) {
        const auto& runtime = *manifest.runtime;
        if (runtime.character.spawn &&
            (!std::isfinite(runtime.character.spawn->x) ||
             !std::isfinite(runtime.character.spawn->y))) {
            add_error(report.errors, ErrorCode::invalid_manifest,
                      "runtime.character.spawn", "coordinates must be finite");
        }
        for (std::size_t index = 0; index < runtime.character.actions.size(); ++index) {
            const auto& action = runtime.character.actions[index];
            if (!action.empty() && !core::ResourceId::is_valid(action)) {
                add_error(report.errors, ErrorCode::invalid_resource_id,
                          "runtime.character.actions[" + std::to_string(index) + "]",
                          "must be a valid semantic action identifier");
            }
        }
        if (runtime.camera.limits &&
            (!std::isfinite(runtime.camera.limits->origin.x) ||
             !std::isfinite(runtime.camera.limits->origin.y) ||
             !std::isfinite(runtime.camera.limits->size.x) ||
             !std::isfinite(runtime.camera.limits->size.y) ||
             runtime.camera.limits->size.x < 0.0F ||
             runtime.camera.limits->size.y < 0.0F)) {
            add_error(report.errors, ErrorCode::invalid_manifest,
                      "runtime.camera.limits", "must contain finite non-negative dimensions");
        }
        if (runtime.audio && !core::ResourceId::is_valid(runtime.audio->value)) {
            add_error(report.errors, ErrorCode::invalid_resource_id,
                      "runtime.audio", "must be a valid resource identifier");
        }
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
    read_number(document, "pixelsPerUnit", manifest.pixels_per_unit,
                result.errors);
    const auto default_texture = document.find("defaultStrokeTexture");
    if (default_texture != document.end()) {
        if (!default_texture->is_string())
            add_error(result.errors, ErrorCode::invalid_manifest,
                      "defaultStrokeTexture", "expected a texture identifier string");
        else manifest.default_stroke_texture =
            core::ResourceId{default_texture->get<std::string>()};
    }

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

    const auto runtime = document.find("runtime");
    if (runtime != document.end()) {
        if (!runtime->is_object()) {
            add_error(result.errors, ErrorCode::invalid_manifest, "runtime",
                      "expected a JSON object");
        } else {
            RuntimeSettings settings;
            const auto character = runtime->find("character");
            if (character == runtime->end() || !character->is_object()) {
                add_error(result.errors, ErrorCode::invalid_manifest,
                          "runtime.character", "expected a JSON object");
            } else {
                read_bool(*character, "enabled", settings.character.enabled,
                          result.errors, "runtime.character");
                core::Vec2 spawn;
                if (character->contains("spawn") &&
                    read_vec2(*character, "spawn", spawn, result.errors,
                              "runtime.character")) settings.character.spawn = spawn;
                const auto actions = character->find("actions");
                if (actions == character->end() || !actions->is_array() ||
                    actions->size() != settings.character.actions.size()) {
                    add_error(result.errors, ErrorCode::invalid_manifest,
                              "runtime.character.actions", "expected an array of three strings");
                } else {
                    for (std::size_t index = 0; index < actions->size(); ++index) {
                        if (!(*actions)[index].is_string()) {
                            add_error(result.errors, ErrorCode::invalid_manifest,
                                      "runtime.character.actions", "expected an array of three strings");
                            break;
                        }
                        settings.character.actions[index] = (*actions)[index].get<std::string>();
                    }
                }
            }
            const auto camera = runtime->find("camera");
            if (camera == runtime->end() || !camera->is_object()) {
                add_error(result.errors, ErrorCode::invalid_manifest,
                          "runtime.camera", "expected a JSON object");
            } else {
                read_bool(*camera, "followCharacter", settings.camera.follow_character,
                          result.errors, "runtime.camera");
                core::Rect limits;
                if (camera->contains("limits") &&
                    read_rect(*camera, "limits", limits, result.errors,
                              "runtime.camera")) settings.camera.limits = limits;
            }
            const auto audio = runtime->find("audio");
            if (audio != runtime->end()) {
                if (!audio->is_string()) add_error(result.errors, ErrorCode::invalid_manifest,
                                                   "runtime.audio", "expected a resource identifier string");
                else settings.audio = core::ResourceId{audio->get<std::string>()};
            }
            manifest.runtime = settings;
        }
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
    Json document = {
        {"schemaVersion", manifest.schema_version},
        {"id", manifest.id.value},
        {"name", manifest.name},
        {"pixelsPerUnit", manifest.pixels_per_unit},
        {"directories", {
            {"assets", manifest.directories.assets.generic_string()},
            {"entities", manifest.directories.entities.generic_string()},
            {"maps", manifest.directories.maps.generic_string()},
            {"scenes", manifest.directories.scenes.generic_string()},
            {"schemas", manifest.directories.schemas.generic_string()},
        }},
    };
    if (manifest.default_stroke_texture)
        document["defaultStrokeTexture"] = manifest.default_stroke_texture->value;
    if (manifest.runtime) {
        const auto& runtime = *manifest.runtime;
        Json character = {
            {"enabled", runtime.character.enabled},
            {"actions", runtime.character.actions},
        };
        if (runtime.character.spawn) character["spawn"] = {
            {"x", runtime.character.spawn->x}, {"y", runtime.character.spawn->y}};
        Json camera = {{"followCharacter", runtime.camera.follow_character}};
        if (runtime.camera.limits) camera["limits"] = {
            {"x", runtime.camera.limits->origin.x},
            {"y", runtime.camera.limits->origin.y},
            {"width", runtime.camera.limits->size.x},
            {"height", runtime.camera.limits->size.y}};
        document["runtime"] = {{"character", character}, {"camera", camera}};
        if (runtime.audio) document["runtime"]["audio"] = runtime.audio->value;
    }
    return document.dump(2) + '\n';
}

} // namespace fabric::project
