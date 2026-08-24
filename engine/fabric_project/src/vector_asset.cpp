#include "fabric/project/vector_asset.hpp"

#include "asset_storage.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool read_string(const Json& object, const char* key, std::string& destination,
                 std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected a JSON string");
        return false;
    }
    destination = iterator->get<std::string>();
    return true;
}

bool read_version(const Json& object, std::uint32_t& destination,
                  std::vector<Error>& errors) {
    const auto iterator = object.find("schemaVersion");
    if (iterator == object.end() || !iterator->is_number_unsigned() ||
        iterator->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
        add_error(errors, ErrorCode::invalid_asset, "schemaVersion",
                  "expected an unsigned 32-bit integer");
        return false;
    }
    destination = iterator->get<std::uint32_t>();
    return true;
}

bool read_source_kind(const Json& object, VectorSourceKind& destination,
                      std::vector<Error>& errors) {
    std::string value;
    if (!read_string(object, "sourceKind", value, errors)) {
        return false;
    }
    if (value == "linkedSvg") {
        destination = VectorSourceKind::linked_svg;
        return true;
    }
    if (value == "native") {
        destination = VectorSourceKind::native;
        return true;
    }
    add_error(errors, ErrorCode::invalid_asset, "sourceKind",
              "must be linkedSvg or native");
    return false;
}

} // namespace

std::string_view to_string(const VectorSourceKind kind) noexcept {
    switch (kind) {
    case VectorSourceKind::linked_svg: return "linkedSvg";
    case VectorSourceKind::native: return "native";
    }
    return "native";
}

std::filesystem::path vector_source_path(const ProjectManifest& manifest,
                                         const core::ResourceId& id) {
    return manifest.directories.assets / "vectors" / (id.value + ".svg");
}

std::filesystem::path vector_document_path(const ProjectManifest& manifest,
                                           const core::ResourceId& id) {
    return manifest.directories.assets / "vectors" /
           (id.value + ".vector.json");
}

ValidationReport validate_vector_asset(const ProjectManifest& manifest,
                                       const VectorAsset& asset) {
    ValidationReport report;
    if (asset.document.schema_version != current_vector_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only vector schema version 2 is supported");
    }
    if (asset.document.type != "vector") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be vector");
    }
    if (!core::ResourceId::is_valid(asset.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (asset.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (asset.source_kind == VectorSourceKind::linked_svg) {
        if (!detail::is_portable_relative_path(asset.source) ||
            asset.source != vector_source_path(manifest, asset.document.id)) {
            add_error(report.errors, ErrorCode::invalid_path, "source",
                      "linkedSvg source must use the canonical project-relative vector path");
        }
    } else {
        if (!asset.source.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, "source",
                      "native vectors must not declare an SVG source");
        }
        add_error(report.errors, ErrorCode::invalid_asset, "native",
                  "native vector geometry is not available in this migration slice");
    }
    return report;
}

std::string serialize_vector_asset(const VectorAsset& asset) {
    Json document = {
        {"schemaVersion", asset.document.schema_version},
        {"type", asset.document.type},
        {"id", asset.document.id.value},
        {"name", asset.document.name},
        {"sourceKind", std::string(to_string(asset.source_kind))},
    };
    if (asset.source_kind == VectorSourceKind::linked_svg) {
        document["source"] = asset.source.generic_string();
    }
    return document.dump(2) + '\n';
}

VectorAssetResult parse_vector_asset(const ProjectManifest& manifest,
                                     const std::string_view json_text) {
    VectorAssetResult result;
    Json document;
    try {
        document = Json::parse(json_text);
    } catch (const Json::parse_error&) {
        add_error(result.errors, ErrorCode::invalid_json, "vector",
                  "cannot parse vector asset JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "vector",
                  "top-level value must be an object");
        return result;
    }

    VectorAsset asset;
    std::uint32_t source_version{};
    read_version(document, source_version, result.errors);
    read_string(document, "type", asset.document.type, result.errors);
    read_string(document, "id", asset.document.id.value, result.errors);
    read_string(document, "name", asset.document.name, result.errors);
    if (source_version == 1) {
        std::string format;
        read_string(document, "format", format, result.errors);
        if (format != "svg") {
            add_error(result.errors, ErrorCode::invalid_asset, "format",
                      "vector schema version 1 only supports svg");
        }
        asset.source_kind = VectorSourceKind::linked_svg;
    } else if (source_version == current_vector_schema_version) {
        read_source_kind(document, asset.source_kind, result.errors);
    } else if (result.errors.empty()) {
        add_error(result.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only vector schema versions 1 and 2 are readable");
    }
    if (asset.source_kind == VectorSourceKind::linked_svg) {
        std::string source;
        if (read_string(document, "source", source, result.errors)) {
            asset.source = source;
        }
    }
    asset.document.schema_version = current_vector_schema_version;
    if (!result.errors.empty()) {
        return result;
    }
    auto validation = validate_vector_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(asset);
    return result;
}

VectorAssetResult load_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path) {
    VectorAssetResult result;
    if (!detail::is_portable_relative_path(document_path)) {
        add_error(result.errors, ErrorCode::invalid_path, "vector",
                  "document path must be project-relative");
        return result;
    }
    std::error_code filesystem_error;
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return result;
    }
    filesystem_error.clear();
    const auto canonical_document = std::filesystem::weakly_canonical(
        project_root / document_path, filesystem_error);
    if (filesystem_error ||
        !detail::is_within(canonical_root, canonical_document)) {
        add_error(result.errors, ErrorCode::invalid_path, "vector",
                  "vector document must resolve inside the project");
        return result;
    }
    std::ifstream input(canonical_document, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::missing_file, "vector",
                  "cannot open vector asset document");
        return result;
    }
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    result = parse_vector_asset(manifest, contents);
    if (!result.ok()) {
        return result;
    }
    if (document_path != vector_document_path(manifest,
                                              result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "vector",
                  "document filename does not match its resource identifier");
        return result;
    }

    filesystem_error.clear();
    const auto canonical_source = std::filesystem::weakly_canonical(
        project_root / result.asset->source, filesystem_error);
    const bool source_is_file = !filesystem_error &&
        std::filesystem::is_regular_file(canonical_source, filesystem_error);
    if (filesystem_error ||
        !detail::is_within(canonical_root, canonical_source) ||
        !source_is_file) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::missing_file, "source",
                  "vector source is missing or outside the project");
    }
    return result;
}

VectorAssetResult publish_vector_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const VectorAsset& asset,
    const std::filesystem::path& validated_source) {
    VectorAssetResult result;
    auto validation = validate_vector_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    if (asset.source_kind != VectorSourceKind::linked_svg) {
        add_error(result.errors, ErrorCode::invalid_asset, "sourceKind",
                  "publish_vector_asset only publishes linked SVG imports");
        return result;
    }
    const auto document_relative = vector_document_path(
        manifest, asset.document.id);
    auto publication = detail::publish_asset_files(
        project_root, manifest.directories.assets / "vectors", asset.source,
        document_relative, validated_source, serialize_vector_asset(asset),
        "vector");
    if (!publication.ok()) {
        result.errors = std::move(publication.errors);
        return result;
    }
    return load_vector_asset(project_root, manifest, document_relative);
}

} // namespace fabric::project
