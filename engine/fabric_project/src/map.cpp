#include "fabric/project/map.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <type_traits>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}
Json vec(core::Vec2 value) { return {{"x", value.x}, {"y", value.y}}; }
Json transform(core::Transform value) {
    return {{"position", vec(value.position)}, {"rotationDegrees", value.rotation_degrees},
            {"scale", vec(value.scale)}, {"pivot", vec(value.pivot)}};
}
Json ref(const ResourceReference& value) {
    return {{"id", value.id.value}, {"expectedType", value.expected_type}};
}
bool text(const Json& object, const char* key, std::string& out,
          std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_string()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a string");
        return false;
    }
    out = item->get<std::string>();
    return true;
}
bool number(const Json& object, const char* key, float& out,
            std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || (!item->is_number_float() && !item->is_number_integer())) {
        error(errors, ErrorCode::invalid_asset, key, "expected a finite number");
        return false;
    }
    out = item->get<float>();
    if (!std::isfinite(out)) error(errors, ErrorCode::invalid_asset, key, "must be finite");
    return true;
}
bool integer(const Json& object, const char* key, std::int64_t& out,
             std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_number_integer()) {
        error(errors, ErrorCode::invalid_asset, key, "expected an integer");
        return false;
    }
    out = item->get<std::int64_t>();
    return true;
}
bool vec_read(const Json& object, const char* key, core::Vec2& out,
              std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_object()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a Vec2");
        return false;
    }
    return number(*item, "x", out.x, errors) && number(*item, "y", out.y, errors);
}
bool transform_read(const Json& object, const char* key, core::Transform& out,
                    std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_object()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a transform");
        return false;
    }
    return vec_read(*item, "position", out.position, errors) &&
        number(*item, "rotationDegrees", out.rotation_degrees, errors) &&
        vec_read(*item, "scale", out.scale, errors) && vec_read(*item, "pivot", out.pivot, errors);
}
bool ref_read(const Json& object, const char* key, std::optional<ResourceReference>& out,
              std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || item->is_null()) return true;
    if (!item->is_object()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a resource reference");
        return false;
    }
    ResourceReference value;
    if (!text(*item, "id", value.id.value, errors) ||
        !text(*item, "expectedType", value.expected_type, errors)) return false;
    out = std::move(value);
    return true;
}
Json property_value(const MapPropertyValue& value) {
    return std::visit([](const auto& item) -> Json {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) return {{"kind", "bool"}, {"value", item}};
        else if constexpr (std::is_same_v<T, std::int64_t>) return {{"kind", "integer"}, {"value", item}};
        else if constexpr (std::is_same_v<T, float>) return {{"kind", "real"}, {"value", item}};
        else if constexpr (std::is_same_v<T, std::string>) return {{"kind", "text"}, {"value", item}};
        else if constexpr (std::is_same_v<T, core::Vec2>) return {{"kind", "vec2"}, {"value", vec(item)}};
        else return {{"kind", "resource"}, {"value", ref(item)}};
    }, value);
}
bool property_value_read(const Json& object, MapPropertyValue& out,
                         std::vector<Error>& errors) {
    std::string kind;
    if (!text(object, "kind", kind, errors)) return false;
    const auto item = object.find("value");
    if (item == object.end()) {
        error(errors, ErrorCode::invalid_asset, "value", "missing property value");
        return false;
    }
    if (kind == "bool" && item->is_boolean()) out = item->get<bool>();
    else if (kind == "integer" && item->is_number_integer()) out = item->get<std::int64_t>();
    else if (kind == "real" && (item->is_number_float() || item->is_number_integer())) out = item->get<float>();
    else if (kind == "text" && item->is_string()) out = item->get<std::string>();
    else if (kind == "vec2" && item->is_object()) {
        core::Vec2 value{};
        if (!number(*item, "x", value.x, errors) || !number(*item, "y", value.y, errors)) return false;
        out = value;
    } else if (kind == "resource" && item->is_object()) {
        ResourceReference value;
        if (!text(*item, "id", value.id.value, errors) || !text(*item, "expectedType", value.expected_type, errors)) return false;
        out = std::move(value);
    } else {
        error(errors, ErrorCode::invalid_asset, "value", "property value kind does not match value");
        return false;
    }
    return true;
}
Json properties(const std::vector<MapProperty>& values) {
    Json result = Json::array();
    for (const auto& property : values)
        result.push_back({{"id", property.id}, {"value", property_value(property.value)}});
    return result;
}
bool properties_read(const Json& object, const char* key, std::vector<MapProperty>& out,
                     std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_array()) {
        error(errors, ErrorCode::invalid_asset, key, "expected an array");
        return false;
    }
    for (const auto& value : *item) {
        MapProperty property;
        if (!text(value, "id", property.id, errors)) continue;
        const auto property_value_item = value.find("value");
        if (property_value_item == value.end() || !property_value_item->is_object() ||
            !property_value_read(*property_value_item, property.value, errors)) continue;
        out.push_back(std::move(property));
    }
    return true;
}
bool layer_exists(const MapDocument& map, const std::string& id, MapLayerKind kind) {
    for (const auto& layer : map.layers)
        if (layer.id == id && layer.kind == kind) return true;
    return false;
}
ValidationReport parse_validation(const ProjectManifest& manifest, std::string_view json) {
    const auto parsed = parse_map(manifest, json);
    return {.errors = parsed.errors};
}
} // namespace

std::string_view to_string(const MapLayerKind kind) noexcept {
    switch (kind) {
    case MapLayerKind::visual: return "visual";
    case MapLayerKind::tiles: return "tiles";
    case MapLayerKind::instances: return "instances";
    case MapLayerKind::collision: return "collision";
    case MapLayerKind::triggers: return "triggers";
    case MapLayerKind::gameplay: return "gameplay";
    }
    return "visual";
}

std::filesystem::path map_document_path(const ProjectManifest& manifest,
                                         const core::ResourceId& id) {
    return manifest.directories.maps / (id.value + ".map.json");
}

ValidationReport validate_map(const ProjectManifest&, const MapDocument& map) {
    ValidationReport report;
    if (map.document.schema_version != current_map_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version, "schemaVersion", "only map schema version 1 is supported");
    if (map.document.type != "map") error(report.errors, ErrorCode::invalid_asset, "type", "must be map");
    if (!core::ResourceId::is_valid(map.document.id.value)) error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (map.document.name.empty()) error(report.errors, ErrorCode::invalid_asset, "name", "must not be empty");
    if (map.chunk_size != map_chunk_size) error(report.errors, ErrorCode::invalid_asset, "chunkSize", "must be 64 units");
    std::set<std::string> layer_ids;
    for (const auto& layer : map.layers) {
        if (!core::ResourceId::is_valid(layer.id) || !layer_ids.insert(layer.id).second || layer.name.empty() || !std::isfinite(layer.depth))
            error(report.errors, ErrorCode::invalid_asset, "layers", "layers require unique ids, names and finite depth");
    }
    std::set<std::string> prefab_ids;
    for (const auto& prefab : map.prefabs) {
        if (!core::ResourceId::is_valid(prefab.id) || !prefab_ids.insert(prefab.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "prefabs.id", "prefab ids must be valid and unique");
        if (!core::ResourceId::is_valid(prefab.entity.id.value) || prefab.entity.expected_type != "entity")
            error(report.errors, ErrorCode::resource_type_mismatch, "prefabs.entity", "prefab must reference an entity");
    }
    std::set<std::string> instance_ids;
    for (const auto& instance : map.instances) {
        if (!core::ResourceId::is_valid(instance.id) || !instance_ids.insert(instance.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "instances.id", "instance ids must be valid and unique");
        if (!layer_exists(map, instance.layer_id, MapLayerKind::instances))
            error(report.errors, ErrorCode::missing_resource, "instances.layer", "instance layer is missing");
        if ((!instance.entity && !instance.prefab) || (instance.entity && instance.prefab))
            error(report.errors, ErrorCode::invalid_asset, "instances.resource", "instance needs exactly one entity or prefab");
        if (instance.entity && (instance.entity->expected_type != "entity" || !core::ResourceId::is_valid(instance.entity->id.value)))
            error(report.errors, ErrorCode::resource_type_mismatch, "instances.entity", "invalid entity reference");
        if (instance.prefab && (instance.prefab->expected_type != "prefab" || !core::ResourceId::is_valid(instance.prefab->id.value)))
            error(report.errors, ErrorCode::resource_type_mismatch, "instances.prefab", "invalid prefab reference");
        const auto& position = instance.transform.position;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(instance.transform.rotation_degrees) ||
            !std::isfinite(instance.transform.scale.x) || !std::isfinite(instance.transform.scale.y))
            error(report.errors, ErrorCode::invalid_asset, "instances.transform", "must be finite");
        bool has_animation = false;
        for (const auto& property : instance.properties) {
            if (property.id != "animation") continue;
            if (has_animation) {
                error(report.errors, ErrorCode::duplicate_resource,
                      "instances.animation", "animation property must be unique");
                continue;
            }
            has_animation = true;
            const auto* reference = std::get_if<ResourceReference>(&property.value);
            if (reference == nullptr || reference->expected_type != "animation" ||
                !core::ResourceId::is_valid(reference->id.value)) {
                error(report.errors, ErrorCode::resource_type_mismatch,
                      "instances.animation",
                      "animation property must reference a valid animation resource");
            }
        }
        const auto expected_x = static_cast<std::int32_t>(std::floor(position.x / map.chunk_size));
        const auto expected_y = static_cast<std::int32_t>(std::floor(position.y / map.chunk_size));
        if (instance.chunk_x != expected_x || instance.chunk_y != expected_y)
            error(report.errors, ErrorCode::invalid_asset, "instances.chunk", "chunk coordinate does not match position");
    }
    for (const auto& collision : map.collisions) {
        if (!layer_exists(map, collision.layer_id, MapLayerKind::collision))
            error(report.errors, ErrorCode::missing_resource, "collisions.layer", "collision layer is missing");
        if (!std::isfinite(collision.radius) || collision.radius < 0.0F || !std::isfinite(collision.length) || collision.length < 0.0F)
            error(report.errors, ErrorCode::invalid_asset, "collisions", "dimensions must be finite and non-negative");
        const auto minimum_points = collision.kind == CollisionShapeKind::polygon ? 3U : (collision.kind == CollisionShapeKind::chain ? 2U : 0U);
        if (collision.points.size() < minimum_points)
            error(report.errors, ErrorCode::invalid_asset, "collisions.points", "shape has too few points");
    }
    std::set<std::string> trigger_ids;
    std::set<std::string> event_ids;
    for (const auto& event : map.events) {
        if (!core::ResourceId::is_valid(event.id.value) || !event_ids.insert(event.id.value).second)
            error(report.errors, ErrorCode::duplicate_resource, "events.id", "event ids must be valid and unique");
    }
    for (const auto& trigger : map.triggers) {
        if (!core::ResourceId::is_valid(trigger.id) || !trigger_ids.insert(trigger.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "triggers.id", "trigger ids must be valid and unique");
        if (!layer_exists(map, trigger.layer_id, MapLayerKind::triggers))
            error(report.errors, ErrorCode::missing_resource, "triggers.layer", "trigger layer is missing");
        if (trigger.collision_index >= map.collisions.size() || !core::ResourceId::is_valid(trigger.event_id.value) ||
            event_ids.find(trigger.event_id.value) == event_ids.end())
            error(report.errors, ErrorCode::invalid_asset, "triggers", "trigger shape or event is invalid");
    }
    return report;
}

std::vector<ResourceReference> map_resource_references(const MapDocument& map) {
    std::vector<ResourceReference> references;
    for (const auto& prefab : map.prefabs) references.push_back(prefab.entity);
    for (const auto& instance : map.instances) {
        if (instance.entity) references.push_back(*instance.entity);
        if (instance.prefab) references.push_back(*instance.prefab);
        for (const auto& property : instance.properties)
            if (const auto* value = std::get_if<ResourceReference>(&property.value)) references.push_back(*value);
    }
    return references;
}

std::string serialize_map(const MapDocument& map) {
    Json json = {{"schemaVersion", map.document.schema_version}, {"type", map.document.type},
                 {"id", map.document.id.value}, {"name", map.document.name},
                 {"chunkSize", map.chunk_size}, {"layers", Json::array()}, {"prefabs", Json::array()},
                 {"instances", Json::array()}, {"collisions", Json::array()}, {"triggers", Json::array()},
                 {"events", Json::array()}};
    for (const auto& layer : map.layers) json["layers"].push_back({{"id", layer.id}, {"name", layer.name}, {"kind", std::string(to_string(layer.kind))}, {"visible", layer.visible}, {"locked", layer.locked}, {"depth", layer.depth}});
    for (const auto& prefab : map.prefabs) json["prefabs"].push_back({{"id", prefab.id}, {"entity", ref(prefab.entity)}, {"overrides", properties(prefab.overrides)}});
    for (const auto& instance : map.instances) {
        Json value = {{"id", instance.id}, {"layer", instance.layer_id}, {"transform", transform(instance.transform)}, {"chunkX", instance.chunk_x}, {"chunkY", instance.chunk_y}, {"properties", properties(instance.properties)}};
        if (instance.entity) value["entity"] = ref(*instance.entity);
        if (instance.prefab) value["prefab"] = ref(*instance.prefab);
        json["instances"].push_back(std::move(value));
    }
    for (const auto& collision : map.collisions) {
        Json points = Json::array(); for (const auto point : collision.points) points.push_back(vec(point));
        json["collisions"].push_back({{"kind", collision.kind == CollisionShapeKind::circle ? "circle" : collision.kind == CollisionShapeKind::capsule ? "capsule" : collision.kind == CollisionShapeKind::polygon ? "polygon" : "chain"}, {"layer", collision.layer_id}, {"sensor", collision.sensor}, {"center", vec(collision.center)}, {"radius", collision.radius}, {"length", collision.length}, {"points", points}});
    }
    for (const auto& trigger : map.triggers) json["triggers"].push_back({{"id", trigger.id}, {"layer", trigger.layer_id}, {"collision", trigger.collision_index}, {"event", trigger.event_id.value}, {"properties", properties(trigger.properties)}});
    for (const auto& event : map.events) json["events"].push_back({{"id", event.id.value}, {"payload", properties(event.payload)}});
    return json.dump(2) + "\n";
}

MapResult parse_map(const ProjectManifest& manifest, std::string_view serialized) {
    MapResult result;
    Json json;
    try { json = Json::parse(serialized); } catch (...) { error(result.errors, ErrorCode::invalid_json, "map", "cannot parse map JSON"); return result; }
    if (!json.is_object()) { error(result.errors, ErrorCode::invalid_asset, "map", "top-level value must be an object"); return result; }
    MapDocument map;
    const auto schema = json.find("schemaVersion");
    if (schema == json.end() || !schema->is_number_unsigned()) error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected unsigned integer"); else map.document.schema_version = schema->get<std::uint32_t>();
    text(json, "type", map.document.type, result.errors); text(json, "id", map.document.id.value, result.errors); text(json, "name", map.document.name, result.errors); number(json, "chunkSize", map.chunk_size, result.errors);
    const auto layers = json.find("layers");
    if (layers == json.end() || !layers->is_array()) error(result.errors, ErrorCode::invalid_asset, "layers", "expected an array"); else for (const auto& value : *layers) { LayerDefinition layer; std::string kind; text(value, "id", layer.id, result.errors); text(value, "name", layer.name, result.errors); text(value, "kind", kind, result.errors); if (kind == "visual") layer.kind = MapLayerKind::visual; else if (kind == "tiles") layer.kind = MapLayerKind::tiles; else if (kind == "instances") layer.kind = MapLayerKind::instances; else if (kind == "collision") layer.kind = MapLayerKind::collision; else if (kind == "triggers") layer.kind = MapLayerKind::triggers; else if (kind == "gameplay") layer.kind = MapLayerKind::gameplay; else error(result.errors, ErrorCode::invalid_asset, "kind", "unsupported layer kind"); const auto visible = value.find("visible"); if (visible == value.end() || !visible->is_boolean()) error(result.errors, ErrorCode::invalid_asset, "visible", "expected boolean"); else layer.visible = visible->get<bool>(); const auto locked = value.find("locked"); if (locked == value.end() || !locked->is_boolean()) error(result.errors, ErrorCode::invalid_asset, "locked", "expected boolean"); else layer.locked = locked->get<bool>(); number(value, "depth", layer.depth, result.errors); map.layers.push_back(std::move(layer)); }
    const auto prefabs = json.find("prefabs");
    if (prefabs == json.end() || !prefabs->is_array()) error(result.errors, ErrorCode::invalid_asset, "prefabs", "expected an array"); else for (const auto& value : *prefabs) { PrefabDefinition prefab; text(value, "id", prefab.id, result.errors); std::optional<ResourceReference> entity; ref_read(value, "entity", entity, result.errors); if (entity) prefab.entity = *entity; properties_read(value, "overrides", prefab.overrides, result.errors); map.prefabs.push_back(std::move(prefab)); }
    const auto instances = json.find("instances");
    if (instances == json.end() || !instances->is_array()) error(result.errors, ErrorCode::invalid_asset, "instances", "expected an array"); else for (const auto& value : *instances) { MapInstance instance; text(value, "id", instance.id, result.errors); text(value, "layer", instance.layer_id, result.errors); transform_read(value, "transform", instance.transform, result.errors); std::int64_t chunk{}; integer(value, "chunkX", chunk, result.errors); instance.chunk_x = static_cast<std::int32_t>(chunk); integer(value, "chunkY", chunk, result.errors); instance.chunk_y = static_cast<std::int32_t>(chunk); ref_read(value, "entity", instance.entity, result.errors); ref_read(value, "prefab", instance.prefab, result.errors); properties_read(value, "properties", instance.properties, result.errors); map.instances.push_back(std::move(instance)); }
    const auto collisions = json.find("collisions");
    if (collisions == json.end() || !collisions->is_array()) error(result.errors, ErrorCode::invalid_asset, "collisions", "expected an array"); else for (const auto& value : *collisions) { CollisionShape collision; std::string kind; text(value, "kind", kind, result.errors); if (kind == "circle") collision.kind = CollisionShapeKind::circle; else if (kind == "capsule") collision.kind = CollisionShapeKind::capsule; else if (kind == "polygon") collision.kind = CollisionShapeKind::polygon; else if (kind == "chain") collision.kind = CollisionShapeKind::chain; else error(result.errors, ErrorCode::invalid_asset, "kind", "unsupported collision kind"); text(value, "layer", collision.layer_id, result.errors); const auto sensor = value.find("sensor"); if (sensor == value.end() || !sensor->is_boolean()) error(result.errors, ErrorCode::invalid_asset, "sensor", "expected boolean"); else collision.sensor = sensor->get<bool>(); vec_read(value, "center", collision.center, result.errors); number(value, "radius", collision.radius, result.errors); number(value, "length", collision.length, result.errors); const auto points = value.find("points"); if (points == value.end() || !points->is_array()) error(result.errors, ErrorCode::invalid_asset, "points", "expected an array"); else for (const auto& point : *points) { core::Vec2 parsed{}; if (point.is_object() && number(point, "x", parsed.x, result.errors) && number(point, "y", parsed.y, result.errors)) collision.points.push_back(parsed); } map.collisions.push_back(std::move(collision)); }
    const auto triggers = json.find("triggers");
    if (triggers == json.end() || !triggers->is_array()) error(result.errors, ErrorCode::invalid_asset, "triggers", "expected an array"); else for (const auto& value : *triggers) { TriggerDefinition trigger; text(value, "id", trigger.id, result.errors); text(value, "layer", trigger.layer_id, result.errors); std::int64_t index{}; integer(value, "collision", index, result.errors); trigger.collision_index = static_cast<std::size_t>(std::max<std::int64_t>(0, index)); text(value, "event", trigger.event_id.value, result.errors); properties_read(value, "properties", trigger.properties, result.errors); map.triggers.push_back(std::move(trigger)); }
    const auto events = json.find("events");
    if (events == json.end() || !events->is_array()) error(result.errors, ErrorCode::invalid_asset, "events", "expected an array"); else for (const auto& value : *events) { MapEventDefinition event; text(value, "id", event.id.value, result.errors); properties_read(value, "payload", event.payload, result.errors); map.events.push_back(std::move(event)); }
    if (!result.errors.empty()) return result;
    const auto validation = validate_map(manifest, map); if (!validation.ok()) { result.errors = validation.errors; return result; }
    result.asset = std::move(map); return result;
}

MapResult load_map(const std::filesystem::path& root, const ProjectManifest& manifest,
                   const std::filesystem::path& path) {
    const auto stored = load_document(root, path, [&](std::string_view value) { return parse_validation(manifest, value); });
    MapResult result; result.errors = stored.errors; if (stored.contents) result = parse_map(manifest, *stored.contents);
    if (result.ok() && path != map_document_path(manifest, result.asset->document.id)) { result.asset.reset(); error(result.errors, ErrorCode::invalid_path, "document", "document filename does not match its id"); }
    return result;
}

MapResult publish_map(const std::filesystem::path& root, const ProjectManifest& manifest,
                      const MapDocument& map) {
    MapResult result; const auto validation = validate_map(manifest, map); if (!validation.ok()) { result.errors = validation.errors; return result; }
    const auto path = map_document_path(manifest, map.document.id);
    const auto saved = save_document_atomic(root, path, serialize_map(map), [&](std::string_view value) { return parse_validation(manifest, value); });
    if (!saved.ok()) { result.errors = saved.errors; return result; }
    return load_map(root, manifest, path);
}

} // namespace fabric::project
