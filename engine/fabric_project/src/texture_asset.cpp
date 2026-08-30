#include "fabric/project/texture_asset.hpp"

#include "asset_storage.hpp"
#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <cmath>
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

bool read_dimension(const Json& object, const char* key,
                    std::uint32_t& destination, std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number_unsigned() ||
        iterator->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected an unsigned 32-bit integer");
        return false;
    }
    destination = iterator->get<std::uint32_t>();
    return true;
}

bool read_float(const Json& object, const char* key, float& destination,
                std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected a finite number");
        return false;
    }
    destination = iterator->get<float>();
    if (!std::isfinite(destination)) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected a finite number");
        return false;
    }
    return true;
}

bool read_vec2(const Json& object, const char* key, core::Vec2& destination,
               std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected an object with x and y");
        return false;
    }
    return read_float(*iterator, "x", destination.x, errors) &&
        read_float(*iterator, "y", destination.y, errors);
}

bool read_rect(const Json& object, const char* key, core::Rect& destination,
               std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected an object with origin and size");
        return false;
    }
    return read_vec2(*iterator, "origin", destination.origin, errors) &&
        read_vec2(*iterator, "size", destination.size, errors);
}

bool read_transform(const Json& object, const char* key,
                    core::Transform& destination,
                    std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected a transform object");
        return false;
    }
    return read_vec2(*iterator, "position", destination.position, errors) &&
        read_float(*iterator, "rotationDegrees", destination.rotation_degrees,
                   errors) &&
        read_vec2(*iterator, "scale", destination.scale, errors) &&
        read_vec2(*iterator, "pivot", destination.pivot, errors);
}

Json serialize_vec2(const core::Vec2 value) {
    return Json{{"x", value.x}, {"y", value.y}};
}

Json serialize_transform(const core::Transform& transform) {
    return Json{{"position", serialize_vec2(transform.position)},
                {"rotationDegrees", transform.rotation_degrees},
                {"scale", serialize_vec2(transform.scale)},
                {"pivot", serialize_vec2(transform.pivot)}};
}

bool read_filter(const Json& object, const char* key, RasterFilter& destination,
                 std::vector<Error>& errors) {
    std::string value;
    if (!read_string(object, key, value, errors)) return false;
    if (value == "nearest") destination = RasterFilter::nearest;
    else if (value == "linear") destination = RasterFilter::linear;
    else {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "must be nearest or linear");
        return false;
    }
    return true;
}

} // namespace

std::string_view to_string(const RasterFilter filter) noexcept {
    return filter == RasterFilter::nearest ? "nearest" : "linear";
}

std::filesystem::path texture_source_path(const ProjectManifest& manifest,
                                          const core::ResourceId& id) {
    return manifest.directories.assets / "textures" / (id.value + ".png");
}

std::filesystem::path texture_document_path(const ProjectManifest& manifest,
                                            const core::ResourceId& id) {
    return manifest.directories.assets / "textures" /
           (id.value + ".texture.json");
}

ValidationReport validate_texture_asset(const ProjectManifest& manifest,
                                        const TextureAsset& asset) {
    ValidationReport report;
    if (asset.document.schema_version != current_texture_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only texture schema version 1 is supported");
    }
    if (asset.document.type != "texture") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be texture");
    }
    if (!core::ResourceId::is_valid(asset.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (asset.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (!detail::is_portable_relative_path(asset.source) ||
        asset.source != texture_source_path(manifest, asset.document.id)) {
        add_error(report.errors, ErrorCode::invalid_path, "source",
                  "must be the canonical project-relative texture path");
    }
    constexpr std::uint32_t maximum_dimension = 16'384;
    constexpr std::uint64_t maximum_pixels = 67'108'864;
    const auto pixels = static_cast<std::uint64_t>(asset.width) * asset.height;
    if (asset.width == 0 || asset.height == 0 ||
        asset.width > maximum_dimension || asset.height > maximum_dimension ||
        pixels > maximum_pixels) {
        add_error(report.errors, ErrorCode::invalid_asset, "dimensions",
                  "dimensions exceed the texture safety limits");
    }
    if (asset.pixel_format != "rgba8") {
        add_error(report.errors, ErrorCode::invalid_asset, "pixelFormat",
                  "only rgba8 is supported");
    }
    if (asset.view) {
        auto view_report = validate_raster_view(*asset.view, asset.width,
                                                asset.height);
        report.errors.insert(report.errors.end(), view_report.errors.begin(),
                             view_report.errors.end());
    }
    return report;
}

ValidationReport validate_raster_view(const RasterView& view,
                                     const std::uint32_t source_width,
                                     const std::uint32_t source_height) {
    ValidationReport report;
    if (view.schema_version != current_raster_view_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "view.schemaVersion", "only raster view schema version 1 is supported");
    }
    const auto finite = [](const float value) { return std::isfinite(value); };
    const auto& crop = view.crop;
    if (!finite(crop.origin.x) || !finite(crop.origin.y) ||
        !finite(crop.size.x) || !finite(crop.size.y) ||
        crop.origin.x < 0.0F || crop.origin.y < 0.0F ||
        crop.size.x <= 0.0F || crop.size.y <= 0.0F ||
        crop.origin.x + crop.size.x > static_cast<float>(source_width) ||
        crop.origin.y + crop.size.y > static_cast<float>(source_height)) {
        add_error(report.errors, ErrorCode::invalid_asset, "view.crop",
                  "must be a positive rectangle inside the source image");
    }
    if (!finite(view.pivot.x) || !finite(view.pivot.y) || view.pivot.x < 0.0F ||
        view.pivot.x > 1.0F || view.pivot.y < 0.0F || view.pivot.y > 1.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "view.pivot",
                  "must be finite and normalized between 0 and 1");
    }
    const auto values = {view.transform.position.x, view.transform.position.y,
                         view.transform.rotation_degrees, view.transform.scale.x,
                         view.transform.scale.y, view.transform.pivot.x,
                         view.transform.pivot.y};
    if (!std::ranges::all_of(values, finite)) {
        add_error(report.errors, ErrorCode::invalid_asset, "view.transform",
                  "must contain only finite values");
    }
    return report;
}

std::string serialize_texture_asset(const TextureAsset& asset) {
    Json document = {
        {"schemaVersion", asset.document.schema_version},
        {"type", asset.document.type},
        {"id", asset.document.id.value},
        {"name", asset.document.name},
        {"source", asset.source.generic_string()},
        {"width", asset.width},
        {"height", asset.height},
        {"pixelFormat", asset.pixel_format},
    };
    if (asset.view) {
        document["view"] = {
            {"schemaVersion", asset.view->schema_version},
            {"crop", { {"origin", serialize_vec2(asset.view->crop.origin)},
                        {"size", serialize_vec2(asset.view->crop.size)} }},
            {"pivot", serialize_vec2(asset.view->pivot)},
            {"transform", serialize_transform(asset.view->transform)},
            {"filter", std::string(to_string(asset.view->filter))},
        };
    }
    return document.dump(2) + '\n';
}

TextureAssetResult parse_texture_asset(const ProjectManifest& manifest,
                                       const std::string_view json_text) {
    TextureAssetResult result;
    Json document;
    try {
        document = Json::parse(json_text);
    } catch (const Json::parse_error&) {
        add_error(result.errors, ErrorCode::invalid_json, "texture",
                  "cannot parse texture asset JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "texture",
                  "top-level value must be an object");
        return result;
    }

    TextureAsset asset;
    read_dimension(document, "schemaVersion", asset.document.schema_version,
                   result.errors);
    read_string(document, "type", asset.document.type, result.errors);
    read_string(document, "id", asset.document.id.value, result.errors);
    read_string(document, "name", asset.document.name, result.errors);
    std::string source;
    if (read_string(document, "source", source, result.errors)) {
        asset.source = source;
    }
    read_dimension(document, "width", asset.width, result.errors);
    read_dimension(document, "height", asset.height, result.errors);
    read_string(document, "pixelFormat", asset.pixel_format, result.errors);
    if (const auto view_iterator = document.find("view");
        view_iterator != document.end()) {
        if (!view_iterator->is_object()) {
            add_error(result.errors, ErrorCode::invalid_asset, "view",
                      "expected an object");
        } else {
            RasterView view;
            read_dimension(*view_iterator, "schemaVersion", view.schema_version,
                           result.errors);
            read_rect(*view_iterator, "crop", view.crop, result.errors);
            read_vec2(*view_iterator, "pivot", view.pivot, result.errors);
            read_transform(*view_iterator, "transform", view.transform,
                           result.errors);
            read_filter(*view_iterator, "filter", view.filter, result.errors);
            asset.view = view;
        }
    }
    if (!result.errors.empty()) {
        return result;
    }
    auto validation = validate_texture_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(asset);
    return result;
}

TextureAssetResult load_texture_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path) {
    TextureAssetResult result;
    if (!detail::is_portable_relative_path(document_path)) {
        add_error(result.errors, ErrorCode::invalid_path, "texture",
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
        add_error(result.errors, ErrorCode::invalid_path, "texture",
                  "texture document must resolve inside the project");
        return result;
    }
    std::ifstream input(canonical_document, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::missing_file, "texture",
                  "cannot open texture asset document");
        return result;
    }
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    result = parse_texture_asset(manifest, contents);
    if (!result.ok()) {
        return result;
    }
    if (document_path != texture_document_path(manifest,
                                               result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "texture",
                  "document filename does not match its resource identifier");
        return result;
    }

    filesystem_error.clear();
    const auto canonical_source = std::filesystem::weakly_canonical(
        project_root / result.asset->source, filesystem_error);
    const bool source_is_file = !filesystem_error &&
        std::filesystem::is_regular_file(canonical_source, filesystem_error);
    if (!filesystem_error && source_is_file &&
        !detail::is_within(canonical_root, canonical_source)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "source",
                  "texture source must resolve inside the project");
    } else if (filesystem_error || !source_is_file) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::missing_file, "source",
                  "texture source is missing");
    }
    return result;
}

TextureAssetResult publish_texture_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const TextureAsset& asset,
    const std::filesystem::path& validated_source) {
    TextureAssetResult result;
    auto validation = validate_texture_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto document_relative = texture_document_path(
        manifest, asset.document.id);
    auto publication = detail::publish_asset_files(
        project_root, manifest.directories.assets / "textures", asset.source,
        document_relative, validated_source, serialize_texture_asset(asset),
        "texture");
    if (!publication.ok()) {
        result.errors = std::move(publication.errors);
        return result;
    }
    return load_texture_asset(project_root, manifest, document_relative);
}

ValidationReport save_texture_asset_document(
    const std::filesystem::path& project_root, const ProjectManifest& manifest,
    const TextureAsset& asset) {
    auto report = validate_texture_asset(manifest, asset);
    if (!report.ok()) return report;
    return save_document_atomic(
        project_root, texture_document_path(manifest, asset.document.id),
        serialize_texture_asset(asset),
        [&manifest](const std::string_view contents) {
            const auto parsed = parse_texture_asset(manifest, contents);
            return ValidationReport{.errors = parsed.errors};
        });
}

} // namespace fabric::project
