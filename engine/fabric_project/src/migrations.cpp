#include "fabric/project/manifest.hpp"

#include <nlohmann/json.hpp>

#include <limits>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

void migrate_v0_to_v1(Json& document) {
    if (const auto project_id = document.find("projectId");
        project_id != document.end()) {
        document["id"] = *project_id;
    }
    if (const auto display_name = document.find("displayName");
        display_name != document.end()) {
        document["name"] = *display_name;
    }

    Json directories = {
        {"assets", "assets"},
        {"entities", "entities"},
        {"maps", "maps"},
        {"scenes", "scenes"},
        {"schemas", "schemas"},
    };
    if (const auto assets_path = document.find("assetsPath");
        assets_path != document.end()) {
        directories["assets"] = *assets_path;
    }
    document["directories"] = std::move(directories);
    document["schemaVersion"] = 1;
    document.erase("projectId");
    document.erase("displayName");
    document.erase("assetsPath");
}

} // namespace

MigrationResult migrate_manifest(const std::string_view json_text) {
    MigrationResult result;
    Json document = Json::parse(json_text, nullptr, false);
    if (document.is_discarded()) {
        add_error(result.errors, ErrorCode::invalid_json, "project.json",
                  "document is not valid JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_manifest, "project.json",
                  "top-level value must be an object");
        return result;
    }

    const auto version_value = document.find("schemaVersion");
    if (version_value == document.end() || !version_value->is_number_unsigned() ||
        version_value->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
        add_error(result.errors, ErrorCode::invalid_manifest, "schemaVersion",
                  "expected an unsigned 32-bit integer");
        return result;
    }

    std::uint32_t version = version_value->get<std::uint32_t>();
    while (version < current_schema_version) {
        switch (version) {
        case 0:
            migrate_v0_to_v1(document);
            version = 1;
            break;
        default:
            add_error(result.errors, ErrorCode::unsupported_schema_version,
                      "schemaVersion", "no migration path is available");
            return result;
        }
    }
    if (version > current_schema_version) {
        add_error(result.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "project requires a newer schema version");
        return result;
    }

    result.json = document.dump();
    return result;
}

} // namespace fabric::project
