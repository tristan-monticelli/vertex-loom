#include "fabric/project/material.hpp"

#include "fabric/project/document_storage.hpp"
#include "asset_storage.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;
void error(std::vector<Error>& e, ErrorCode c, std::string f, std::string m) {
    e.push_back({c, std::move(f), std::move(m)});
}
Json vec(core::Vec2 v) { return {{"x", v.x}, {"y", v.y}}; }
Json transform(core::Transform t) {
    return {{"position", vec(t.position)}, {"rotationDegrees", t.rotation_degrees},
            {"scale", vec(t.scale)}, {"pivot", vec(t.pivot)}};
}
bool number(const Json& o, const char* key, float& out, std::vector<Error>& e) {
    const auto i = o.find(key);
    if (i == o.end() || (!i->is_number_float() && !i->is_number_integer())) {
        error(e, ErrorCode::invalid_asset, key, "expected a finite number"); return false;
    }
    out = i->get<float>();
    if (!std::isfinite(out)) error(e, ErrorCode::invalid_asset, key, "must be finite");
    return true;
}
bool text(const Json& o, const char* key, std::string& out, std::vector<Error>& e) {
    const auto i = o.find(key);
    if (i == o.end() || !i->is_string()) { error(e, ErrorCode::invalid_asset, key, "expected a string"); return false; }
    out = i->get<std::string>(); return true;
}
bool vec_read(const Json& o, const char* key, core::Vec2& out, std::vector<Error>& e) {
    const auto i = o.find(key);
    if (i == o.end() || !i->is_object()) { error(e, ErrorCode::invalid_asset, key, "expected a Vec2"); return false; }
    return number(*i, "x", out.x, e) && number(*i, "y", out.y, e);
}
bool transform_read(const Json& o, const char* key, core::Transform& out, std::vector<Error>& e) {
    const auto i = o.find(key);
    if (i == o.end() || !i->is_object()) { error(e, ErrorCode::invalid_asset, key, "expected a transform"); return false; }
    return vec_read(*i, "position", out.position, e) && number(*i, "rotationDegrees", out.rotation_degrees, e) &&
           vec_read(*i, "scale", out.scale, e) && vec_read(*i, "pivot", out.pivot, e);
}
Json color(core::Color c) { return {{"red", c.red}, {"green", c.green}, {"blue", c.blue}, {"alpha", c.alpha}}; }
bool color_read(const Json& o, const char* key, core::Color& out, std::vector<Error>& e) {
    const auto i = o.find(key);
    if (i == o.end() || !i->is_object()) { error(e, ErrorCode::invalid_asset, key, "expected a color"); return false; }
    return number(*i, "red", out.red, e) && number(*i, "green", out.green, e) &&
           number(*i, "blue", out.blue, e) && number(*i, "alpha", out.alpha, e);
}
Json shader(const ShaderSurfaceSettings& value) {
    return {{"profile", std::string(to_string(value.profile))},
            {"classification", std::string(to_string(value.classification))},
            {"primaryColor", color(value.primary_color)},
            {"effectColor", color(value.effect_color)},
            {"shine", value.shine}, {"holography", value.holography},
            {"opacity", value.opacity}, {"intensity", value.intensity},
            {"repetition", vec(value.repetition)},
            {"deformation", vec(value.deformation)}};
}
bool shader_read(const Json& o, std::optional<ShaderSurfaceSettings>& out,
                 std::vector<Error>& e) {
    const auto i = o.find("shader");
    if (i == o.end() || i->is_null()) return true;
    if (!i->is_object()) {
        error(e, ErrorCode::invalid_asset, "shader", "expected an object");
        return false;
    }
    ShaderSurfaceSettings value;
    std::string profile;
    std::string classification;
    text(*i, "profile", profile, e);
    text(*i, "classification", classification, e);
    if (profile == "Thread") value.profile = SurfaceShaderProfile::thread;
    else if (profile == "Plastic") value.profile = SurfaceShaderProfile::plastic;
    else if (profile == "Monochrome") value.profile = SurfaceShaderProfile::monochrome;
    else if (profile == "Custom") value.profile = SurfaceShaderProfile::custom;
    else error(e, ErrorCode::invalid_asset, "shader.profile", "unsupported shader profile");
    if (classification == "floor") value.classification = TextureClassification::floor;
    else if (classification == "rope") value.classification = TextureClassification::rope;
    else if (classification == "beam") value.classification = TextureClassification::beam;
    else if (classification == "buttonEye") value.classification = TextureClassification::button_eye;
    else if (classification == "collisionMarker") value.classification = TextureClassification::collision_marker;
    else error(e, ErrorCode::invalid_asset, "shader.classification", "unsupported texture classification");
    color_read(*i, "primaryColor", value.primary_color, e);
    color_read(*i, "effectColor", value.effect_color, e);
    number(*i, "shine", value.shine, e);
    number(*i, "holography", value.holography, e);
    number(*i, "opacity", value.opacity, e);
    number(*i, "intensity", value.intensity, e);
    vec_read(*i, "repetition", value.repetition, e);
    vec_read(*i, "deformation", value.deformation, e);
    if (e.empty()) out = value;
    return e.empty();
}
bool ref_read(const Json& o, const char* key, std::optional<ResourceReference>& out,
              std::vector<Error>& e) {
    const auto i = o.find(key); if (i == o.end() || i->is_null()) return true;
    if (!i->is_object()) { error(e, ErrorCode::invalid_asset, key, "expected a resource reference"); return false; }
    ResourceReference ref; if (!text(*i, "id", ref.id.value, e) || !text(*i, "expectedType", ref.expected_type, e)) return false;
    out = std::move(ref); return true;
}
ValidationReport parse_validation(const ProjectManifest& m, std::string_view s) {
    auto r = parse_material(m, s); return {.errors = std::move(r.errors)};
}
} // namespace

std::string_view to_string(const MaterialBlendMode mode) noexcept {
    switch (mode) { case MaterialBlendMode::normal: return "normal"; case MaterialBlendMode::additive: return "additive"; case MaterialBlendMode::multiply: return "multiply"; case MaterialBlendMode::screen: return "screen"; }
    return "normal";
}
std::filesystem::path material_document_path(const ProjectManifest& m, const core::ResourceId& id) {
    return m.directories.assets / "materials" / (id.value + ".material.json");
}
ValidationReport validate_material(const ProjectManifest&, const MaterialDefinition& a) {
    ValidationReport r;
    if (a.document.schema_version != current_material_schema_version) error(r.errors, ErrorCode::unsupported_schema_version, "schemaVersion", "only material schema version 2 is supported");
    if (a.document.type != "material") error(r.errors, ErrorCode::invalid_asset, "type", "must be material");
    if (!core::ResourceId::is_valid(a.document.id.value)) error(r.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (a.document.name.empty()) error(r.errors, ErrorCode::invalid_asset, "name", "must not be empty");
    for (const auto& [name, v] : {std::pair{"red", a.color.red}, {"green", a.color.green}, {"blue", a.color.blue}, {"alpha", a.color.alpha}}) if (!std::isfinite(v) || v < 0.0F || v > 1.0F) error(r.errors, ErrorCode::invalid_asset, "color." + std::string(name), "must be finite in [0,1]");
    if (!std::isfinite(a.opacity) || a.opacity < 0.0F || a.opacity > 1.0F) error(r.errors, ErrorCode::invalid_asset, "opacity", "must be finite in [0,1]");
    if (a.shader) {
        for (const auto shader_color : {a.shader->primary_color,
                                        a.shader->effect_color})
            for (const auto channel : {shader_color.red, shader_color.green,
                                       shader_color.blue, shader_color.alpha})
                if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F)
                    error(r.errors, ErrorCode::invalid_asset, "shader.color",
                          "channels must be finite in [0,1]");
        for (const auto value : {a.shader->shine, a.shader->holography,
                                 a.shader->opacity, a.shader->intensity})
            if (!std::isfinite(value) || value < 0.0F)
                error(r.errors, ErrorCode::invalid_asset, "shader",
                      "numeric settings must be finite and non-negative");
        if (!std::isfinite(a.shader->repetition.x) ||
            !std::isfinite(a.shader->repetition.y) ||
            a.shader->repetition.x <= 0.0F || a.shader->repetition.y <= 0.0F)
            error(r.errors, ErrorCode::invalid_asset, "shader.repetition",
                  "must be finite and positive");
        if (!std::isfinite(a.shader->deformation.x) ||
            !std::isfinite(a.shader->deformation.y))
            error(r.errors, ErrorCode::invalid_asset, "shader.deformation",
                  "must be finite");
    }
    const auto valid_ref = [&](const auto& ref, const char* field, const char* type) { if (!ref) return; if (!core::ResourceId::is_valid(ref->id.value) || ref->expected_type != type) error(r.errors, ErrorCode::resource_type_mismatch, field, "reference has an invalid id or expected type"); };
    valid_ref(a.texture, "texture", "texture"); valid_ref(a.vector_pattern, "vector", "vector");
    const auto finite = [&](core::Vec2 v, const char* f) { if (!std::isfinite(v.x) || !std::isfinite(v.y)) error(r.errors, ErrorCode::invalid_asset, f, "must be finite"); };
    finite(a.uv_transform.position, "uvTransform.position"); finite(a.uv_transform.scale, "uvTransform.scale"); finite(a.uv_transform.pivot, "uvTransform.pivot");
    if (!std::isfinite(a.uv_transform.rotation_degrees)) error(r.errors, ErrorCode::invalid_asset, "uvTransform.rotationDegrees", "must be finite");
    return r;
}
std::vector<ResourceReference> material_resource_references(const MaterialDefinition& a) {
    std::vector<ResourceReference> r; if (a.texture) r.push_back(*a.texture); if (a.vector_pattern) r.push_back(*a.vector_pattern); return r;
}
std::string serialize_material(const MaterialDefinition& a) {
    Json j = {{"schemaVersion", a.document.schema_version}, {"type", a.document.type}, {"id", a.document.id.value}, {"name", a.document.name}, {"color", color(a.color)}, {"opacity", a.opacity}, {"blend", std::string(to_string(a.blend))}, {"uvTransform", transform(a.uv_transform)}};
    if (a.texture) j["texture"] = {{"id", a.texture->id.value}, {"expectedType", a.texture->expected_type}};
    if (a.vector_pattern) j["vectorPattern"] = {{"id", a.vector_pattern->id.value}, {"expectedType", a.vector_pattern->expected_type}};
    if (a.shader) j["shader"] = shader(*a.shader);
    return j.dump(2) + "\n";
}
MaterialResult parse_material(const ProjectManifest& m, const std::string_view s) {
    MaterialResult r; Json j; try { j = Json::parse(s); } catch (...) { error(r.errors, ErrorCode::invalid_json, "material", "cannot parse material JSON"); return r; }
    if (!j.is_object()) { error(r.errors, ErrorCode::invalid_asset, "material", "top-level value must be an object"); return r; }
    MaterialDefinition a; std::string blend;
    const auto schema = j.find("schemaVersion"); if (schema == j.end() || !schema->is_number_unsigned()) error(r.errors, ErrorCode::invalid_asset, "schemaVersion", "expected an unsigned integer"); else a.document.schema_version = schema->get<std::uint32_t>();
    const bool legacy_v1 = a.document.schema_version == 1U;
    text(j, "type", a.document.type, r.errors); text(j, "id", a.document.id.value, r.errors); text(j, "name", a.document.name, r.errors); color_read(j, "color", a.color, r.errors); number(j, "opacity", a.opacity, r.errors); text(j, "blend", blend, r.errors); transform_read(j, "uvTransform", a.uv_transform, r.errors); ref_read(j, "texture", a.texture, r.errors); ref_read(j, "vectorPattern", a.vector_pattern, r.errors); if (!legacy_v1) shader_read(j, a.shader, r.errors); else a.document.schema_version = current_material_schema_version;
    if (blend == "normal") a.blend = MaterialBlendMode::normal; else if (blend == "additive") a.blend = MaterialBlendMode::additive; else if (blend == "multiply") a.blend = MaterialBlendMode::multiply; else if (blend == "screen") a.blend = MaterialBlendMode::screen; else error(r.errors, ErrorCode::invalid_asset, "blend", "unsupported blend mode");
    if (!r.errors.empty()) return r; auto v = validate_material(m, a); if (!v.ok()) { r.errors = std::move(v.errors); return r; } r.asset = std::move(a); return r;
}
MaterialResult load_material(const std::filesystem::path& root, const ProjectManifest& m, const std::filesystem::path& path) {
    auto stored = load_document(root, path, [&](std::string_view s) { return parse_validation(m, s); }); MaterialResult r; r.errors = std::move(stored.errors); if (stored.contents) r = parse_material(m, *stored.contents); if (r.ok() && path != material_document_path(m, r.asset->document.id)) { r.asset.reset(); error(r.errors, ErrorCode::invalid_path, "document", "document filename does not match its id"); } return r;
}
MaterialResult publish_material(const std::filesystem::path& root, const ProjectManifest& m, const MaterialDefinition& a) {
    MaterialResult r; auto v = validate_material(m, a); if (!v.ok()) { r.errors = std::move(v.errors); return r; } const auto path = material_document_path(m, a.document.id); auto saved = save_document_atomic(root, path, serialize_material(a), [&](std::string_view s) { return parse_validation(m, s); }); if (!saved.ok()) { r.errors = std::move(saved.errors); return r; } return load_material(root, m, path);
}
} // namespace fabric::project
