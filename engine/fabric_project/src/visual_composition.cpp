#include "fabric/project/visual_composition.hpp"

#include "fabric/project/document_storage.hpp"
#include "asset_storage.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

bool reject_unknown(const Json& object,
                    const std::initializer_list<std::string_view> allowed,
                    const std::string_view field, std::vector<Error>& errors) {
    bool valid = true;
    for (const auto& [key, _] : object.items()) {
        bool known = false;
        for (const auto candidate : allowed) {
            if (key == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            add_error(errors, ErrorCode::invalid_asset,
                      std::string(field) + "." + key,
                      "unknown field");
            valid = false;
        }
    }
    return valid;
}

bool read_text(const Json& object, const char* key, std::string& output,
               std::vector<Error>& errors, const std::string& field = {}) {
    const auto iterator = object.find(key);
    const auto path = field.empty() ? std::string{key} : field + "." + key;
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset, path, "expected a string");
        return false;
    }
    output = iterator->get<std::string>();
    return true;
}

bool read_float(const Json& object, const char* key, float& output,
                std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    const auto path = field.empty() ? std::string{key} : field + "." + key;
    if (iterator == object.end() || !iterator->is_number()) {
        add_error(errors, ErrorCode::invalid_asset, path,
                  "expected a finite number");
        return false;
    }
    output = iterator->get<float>();
    if (!std::isfinite(output)) {
        add_error(errors, ErrorCode::invalid_asset, path, "must be finite");
        return false;
    }
    return true;
}

Json serialize_vec2(const core::Vec2 value) {
    return {{"x", value.x}, {"y", value.y}};
}

bool read_vec2(const Json& object, const char* key, core::Vec2& output,
               std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    const auto path = field.empty() ? std::string{key} : field + "." + key;
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, path, "expected a Vec2");
        return false;
    }
    reject_unknown(*iterator, {"x", "y"}, path, errors);
    return read_float(*iterator, "x", output.x, errors, path) &&
        read_float(*iterator, "y", output.y, errors, path);
}

Json serialize_transform(const core::Transform& value) {
    return {{"position", serialize_vec2(value.position)},
            {"rotationDegrees", value.rotation_degrees},
            {"scale", serialize_vec2(value.scale)},
            {"pivot", serialize_vec2(value.pivot)}};
}

bool read_transform(const Json& object, const char* key,
                    core::Transform& output, std::vector<Error>& errors,
                    const std::string& field) {
    const auto iterator = object.find(key);
    const auto path = field.empty() ? std::string{key} : field + "." + key;
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, path,
                  "expected a transform");
        return false;
    }
    reject_unknown(*iterator,
                   {"position", "rotationDegrees", "scale", "pivot"},
                   path, errors);
    return read_vec2(*iterator, "position", output.position, errors, path) &&
        read_float(*iterator, "rotationDegrees", output.rotation_degrees,
                   errors, path) &&
        read_vec2(*iterator, "scale", output.scale, errors, path) &&
        read_vec2(*iterator, "pivot", output.pivot, errors, path);
}

Json serialize_rect(const core::Rect& value) {
    return {{"origin", serialize_vec2(value.origin)},
            {"size", serialize_vec2(value.size)}};
}

bool read_rect(const Json& object, const char* key, core::Rect& output,
               std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    const auto path = field + "." + key;
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, path, "expected a Rect");
        return false;
    }
    reject_unknown(*iterator, {"origin", "size"}, path, errors);
    return read_vec2(*iterator, "origin", output.origin, errors, path) &&
        read_vec2(*iterator, "size", output.size, errors, path);
}

Json serialize_reference(const ResourceReference& reference) {
    return {{"id", reference.id.value},
            {"expectedType", reference.expected_type}};
}

bool read_reference(const Json& object, const char* key,
                    ResourceReference& output, std::vector<Error>& errors,
                    const std::string& field) {
    const auto iterator = object.find(key);
    const auto path = field + "." + key;
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, path,
                  "expected a resource reference");
        return false;
    }
    reject_unknown(*iterator, {"id", "expectedType"}, path, errors);
    return read_text(*iterator, "id", output.id.value, errors, path) &&
        read_text(*iterator, "expectedType", output.expected_type, errors,
                  path);
}

Json serialize_raster_view(const RasterView& view) {
    return {{"schemaVersion", view.schema_version},
            {"crop", serialize_rect(view.crop)},
            {"pivot", serialize_vec2(view.pivot)},
            {"transform", serialize_transform(view.transform)},
            {"filter", std::string(to_string(view.filter))}};
}

bool read_raster_view(const Json& object, RasterView& output,
                      std::vector<Error>& errors, const std::string& field) {
    reject_unknown(object,
                   {"schemaVersion", "crop", "pivot", "transform", "filter"},
                   field, errors);
    const auto schema = object.find("schemaVersion");
    if (schema == object.end() || !schema->is_number_unsigned()) {
        add_error(errors, ErrorCode::invalid_asset, field + ".schemaVersion",
                  "expected an unsigned integer");
    } else {
        output.schema_version = schema->get<std::uint32_t>();
    }
    std::string filter;
    const bool parsed = read_rect(object, "crop", output.crop, errors, field) &&
        read_vec2(object, "pivot", output.pivot, errors, field) &&
        read_transform(object, "transform", output.transform, errors, field) &&
        read_text(object, "filter", filter, errors, field);
    if (filter == "nearest") {
        output.filter = RasterFilter::nearest;
    } else if (filter == "linear") {
        output.filter = RasterFilter::linear;
    } else if (!filter.empty()) {
        add_error(errors, ErrorCode::invalid_asset, field + ".filter",
                  "unsupported raster filter");
    }
    return parsed;
}

const char* expected_type(const VisualLayerKind kind) {
    switch (kind) {
    case VisualLayerKind::raster: return "texture";
    case VisualLayerKind::vector: return "vector";
    case VisualLayerKind::component: return "visualComponent";
    case VisualLayerKind::textured_path: return "texturedPath";
    }
    return "";
}

bool finite(const core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

ValidationReport parse_validation(const ProjectManifest& manifest,
                                  const std::string_view contents) {
    auto parsed = parse_visual_composition(manifest, contents);
    return {.errors = std::move(parsed.errors)};
}

} // namespace

std::string_view to_string(const VisualLayerKind kind) noexcept {
    switch (kind) {
    case VisualLayerKind::raster: return "raster";
    case VisualLayerKind::vector: return "vector";
    case VisualLayerKind::component: return "component";
    case VisualLayerKind::textured_path: return "texturedPath";
    }
    return "raster";
}

std::filesystem::path visual_composition_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "compositions" /
        (id.value + ".composition.json");
}

ValidationReport validate_visual_composition(
    const ProjectManifest&, const VisualComposition& composition) {
    ValidationReport report;
    if (composition.document.schema_version !=
        current_visual_composition_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion",
                  "only visual composition schema version 1 is supported");
    }
    if (composition.document.type != "visualComposition") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be visualComposition");
    }
    if (!core::ResourceId::is_valid(composition.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (composition.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (!finite(composition.size) || composition.size.x <= 0.0F ||
        composition.size.y <= 0.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "size",
                  "must be finite and positive");
    }
    std::set<std::string> layer_ids;
    for (std::size_t index = 0; index < composition.layers.size(); ++index) {
        const auto& layer = composition.layers[index];
        const auto field = "layers[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(layer.id)) {
            add_error(report.errors, ErrorCode::invalid_resource_id,
                      field + ".id", "must be a valid stable identifier");
        } else if (!layer_ids.insert(layer.id).second) {
            add_error(report.errors, ErrorCode::duplicate_resource,
                      field + ".id", "layer identifier must be unique");
        }
        if (layer.name.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".name",
                      "must not be empty");
        }
        if (!core::ResourceId::is_valid(layer.resource.id.value) ||
            layer.resource.expected_type != expected_type(layer.kind)) {
            add_error(report.errors, ErrorCode::resource_type_mismatch,
                      field + ".resource",
                      "reference id or expected type does not match layer kind");
        }
        if (!finite(layer.anchor) || layer.anchor.x < 0.0F ||
            layer.anchor.x > 1.0F || layer.anchor.y < 0.0F ||
            layer.anchor.y > 1.0F) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".anchor", "must be finite in [0,1]");
        }
        if (!finite(layer.transform.position) ||
            !finite(layer.transform.scale) || !finite(layer.transform.pivot) ||
            !std::isfinite(layer.transform.rotation_degrees)) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".transform", "all transform values must be finite");
        }
        if (!std::isfinite(layer.opacity) || layer.opacity < 0.0F ||
            layer.opacity > 1.0F) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".opacity", "must be finite in [0,1]");
        }
        if (!std::isfinite(layer.z_order)) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".zOrder", "must be finite");
        }
        if (layer.kind != VisualLayerKind::raster && layer.raster_view) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".rasterView",
                      "is only valid for raster layers");
        }
        if (layer.raster_view) {
            const auto view_validation = validate_raster_view(
                *layer.raster_view, std::numeric_limits<std::uint32_t>::max(),
                std::numeric_limits<std::uint32_t>::max());
            for (const auto& error : view_validation.errors) {
                add_error(report.errors, error.code,
                          field + ".rasterView." + error.field,
                          error.message);
            }
        }
    }
    return report;
}

std::vector<ResourceReference> visual_composition_resource_references(
    const VisualComposition& composition) {
    std::vector<ResourceReference> references;
    references.reserve(composition.layers.size());
    for (const auto& layer : composition.layers) {
        references.push_back(layer.resource);
    }
    return references;
}

std::string serialize_visual_composition(const VisualComposition& composition) {
    Json document{{"schemaVersion", composition.document.schema_version},
                  {"type", composition.document.type},
                  {"id", composition.document.id.value},
                  {"name", composition.document.name},
                  {"size", serialize_vec2(composition.size)},
                  {"layers", Json::array()}};
    for (const auto& layer : composition.layers) {
        Json serialized{{"id", layer.id},
                        {"name", layer.name},
                        {"kind", std::string(to_string(layer.kind))},
                        {"resource", serialize_reference(layer.resource)},
                        {"anchor", serialize_vec2(layer.anchor)},
                        {"transform", serialize_transform(layer.transform)},
                        {"visible", layer.visible},
                        {"opacity", layer.opacity},
                        {"zOrder", layer.z_order}};
        if (layer.raster_view) {
            serialized["rasterView"] = serialize_raster_view(*layer.raster_view);
        }
        document["layers"].push_back(std::move(serialized));
    }
    return document.dump(2) + "\n";
}

VisualCompositionResult parse_visual_composition(
    const ProjectManifest& manifest, const std::string_view contents) {
    VisualCompositionResult result;
    Json document;
    try {
        document = Json::parse(contents);
    } catch (...) {
        add_error(result.errors, ErrorCode::invalid_json, "visualComposition",
                  "cannot parse visual composition JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "visualComposition",
                  "top-level value must be an object");
        return result;
    }
    reject_unknown(document, {"schemaVersion", "type", "id", "name", "size",
                              "layers"}, "visualComposition", result.errors);
    VisualComposition composition;
    const auto schema = document.find("schemaVersion");
    if (schema == document.end() || !schema->is_number_unsigned()) {
        add_error(result.errors, ErrorCode::invalid_asset, "schemaVersion",
                  "expected an unsigned integer");
    } else {
        composition.document.schema_version = schema->get<std::uint32_t>();
    }
    read_text(document, "type", composition.document.type, result.errors);
    read_text(document, "id", composition.document.id.value, result.errors);
    read_text(document, "name", composition.document.name, result.errors);
    read_vec2(document, "size", composition.size, result.errors, "");
    const auto layers = document.find("layers");
    if (layers == document.end() || !layers->is_array()) {
        add_error(result.errors, ErrorCode::invalid_asset, "layers",
                  "expected an array");
    } else {
        composition.layers.reserve(layers->size());
        for (std::size_t index = 0; index < layers->size(); ++index) {
            const auto& serialized = (*layers)[index];
            const auto field = "layers[" + std::to_string(index) + "]";
            if (!serialized.is_object()) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "expected an object");
                continue;
            }
            reject_unknown(serialized,
                           {"id", "name", "kind", "resource", "anchor",
                            "transform", "visible", "opacity", "zOrder",
                            "rasterView"}, field, result.errors);
            VisualCompositionLayer layer;
            std::string kind;
            read_text(serialized, "id", layer.id, result.errors, field);
            read_text(serialized, "name", layer.name, result.errors, field);
            read_text(serialized, "kind", kind, result.errors, field);
            read_reference(serialized, "resource", layer.resource,
                           result.errors, field);
            read_vec2(serialized, "anchor", layer.anchor, result.errors, field);
            read_transform(serialized, "transform", layer.transform,
                           result.errors, field);
            const auto visible = serialized.find("visible");
            if (visible == serialized.end() || !visible->is_boolean()) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".visible", "expected a boolean");
            } else {
                layer.visible = visible->get<bool>();
            }
            read_float(serialized, "opacity", layer.opacity, result.errors,
                       field);
            read_float(serialized, "zOrder", layer.z_order, result.errors,
                       field);
            if (kind == "raster") layer.kind = VisualLayerKind::raster;
            else if (kind == "vector") layer.kind = VisualLayerKind::vector;
            else if (kind == "component") layer.kind = VisualLayerKind::component;
            else if (kind == "texturedPath") {
                layer.kind = VisualLayerKind::textured_path;
            } else if (!kind.empty()) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".kind", "unsupported visual layer kind");
            }
            const auto raster_view = serialized.find("rasterView");
            if (raster_view != serialized.end()) {
                if (!raster_view->is_object()) {
                    add_error(result.errors, ErrorCode::invalid_asset,
                              field + ".rasterView", "expected an object");
                } else {
                    RasterView view;
                    read_raster_view(*raster_view, view, result.errors,
                                     field + ".rasterView");
                    layer.raster_view = std::move(view);
                }
            }
            composition.layers.push_back(std::move(layer));
        }
    }
    if (!result.errors.empty()) return result;
    auto validation = validate_visual_composition(manifest, composition);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(composition);
    return result;
}

VisualCompositionResult load_visual_composition(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const std::filesystem::path& path) {
    auto stored = load_document(project_root, path,
        [&](const std::string_view contents) {
            return parse_validation(manifest, contents);
        });
    VisualCompositionResult result;
    result.errors = std::move(stored.errors);
    if (stored.contents) {
        result = parse_visual_composition(manifest, *stored.contents);
    }
    if (result.ok() && path != visual_composition_document_path(
            manifest, result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "document",
                  "document filename does not match its id");
    }
    return result;
}

VisualCompositionResult publish_visual_composition(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const VisualComposition& composition) {
    VisualCompositionResult result;
    auto validation = validate_visual_composition(manifest, composition);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto path = visual_composition_document_path(
        manifest, composition.document.id);
    auto saved = save_document_atomic(
        project_root, path, serialize_visual_composition(composition),
        [&](const std::string_view contents) {
            return parse_validation(manifest, contents);
        });
    if (!saved.ok()) {
        result.errors = std::move(saved.errors);
        return result;
    }
    return load_visual_composition(project_root, manifest, path);
}

} // namespace fabric::project
