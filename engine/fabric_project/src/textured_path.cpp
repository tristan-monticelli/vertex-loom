#include "fabric/project/textured_path.hpp"

#include "fabric/project/document_storage.hpp"
#include "asset_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

void reject_unknown(const Json& object,
                    const std::initializer_list<std::string_view> allowed,
                    const std::string_view field, std::vector<Error>& errors) {
    for (const auto& [key, _] : object.items()) {
        const bool known = std::ranges::any_of(
            allowed, [&](const auto candidate) { return key == candidate; });
        if (!known) {
            add_error(errors, ErrorCode::invalid_asset,
                      std::string(field) + "." + key, "unknown field");
        }
    }
}

std::string field_path(const std::string& field, const char* key) {
    return field.empty() ? std::string{key} : field + "." + key;
}

bool read_text(const Json& object, const char* key, std::string& output,
               std::vector<Error>& errors, const std::string& field = {}) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset, field_path(field, key),
                  "expected a string");
        return false;
    }
    output = iterator->get<std::string>();
    return true;
}

bool read_float(const Json& object, const char* key, float& output,
                std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number()) {
        add_error(errors, ErrorCode::invalid_asset, field_path(field, key),
                  "expected a finite number");
        return false;
    }
    output = iterator->get<float>();
    if (!std::isfinite(output)) {
        add_error(errors, ErrorCode::invalid_asset, field_path(field, key),
                  "must be finite");
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
    const auto target = field_path(field, key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, target, "expected a Vec2");
        return false;
    }
    reject_unknown(*iterator, {"x", "y"}, target, errors);
    return read_float(*iterator, "x", output.x, errors, target) &&
        read_float(*iterator, "y", output.y, errors, target);
}

Json serialize_color(const core::Color color) {
    return {{"red", color.red}, {"green", color.green},
            {"blue", color.blue}, {"alpha", color.alpha}};
}

bool read_color(const Json& object, const char* key, core::Color& output,
                std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    const auto target = field_path(field, key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, target, "expected a color");
        return false;
    }
    reject_unknown(*iterator, {"red", "green", "blue", "alpha"}, target,
                   errors);
    return read_float(*iterator, "red", output.red, errors, target) &&
        read_float(*iterator, "green", output.green, errors, target) &&
        read_float(*iterator, "blue", output.blue, errors, target) &&
        read_float(*iterator, "alpha", output.alpha, errors, target);
}

Json serialize_reference(const ResourceReference& reference) {
    return {{"id", reference.id.value},
            {"expectedType", reference.expected_type}};
}

bool read_reference(const Json& object, const char* key,
                    ResourceReference& output, std::vector<Error>& errors,
                    const std::string& field) {
    const auto iterator = object.find(key);
    const auto target = field_path(field, key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, target,
                  "expected a resource reference");
        return false;
    }
    reject_unknown(*iterator, {"id", "expectedType"}, target, errors);
    return read_text(*iterator, "id", output.id.value, errors, target) &&
        read_text(*iterator, "expectedType", output.expected_type, errors,
                  target);
}

bool finite(const core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

Json serialize_shader(const ShaderSurfaceSettings& value) {
    return {{"profile", std::string(to_string(value.profile))},
            {"classification", std::string(to_string(value.classification))},
            {"primaryColor", serialize_color(value.primary_color)},
            {"effectColor", serialize_color(value.effect_color)},
            {"shine", value.shine}, {"holography", value.holography},
            {"opacity", value.opacity}, {"intensity", value.intensity},
            {"repetition", serialize_vec2(value.repetition)},
            {"deformation", serialize_vec2(value.deformation)}};
}

void read_shader(const Json& document, ShaderSurfaceSettings& value,
                 std::vector<Error>& errors) {
    const auto item = document.find("shader");
    if (item == document.end()) return;
    if (!item->is_object()) { add_error(errors, ErrorCode::invalid_asset, "shader", "expected an object"); return; }
    std::string profile; std::string classification;
    read_text(*item, "profile", profile, errors, "shader");
    read_text(*item, "classification", classification, errors, "shader");
    if (profile == "Thread") value.profile = SurfaceShaderProfile::thread;
    else if (profile == "Plastic") value.profile = SurfaceShaderProfile::plastic;
    else if (profile == "Monochrome") value.profile = SurfaceShaderProfile::monochrome;
    else if (profile == "Custom") value.profile = SurfaceShaderProfile::custom;
    else add_error(errors, ErrorCode::invalid_asset, "shader.profile", "unsupported shader profile");
    if (classification == "floor") value.classification = TextureClassification::floor;
    else if (classification == "rope") value.classification = TextureClassification::rope;
    else if (classification == "beam") value.classification = TextureClassification::beam;
    else if (classification == "buttonEye") value.classification = TextureClassification::button_eye;
    else if (classification == "collisionMarker") value.classification = TextureClassification::collision_marker;
    else add_error(errors, ErrorCode::invalid_asset, "shader.classification", "unsupported texture classification");
    read_color(*item, "primaryColor", value.primary_color, errors, "shader");
    read_color(*item, "effectColor", value.effect_color, errors, "shader");
    read_float(*item, "shine", value.shine, errors, "shader");
    read_float(*item, "holography", value.holography, errors, "shader");
    read_float(*item, "opacity", value.opacity, errors, "shader");
    read_float(*item, "intensity", value.intensity, errors, "shader");
    read_vec2(*item, "repetition", value.repetition, errors, "shader");
    read_vec2(*item, "deformation", value.deformation, errors, "shader");
}

void validate_shader(const ShaderSurfaceSettings& value, std::vector<Error>& errors) {
    for (const auto color : {value.primary_color, value.effect_color})
        for (const auto component : {color.red, color.green, color.blue, color.alpha})
            if (!std::isfinite(component) || component < 0.0F || component > 1.0F)
                add_error(errors, ErrorCode::invalid_asset, "shader.color", "channels must be finite in [0,1]");
    for (const auto number : {value.shine, value.holography, value.opacity, value.intensity})
        if (!std::isfinite(number) || number < 0.0F)
            add_error(errors, ErrorCode::invalid_asset, "shader", "numeric settings must be finite and non-negative");
    if (!finite(value.repetition) || value.repetition.x <= 0.0F || value.repetition.y <= 0.0F)
        add_error(errors, ErrorCode::invalid_asset, "shader.repetition", "must be finite and positive");
    if (!finite(value.deformation)) add_error(errors, ErrorCode::invalid_asset, "shader.deformation", "must be finite");
}

ValidationReport parse_validation(const ProjectManifest& manifest,
                                  const std::string_view contents) {
    auto parsed = parse_textured_path(manifest, contents);
    return {.errors = std::move(parsed.errors)};
}

} // namespace

std::string_view to_string(const TexturedPathCommandKind kind) noexcept {
    switch (kind) {
    case TexturedPathCommandKind::move: return "move";
    case TexturedPathCommandKind::line: return "line";
    case TexturedPathCommandKind::cubic: return "cubic";
    }
    return "move";
}

std::string_view to_string(const TexturedPathUvMode mode) noexcept {
    switch (mode) {
    case TexturedPathUvMode::repeat: return "repeat";
    case TexturedPathUvMode::stretch: return "stretch";
    }
    return "repeat";
}

std::string_view to_string(const TexturedPathJoin join) noexcept {
    switch (join) {
    case TexturedPathJoin::miter: return "miter";
    case TexturedPathJoin::round: return "round";
    case TexturedPathJoin::bevel: return "bevel";
    }
    return "miter";
}

std::string_view to_string(const TexturedPathCap cap) noexcept {
    switch (cap) {
    case TexturedPathCap::butt: return "butt";
    case TexturedPathCap::round: return "round";
    case TexturedPathCap::square: return "square";
    }
    return "butt";
}

std::filesystem::path textured_path_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "paths" /
        (id.value + ".textured-path.json");
}

ValidationReport validate_textured_path(
    const ProjectManifest&, const TexturedPath& path) {
    ValidationReport report;
    if (path.document.schema_version != current_textured_path_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion",
                  "only textured path schema version 1 is supported");
    }
    if (path.document.type != "texturedPath") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be texturedPath");
    }
    if (!core::ResourceId::is_valid(path.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (path.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (path.commands.size() < (path.closed ? 3U : 2U) ||
        path.commands.empty() ||
        path.commands.front().kind != TexturedPathCommandKind::move) {
        add_error(report.errors, ErrorCode::invalid_asset, "commands",
                  "path requires one initial move and enough segments");
    }
    for (std::size_t index = 0; index < path.commands.size(); ++index) {
        const auto& command = path.commands[index];
        const auto field = "commands[" + std::to_string(index) + "]";
        if (index > 0U && command.kind == TexturedPathCommandKind::move) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".kind",
                      "move is only valid as the first command");
        }
        if (!finite(command.point) ||
            (command.kind == TexturedPathCommandKind::cubic &&
             (!finite(command.control1) || !finite(command.control2)))) {
            add_error(report.errors, ErrorCode::invalid_asset, field,
                      "command coordinates must be finite");
        }
        if (index > 0U && command.kind == TexturedPathCommandKind::line &&
            command.point == path.commands[index - 1U].point) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".point",
                      "line segment must have positive length");
        }
        if (index > 0U && command.kind == TexturedPathCommandKind::cubic &&
            command.point == path.commands[index - 1U].point &&
            command.control1 == command.point &&
            command.control2 == command.point) {
            add_error(report.errors, ErrorCode::invalid_asset, field,
                      "cubic segment must have positive length");
        }
    }
    if (path.closed && path.commands.size() >= 2U &&
        path.commands.front().point == path.commands.back().point) {
        add_error(
            report.errors, ErrorCode::invalid_asset, "commands",
            "closed paths close implicitly and must not duplicate the first point");
    }
    if (!std::isfinite(path.width) || path.width <= 0.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "width",
                  "must be finite and positive");
    }
    validate_shader(path.shader, report.errors);
    if (!path.width_profile.empty()) {
        if (path.width_profile.size() < 2U ||
            path.width_profile.front().position != 0.0F ||
            path.width_profile.back().position != 1.0F) {
            add_error(report.errors, ErrorCode::invalid_asset, "widthProfile",
                      "profile must start at 0 and end at 1");
        }
        float previous = -1.0F;
        for (std::size_t index = 0; index < path.width_profile.size(); ++index) {
            const auto& key = path.width_profile[index];
            const auto field = "widthProfile[" + std::to_string(index) + "]";
            if (!std::isfinite(key.position) || key.position < 0.0F ||
                key.position > 1.0F || key.position <= previous) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          field + ".position",
                          "must be finite and strictly increasing in [0,1]");
            }
            if (!std::isfinite(key.width) || key.width <= 0.0F) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          field + ".width", "must be finite and positive");
            }
            previous = key.position;
        }
    }
    if (!core::ResourceId::is_valid(path.texture.id.value) ||
        path.texture.expected_type != "texture") {
        add_error(report.errors, ErrorCode::resource_type_mismatch, "texture",
                  "must reference a texture resource");
    }
    if (!finite(path.uv_scale) || path.uv_scale.x <= 0.0F ||
        path.uv_scale.y <= 0.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "uvScale",
                  "must be finite and positive");
    }
    if (!finite(path.uv_offset)) {
        add_error(report.errors, ErrorCode::invalid_asset, "uvOffset",
                  "must be finite");
    }
    const auto valid_channel = [](const float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    if (!valid_channel(path.color.red) || !valid_channel(path.color.green) ||
        !valid_channel(path.color.blue) || !valid_channel(path.color.alpha)) {
        add_error(report.errors, ErrorCode::invalid_asset, "color",
                  "channels must be finite in [0,1]");
    }
    if (!std::isfinite(path.opacity) || path.opacity < 0.0F ||
        path.opacity > 1.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "opacity",
                  "must be finite in [0,1]");
    }
    if (!std::isfinite(path.miter_limit) || path.miter_limit < 1.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "miterLimit",
                  "must be finite and at least 1");
    }
    return report;
}

std::vector<ResourceReference> textured_path_resource_references(
    const TexturedPath& path) {
    return {path.texture};
}

std::string serialize_textured_path(const TexturedPath& path) {
    Json document{{"schemaVersion", path.document.schema_version},
                  {"type", path.document.type},
                  {"id", path.document.id.value},
                  {"name", path.document.name},
                  {"closed", path.closed},
                  {"width", path.width},
                  {"texture", serialize_reference(path.texture)},
                  {"uvMode", std::string(to_string(path.uv_mode))},
                  {"uvScale", serialize_vec2(path.uv_scale)},
                  {"uvOffset", serialize_vec2(path.uv_offset)},
                  {"color", serialize_color(path.color)},
                  {"opacity", path.opacity},
                  {"join", std::string(to_string(path.join))},
                  {"cap", std::string(to_string(path.cap))},
                  {"miterLimit", path.miter_limit},
                  {"commands", Json::array()},
                  {"widthProfile", Json::array()}};
    if (!(path.shader == ShaderSurfaceSettings{}))
        document["shader"] = serialize_shader(path.shader);
    for (const auto& command : path.commands) {
        Json serialized{{"kind", std::string(to_string(command.kind))},
                        {"point", serialize_vec2(command.point)}};
        if (command.kind == TexturedPathCommandKind::cubic) {
            serialized["control1"] = serialize_vec2(command.control1);
            serialized["control2"] = serialize_vec2(command.control2);
        }
        document["commands"].push_back(std::move(serialized));
    }
    for (const auto& key : path.width_profile) {
        document["widthProfile"].push_back(
            {{"position", key.position}, {"width", key.width}});
    }
    return document.dump(2) + "\n";
}

TexturedPathResult parse_textured_path(
    const ProjectManifest& manifest, const std::string_view contents) {
    TexturedPathResult result;
    Json document;
    try {
        document = Json::parse(contents);
    } catch (...) {
        add_error(result.errors, ErrorCode::invalid_json, "texturedPath",
                  "cannot parse textured path JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "texturedPath",
                  "top-level value must be an object");
        return result;
    }
    reject_unknown(document,
                   {"schemaVersion", "type", "id", "name", "commands",
                    "closed", "width", "widthProfile", "texture", "uvMode",
                    "uvScale", "uvOffset", "color", "opacity", "join",
                    "cap", "miterLimit", "shader"},
                   "texturedPath", result.errors);
    TexturedPath path;
    const auto schema = document.find("schemaVersion");
    if (schema == document.end() || !schema->is_number_unsigned()) {
        add_error(result.errors, ErrorCode::invalid_asset, "schemaVersion",
                  "expected an unsigned integer");
    } else {
        path.document.schema_version = schema->get<std::uint32_t>();
    }
    read_text(document, "type", path.document.type, result.errors);
    read_text(document, "id", path.document.id.value, result.errors);
    read_text(document, "name", path.document.name, result.errors);
    const auto closed = document.find("closed");
    if (closed == document.end() || !closed->is_boolean()) {
        add_error(result.errors, ErrorCode::invalid_asset, "closed",
                  "expected a boolean");
    } else {
        path.closed = closed->get<bool>();
    }
    read_float(document, "width", path.width, result.errors, "");
    read_reference(document, "texture", path.texture, result.errors, "");
    read_vec2(document, "uvScale", path.uv_scale, result.errors, "");
    read_vec2(document, "uvOffset", path.uv_offset, result.errors, "");
    read_color(document, "color", path.color, result.errors, "");
    read_float(document, "opacity", path.opacity, result.errors, "");
    read_float(document, "miterLimit", path.miter_limit, result.errors, "");
    read_shader(document, path.shader, result.errors);
    std::string uv_mode;
    std::string join;
    std::string cap;
    read_text(document, "uvMode", uv_mode, result.errors);
    read_text(document, "join", join, result.errors);
    read_text(document, "cap", cap, result.errors);
    if (uv_mode == "repeat") path.uv_mode = TexturedPathUvMode::repeat;
    else if (uv_mode == "stretch") path.uv_mode = TexturedPathUvMode::stretch;
    else if (!uv_mode.empty()) add_error(result.errors, ErrorCode::invalid_asset,
                                         "uvMode", "unsupported UV mode");
    if (join == "miter") path.join = TexturedPathJoin::miter;
    else if (join == "round") path.join = TexturedPathJoin::round;
    else if (join == "bevel") path.join = TexturedPathJoin::bevel;
    else if (!join.empty()) add_error(result.errors, ErrorCode::invalid_asset,
                                      "join", "unsupported path join");
    if (cap == "butt") path.cap = TexturedPathCap::butt;
    else if (cap == "round") path.cap = TexturedPathCap::round;
    else if (cap == "square") path.cap = TexturedPathCap::square;
    else if (!cap.empty()) add_error(result.errors, ErrorCode::invalid_asset,
                                     "cap", "unsupported path cap");

    const auto commands = document.find("commands");
    if (commands == document.end() || !commands->is_array()) {
        add_error(result.errors, ErrorCode::invalid_asset, "commands",
                  "expected an array");
    } else {
        for (std::size_t index = 0; index < commands->size(); ++index) {
            const auto& serialized = (*commands)[index];
            const auto field = "commands[" + std::to_string(index) + "]";
            if (!serialized.is_object()) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "expected an object");
                continue;
            }
            reject_unknown(serialized,
                           {"kind", "point", "control1", "control2"}, field,
                           result.errors);
            TexturedPathCommand command;
            std::string kind;
            read_text(serialized, "kind", kind, result.errors, field);
            read_vec2(serialized, "point", command.point, result.errors,
                      field);
            if (kind == "move") command.kind = TexturedPathCommandKind::move;
            else if (kind == "line") {
                command.kind = TexturedPathCommandKind::line;
            }
            else if (kind == "cubic") {
                command.kind = TexturedPathCommandKind::cubic;
                read_vec2(serialized, "control1", command.control1,
                          result.errors, field);
                read_vec2(serialized, "control2", command.control2,
                          result.errors, field);
            } else if (!kind.empty()) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".kind", "unsupported path command");
            }
            if (kind != "cubic" &&
                (serialized.contains("control1") ||
                 serialized.contains("control2"))) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "control points are only valid for cubic commands");
            }
            path.commands.push_back(command);
        }
    }
    const auto profile = document.find("widthProfile");
    if (profile == document.end() || !profile->is_array()) {
        add_error(result.errors, ErrorCode::invalid_asset, "widthProfile",
                  "expected an array");
    } else {
        for (std::size_t index = 0; index < profile->size(); ++index) {
            const auto& serialized = (*profile)[index];
            const auto field = "widthProfile[" + std::to_string(index) + "]";
            if (!serialized.is_object()) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "expected an object");
                continue;
            }
            reject_unknown(serialized, {"position", "width"}, field,
                           result.errors);
            TexturedPathWidthKey key;
            read_float(serialized, "position", key.position, result.errors,
                       field);
            read_float(serialized, "width", key.width, result.errors, field);
            path.width_profile.push_back(key);
        }
    }
    if (!result.errors.empty()) return result;
    auto validation = validate_textured_path(manifest, path);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(path);
    return result;
}

TexturedPathResult load_textured_path(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const std::filesystem::path& path) {
    auto stored = load_document(project_root, path,
        [&](const std::string_view contents) {
            return parse_validation(manifest, contents);
        });
    TexturedPathResult result;
    result.errors = std::move(stored.errors);
    if (stored.contents) result = parse_textured_path(manifest, *stored.contents);
    if (result.ok() && path != textured_path_document_path(
            manifest, result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "document",
                  "document filename does not match its id");
    }
    return result;
}

TexturedPathResult publish_textured_path(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const TexturedPath& path) {
    TexturedPathResult result;
    auto validation = validate_textured_path(manifest, path);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto document_path = textured_path_document_path(
        manifest, path.document.id);
    auto saved = save_document_atomic(
        project_root, document_path, serialize_textured_path(path),
        [&](const std::string_view contents) {
            return parse_validation(manifest, contents);
        });
    if (!saved.ok()) {
        result.errors = std::move(saved.errors);
        return result;
    }
    return load_textured_path(project_root, manifest, document_path);
}

} // namespace fabric::project
