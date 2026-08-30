#include "fabric/project/progress_save.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;

void error(std::vector<Error>& errors, ErrorCode code, std::string field, std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

Json reference(const ResourceReference& value) {
    return {{"id", value.id.value}, {"expectedType", value.expected_type}};
}

bool text(const Json& object, const char* key, std::string& out,
          std::vector<Error>& errors) {
    const auto value = object.find(key);
    if (value == object.end() || !value->is_string()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a string");
        return false;
    }
    out = value->get<std::string>();
    return true;
}

bool read_reference(const Json& object, const char* key, ResourceReference& out,
                    std::vector<Error>& errors) {
    const auto value = object.find(key);
    if (value == object.end() || !value->is_object()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a resource reference");
        return false;
    }
    return text(*value, "id", out.id.value, errors) &&
        text(*value, "expectedType", out.expected_type, errors);
}

Json progress_value(const ProgressValue& value) {
    return std::visit([](const auto& item) -> Json {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) return {{"type", "bool"}, {"value", item}};
        if constexpr (std::is_same_v<T, std::int64_t>) return {{"type", "int"}, {"value", item}};
        if constexpr (std::is_same_v<T, double>) return {{"type", "real"}, {"value", item}};
        if constexpr (std::is_same_v<T, std::string>) return {{"type", "text"}, {"value", item}};
        if constexpr (std::is_same_v<T, core::Vec2>)
            return {{"type", "vec2"}, {"value", {{"x", item.x}, {"y", item.y}}}};
        if constexpr (std::is_same_v<T, ResourceReference>)
            return {{"type", "resource"}, {"value", reference(item)}};
    }, value);
}

bool read_progress_value(const Json& object, ProgressValue& out,
                         std::vector<Error>& errors) {
    std::string type;
    if (!text(object, "type", type, errors)) return false;
    const auto value = object.find("value");
    if (value == object.end()) {
        error(errors, ErrorCode::invalid_asset, "value", "is required");
        return false;
    }
    if (type == "bool" && value->is_boolean()) out = value->get<bool>();
    else if (type == "int" && value->is_number_integer()) out = value->get<std::int64_t>();
    else if (type == "real" && value->is_number_float()) out = value->get<double>();
    else if (type == "text" && value->is_string()) out = value->get<std::string>();
    else if (type == "vec2" && value->is_object() && value->contains("x") &&
             value->contains("y") && value->at("x").is_number() && value->at("y").is_number())
        out = core::Vec2{value->at("x").get<float>(), value->at("y").get<float>()};
    else if (type == "resource" && value->is_object()) {
        ResourceReference reference_value;
        if (!text(*value, "id", reference_value.id.value, errors) ||
            !text(*value, "expectedType", reference_value.expected_type, errors)) return false;
        out = std::move(reference_value);
    } else {
        error(errors, ErrorCode::invalid_asset, "value", "does not match its declared type");
        return false;
    }
    return true;
}

ValidationReport parse_validation(std::string_view text_value) {
    const auto result = parse_progress_save(text_value);
    return {.errors = result.errors};
}
}

ValidationReport validate_progress_save(const ProgressSave& save) {
    ValidationReport report;
    if (save.schema_version != current_progress_save_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version, "schemaVersion",
              "only progress save schema version 1 is supported");
    if (save.build.empty()) error(report.errors, ErrorCode::invalid_asset, "build", "must not be empty");
    if (!core::ResourceId::is_valid(save.scene.id.value) || save.scene.expected_type != "scene")
        error(report.errors, ErrorCode::resource_type_mismatch, "scene", "must reference a scene");
    for (const auto& [key, value] : save.properties) {
        if (!core::ResourceId::is_valid(key))
            error(report.errors, ErrorCode::invalid_resource_id, "properties", "property ids must be valid");
        if (const auto real = std::get_if<double>(&value);
            real && !std::isfinite(*real))
            error(report.errors, ErrorCode::invalid_asset, "properties", "real values must be finite");
        if (const auto vector = std::get_if<core::Vec2>(&value);
            vector && (!std::isfinite(vector->x) || !std::isfinite(vector->y)))
            error(report.errors, ErrorCode::invalid_asset, "properties", "Vec2 values must be finite");
        if (const auto reference_value = std::get_if<ResourceReference>(&value);
            reference_value && (!core::ResourceId::is_valid(reference_value->id.value) ||
                                 reference_value->expected_type.empty()))
            error(report.errors, ErrorCode::invalid_asset, "properties", "resource references must be valid");
    }
    return report;
}

std::string serialize_progress_save(const ProgressSave& save) {
    Json json = {{"schemaVersion", save.schema_version}, {"build", save.build},
                 {"scene", reference(save.scene)}, {"properties", Json::object()}};
    for (const auto& [key, value] : save.properties) json["properties"][key] = progress_value(value);
    return json.dump(2) + "\n";
}

ProgressSaveResult parse_progress_save(const std::string_view json_text) {
    ProgressSaveResult result;
    Json json;
    try { json = Json::parse(json_text); }
    catch (...) { error(result.errors, ErrorCode::invalid_json, "progressSave", "cannot parse JSON"); return result; }
    if (!json.is_object()) { error(result.errors, ErrorCode::invalid_asset, "progressSave", "top-level value must be an object"); return result; }
    ProgressSave save;
    const auto version = json.find("schemaVersion");
    if (version == json.end() || !version->is_number_unsigned()) error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected an unsigned integer");
    else save.schema_version = version->get<std::uint32_t>();
    text(json, "build", save.build, result.errors);
    read_reference(json, "scene", save.scene, result.errors);
    const auto properties = json.find("properties");
    if (properties == json.end() || !properties->is_object()) error(result.errors, ErrorCode::invalid_asset, "properties", "expected an object");
    else for (const auto& [key, value] : properties->items()) {
        ProgressValue parsed;
        if (read_progress_value(value, parsed, result.errors)) save.properties.emplace(key, std::move(parsed));
    }
    if (!result.errors.empty()) return result;
    const auto validation = validate_progress_save(save);
    if (!validation.ok()) { result.errors = validation.errors; return result; }
    result.save = std::move(save);
    return result;
}

ProgressSaveResult load_progress_save(const std::filesystem::path& path) {
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    const auto stored = load_document(parent, path.filename(), parse_validation);
    ProgressSaveResult result;
    result.errors = stored.errors;
    if (stored.contents) result = parse_progress_save(*stored.contents);
    return result;
}

ValidationReport save_progress_save_atomic(const std::filesystem::path& path,
                                           const ProgressSave& save) {
    const auto validation = validate_progress_save(save);
    if (!validation.ok()) return validation;
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    return save_document_atomic(parent, path.filename(), serialize_progress_save(save), parse_validation);
}

} // namespace fabric::project
