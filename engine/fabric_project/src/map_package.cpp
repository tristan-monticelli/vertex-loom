#include "fabric/project/map_package.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <limits>
#include <set>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;

struct SemanticVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};
    friend auto operator<=>(const SemanticVersion&,
                            const SemanticVersion&) = default;
};

void error(std::vector<Error>& errors, const ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

std::optional<SemanticVersion> parse_semantic_version(
    const std::string_view value) noexcept {
    SemanticVersion result;
    std::array<std::uint32_t*, 3> fields{&result.major, &result.minor,
                                         &result.patch};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto end = index + 1 == fields.size() ? value.size()
                                                    : value.find('.', begin);
        if (end == std::string_view::npos || end == begin ||
            (end - begin > 1 && value[begin] == '0'))
            return std::nullopt;
        const auto* first = value.data() + begin;
        const auto* last = value.data() + end;
        const auto parsed = std::from_chars(first, last, *fields[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != last) return std::nullopt;
        begin = end + 1;
    }
    return result;
}

bool portable_relative_path(const std::filesystem::path& path) {
    const auto value = path.generic_string();
    if (value.empty() || value == "." || path.is_absolute() ||
        value.starts_with('/') || value.starts_with('\\') ||
        (value.size() >= 2 && value[1] == ':') ||
        value.find('\\') != std::string::npos)
        return false;
    return std::none_of(path.begin(), path.end(), [](const auto& component) {
        return component == ".." || component == ".";
    });
}

void reject_unknown_fields(
    const Json& object, const std::initializer_list<std::string_view> allowed,
    const std::string_view prefix, std::vector<Error>& errors) {
    if (!object.is_object()) return;
    for (const auto& [key, unused] : object.items()) {
        (void)unused;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            error(errors, ErrorCode::invalid_asset,
                  prefix.empty() ? key : std::string(prefix) + "." + key,
                  "unknown field");
    }
}

bool read_text(const Json& object, const char* key, std::string& destination,
               std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto item = object.find(key);
    const auto field = prefix.empty() ? std::string(key)
                                      : std::string(prefix) + "." + key;
    if (item == object.end() || !item->is_string()) {
        error(errors, ErrorCode::invalid_asset, field, "expected a string");
        return false;
    }
    destination = item->get<std::string>();
    return true;
}

Json reference_json(const ResourceReference& reference) {
    return {{"id", reference.id.value},
            {"expectedType", reference.expected_type}};
}

bool read_reference(const Json& value, ResourceReference& destination,
                    const std::string_view prefix,
                    std::vector<Error>& errors) {
    if (!value.is_object()) {
        error(errors, ErrorCode::invalid_asset, std::string(prefix),
              "expected a resource reference");
        return false;
    }
    reject_unknown_fields(value, {"id", "expectedType"}, prefix, errors);
    return read_text(value, "id", destination.id.value, errors, prefix) &&
        read_text(value, "expectedType", destination.expected_type, errors,
                  prefix);
}

} // namespace

ValidationReport validate_map_package_manifest(
    const MapPackageManifest& manifest) {
    ValidationReport report;
    if (manifest.schema_version != current_map_package_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version,
              "schemaVersion", "only map package schema version 1 is supported");
    if (manifest.type != "map-package")
        error(report.errors, ErrorCode::invalid_asset, "type",
              "must be map-package");
    if (!core::ResourceId::is_valid(manifest.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id",
              "must be a valid resource id");
    if (manifest.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name",
              "must not be empty");
    if (!parse_semantic_version(manifest.minimum_runtime_version))
        error(report.errors, ErrorCode::invalid_asset,
              "minimumRuntimeVersion", "must be MAJOR.MINOR.PATCH SemVer");
    if (!core::ResourceId::is_valid(manifest.root_map.id.value) ||
        manifest.root_map.expected_type != "map")
        error(report.errors, ErrorCode::resource_type_mismatch, "rootMap",
              "must reference a map");

    std::set<std::pair<std::string, std::string>> resources;
    std::set<std::string> paths;
    std::pair<std::string, std::string> previous;
    bool first = true;
    bool root_found = false;
    for (const auto& entry : manifest.resources) {
        const auto key = std::pair{entry.resource.expected_type,
                                   entry.resource.id.value};
        if (!core::ResourceId::is_valid(entry.resource.id.value) ||
            entry.resource.expected_type.empty())
            error(report.errors, ErrorCode::invalid_resource_id, "resources",
                  "resource id and type must be valid");
        if (!resources.insert(key).second)
            error(report.errors, ErrorCode::duplicate_resource, "resources",
                  "resource type/id pairs must be unique");
        if (!first && key <= previous)
            error(report.errors, ErrorCode::invalid_asset, "resources",
                  "must be strictly ordered by type then id");
        first = false;
        previous = key;
        root_found = root_found || entry.resource == manifest.root_map;

        const auto register_path = [&](const std::filesystem::path& path,
                                       const std::string_view field) {
            if (!portable_relative_path(path))
                error(report.errors, ErrorCode::invalid_path,
                      std::string(field),
                      "must be a portable relative package path");
            else if (!paths.insert(path.generic_string()).second)
                error(report.errors, ErrorCode::duplicate_resource,
                      std::string(field), "package paths must be unique");
        };
        register_path(entry.document_path, "resources.documentPath");
        std::string previous_payload;
        bool first_payload = true;
        for (const auto& payload : entry.payload_paths) {
            const auto path = payload.generic_string();
            if (!first_payload && path <= previous_payload)
                error(report.errors, ErrorCode::invalid_asset,
                      "resources.payloadPaths", "must be strictly ordered");
            first_payload = false;
            previous_payload = path;
            register_path(payload, "resources.payloadPaths");
        }
    }
    if (!root_found)
        error(report.errors, ErrorCode::missing_resource, "rootMap",
              "must be present in resources");
    return report;
}

std::string serialize_map_package_manifest(const MapPackageManifest& manifest) {
    Json resources = Json::array();
    for (const auto& entry : manifest.resources) {
        Json payloads = Json::array();
        for (const auto& path : entry.payload_paths)
            payloads.push_back(path.generic_string());
        resources.push_back({{"resource", reference_json(entry.resource)},
                             {"documentPath",
                              entry.document_path.generic_string()},
                             {"payloadPaths", std::move(payloads)}});
    }
    return Json{{"schemaVersion", manifest.schema_version},
                {"type", manifest.type},
                {"id", manifest.id.value},
                {"name", manifest.name},
                {"minimumRuntimeVersion", manifest.minimum_runtime_version},
                {"rootMap", reference_json(manifest.root_map)},
                {"resources", std::move(resources)}}
               .dump(2) +
        "\n";
}

MapPackageManifestResult parse_map_package_manifest(
    const std::string_view json_text) {
    MapPackageManifestResult result;
    Json json;
    try {
        json = Json::parse(json_text);
    } catch (...) {
        error(result.errors, ErrorCode::invalid_json, "map-package.json",
              "cannot parse map package JSON");
        return result;
    }
    if (!json.is_object()) {
        error(result.errors, ErrorCode::invalid_asset, "map-package.json",
              "top-level value must be an object");
        return result;
    }
    reject_unknown_fields(json,
                          {"schemaVersion", "type", "id", "name",
                           "minimumRuntimeVersion", "rootMap", "resources"},
                          {}, result.errors);
    MapPackageManifest manifest;
    const auto schema = json.find("schemaVersion");
    if (schema == json.end() || !schema->is_number_unsigned() ||
        schema->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max())
        error(result.errors, ErrorCode::invalid_asset, "schemaVersion",
              "expected an unsigned 32-bit integer");
    else
        manifest.schema_version = schema->get<std::uint32_t>();
    read_text(json, "type", manifest.type, result.errors);
    read_text(json, "id", manifest.id.value, result.errors);
    read_text(json, "name", manifest.name, result.errors);
    read_text(json, "minimumRuntimeVersion",
              manifest.minimum_runtime_version, result.errors);
    const auto root = json.find("rootMap");
    if (root == json.end())
        error(result.errors, ErrorCode::invalid_asset, "rootMap",
              "expected a resource reference");
    else
        read_reference(*root, manifest.root_map, "rootMap", result.errors);

    const auto resources = json.find("resources");
    if (resources == json.end() || !resources->is_array()) {
        error(result.errors, ErrorCode::invalid_asset, "resources",
              "expected an array");
    } else {
        for (std::size_t index = 0; index < resources->size(); ++index) {
            const auto& item = (*resources)[index];
            const auto prefix = "resources[" + std::to_string(index) + "]";
            if (!item.is_object()) {
                error(result.errors, ErrorCode::invalid_asset, prefix,
                      "expected an object");
                continue;
            }
            reject_unknown_fields(item,
                                  {"resource", "documentPath", "payloadPaths"},
                                  prefix, result.errors);
            MapPackageResource entry;
            const auto resource = item.find("resource");
            if (resource == item.end())
                error(result.errors, ErrorCode::invalid_asset,
                      prefix + ".resource", "expected a resource reference");
            else
                read_reference(*resource, entry.resource,
                               prefix + ".resource", result.errors);
            std::string document_path;
            if (read_text(item, "documentPath", document_path, result.errors,
                          prefix))
                entry.document_path = document_path;
            const auto payloads = item.find("payloadPaths");
            if (payloads == item.end() || !payloads->is_array())
                error(result.errors, ErrorCode::invalid_asset,
                      prefix + ".payloadPaths", "expected an array");
            else
                for (const auto& payload : *payloads) {
                    if (!payload.is_string())
                        error(result.errors, ErrorCode::invalid_asset,
                              prefix + ".payloadPaths",
                              "expected string paths");
                    else
                        entry.payload_paths.emplace_back(
                            payload.get<std::string>());
                }
            manifest.resources.push_back(std::move(entry));
        }
    }
    if (!result.errors.empty()) return result;
    auto validation = validate_map_package_manifest(manifest);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.manifest = std::move(manifest);
    return result;
}

bool runtime_can_load_map_package(const MapPackageManifest& manifest,
                                  const std::string_view runtime_version) noexcept {
    if (manifest.schema_version != current_map_package_schema_version)
        return false;
    const auto minimum = parse_semantic_version(manifest.minimum_runtime_version);
    const auto runtime = parse_semantic_version(runtime_version);
    return minimum && runtime && *runtime >= *minimum;
}

} // namespace fabric::project
