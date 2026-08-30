#include "fabric/project/scene.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <set>
#include <ranges>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;

void error(std::vector<Error>& errors, ErrorCode code, std::string field, std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
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

Json reference(const ResourceReference& value) {
    return {{"id", value.id.value}, {"expectedType", value.expected_type}};
}

bool read_reference(const Json& object, const char* key, ResourceReference& out,
                    std::vector<Error>& errors) {
    const auto item = object.find(key);
    if (item == object.end() || !item->is_object()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a resource reference");
        return false;
    }
    return text(*item, "id", out.id.value, errors) &&
        text(*item, "expectedType", out.expected_type, errors);
}

ValidationReport parse_validation(const ProjectManifest& manifest, std::string_view json) {
    auto result = parse_scene(manifest, json);
    return {.errors = std::move(result.errors)};
}
}

std::filesystem::path scene_document_path(const ProjectManifest& manifest,
                                           const core::ResourceId& id) {
    return manifest.directories.scenes / (id.value + ".scene.json");
}

ValidationReport validate_scene(const ProjectManifest&, const SceneDocument& scene) {
    ValidationReport report;
    if (scene.document.schema_version != current_scene_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version, "schemaVersion",
              "only scene schema version 1 is supported");
    if (scene.document.type != "scene")
        error(report.errors, ErrorCode::invalid_asset, "type", "must be scene");
    if (!core::ResourceId::is_valid(scene.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (scene.document.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name", "must not be empty");

    std::set<std::string> map_ids;
    std::set<std::string> mount_ids;
    for (const auto& map : scene.maps) {
        if (!core::ResourceId::is_valid(map.map.id.value) ||
            map.map.expected_type != "map" || !map_ids.insert(map.map.id.value).second)
            error(report.errors, ErrorCode::duplicate_resource, "maps", "map references must be valid and unique");
        if (!core::ResourceId::is_valid(map.layer_id) ||
            !mount_ids.insert(map.layer_id).second)
            error(report.errors, ErrorCode::duplicate_resource, "maps.layer",
                  "mount ids must be valid and unique");
    }
    if (scene.entry_map) {
        if (scene.entry_map->expected_type != "map" ||
            !core::ResourceId::is_valid(scene.entry_map->id.value) ||
            !map_ids.contains(scene.entry_map->id.value))
            error(report.errors, ErrorCode::missing_resource, "entryMap",
                  "entry map must reference a declared scene map");
    }
    std::set<std::string> transition_ids;
    for (const auto& transition : scene.transitions) {
        if (!core::ResourceId::is_valid(transition.id) ||
            !transition_ids.insert(transition.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "transitions.id",
                  "transition ids must be valid and unique");
        if (!core::ResourceId::is_valid(transition.target_scene.id.value) ||
            transition.target_scene.expected_type != "scene")
            error(report.errors, ErrorCode::resource_type_mismatch,
                  "transitions.targetScene", "transition target must be a scene");
        if (transition.entry_point.empty())
            error(report.errors, ErrorCode::invalid_asset, "transitions.entryPoint",
                  "must not be empty");
        if (transition.event_id && !core::ResourceId::is_valid(transition.event_id->value))
            error(report.errors, ErrorCode::invalid_resource_id, "transitions.event",
                  "must be a valid event id");
    }
    return report;
}

std::vector<ResourceReference> scene_resource_references(const SceneDocument& scene) {
    std::vector<ResourceReference> references;
    for (const auto& map : scene.maps) references.push_back(map.map);
    if (scene.entry_map) references.push_back(*scene.entry_map);
    for (const auto& transition : scene.transitions) references.push_back(transition.target_scene);
    return references;
}

std::string serialize_scene(const SceneDocument& scene) {
    Json json = {{"schemaVersion", scene.document.schema_version},
                 {"type", scene.document.type},
                 {"id", scene.document.id.value},
                 {"name", scene.document.name},
                 {"maps", Json::array()},
                 {"transitions", Json::array()}};
    for (const auto& map : scene.maps)
        json["maps"].push_back({{"map", reference(map.map)}, {"layer", map.layer_id}});
    if (scene.entry_map) json["entryMap"] = reference(*scene.entry_map);
    for (const auto& transition : scene.transitions)
    {
        Json item = {{"id", transition.id},
                     {"targetScene", reference(transition.target_scene)},
                     {"entryPoint", transition.entry_point}};
        if (transition.event_id) item["event"] = transition.event_id->value;
        json["transitions"].push_back(std::move(item));
    }
    return json.dump(2) + "\n";
}

SceneResult parse_scene(const ProjectManifest& manifest, std::string_view json_text) {
    SceneResult result;
    Json json;
    try { json = Json::parse(json_text); }
    catch (...) { error(result.errors, ErrorCode::invalid_json, "scene", "cannot parse scene JSON"); return result; }
    if (!json.is_object()) {
        error(result.errors, ErrorCode::invalid_asset, "scene", "top-level value must be an object");
        return result;
    }
    SceneDocument scene;
    const auto schema = json.find("schemaVersion");
    if (schema == json.end() || !schema->is_number_unsigned())
        error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected an unsigned integer");
    else scene.document.schema_version = schema->get<std::uint32_t>();
    text(json, "type", scene.document.type, result.errors);
    text(json, "id", scene.document.id.value, result.errors);
    text(json, "name", scene.document.name, result.errors);
    const auto maps = json.find("maps");
    if (maps == json.end() || !maps->is_array()) error(result.errors, ErrorCode::invalid_asset, "maps", "expected an array");
    else for (const auto& item : *maps) {
        SceneMapReference map;
        read_reference(item, "map", map.map, result.errors);
        text(item, "layer", map.layer_id, result.errors);
        scene.maps.push_back(std::move(map));
    }
    const auto entry = json.find("entryMap");
    if (entry != json.end()) {
        ResourceReference reference_value;
        if (entry->is_object() && text(*entry, "id", reference_value.id.value, result.errors) &&
            text(*entry, "expectedType", reference_value.expected_type, result.errors))
            scene.entry_map = std::move(reference_value);
        else if (!entry->is_object()) error(result.errors, ErrorCode::invalid_asset, "entryMap", "expected a resource reference");
    }
    const auto transitions = json.find("transitions");
    if (transitions == json.end() || !transitions->is_array()) error(result.errors, ErrorCode::invalid_asset, "transitions", "expected an array");
    else for (const auto& item : *transitions) {
        SceneTransition transition;
        text(item, "id", transition.id, result.errors);
        read_reference(item, "targetScene", transition.target_scene, result.errors);
        text(item, "entryPoint", transition.entry_point, result.errors);
        const auto event = item.find("event");
        if (event != item.end()) {
            if (!event->is_string())
                error(result.errors, ErrorCode::invalid_asset, "transitions.event",
                      "expected a string");
            else transition.event_id = core::ResourceId{event->get<std::string>()};
        }
        scene.transitions.push_back(std::move(transition));
    }
    if (!result.errors.empty()) return result;
    const auto validation = validate_scene(manifest, scene);
    if (!validation.ok()) { result.errors = validation.errors; return result; }
    result.asset = std::move(scene);
    return result;
}

SceneResult load_scene(const std::filesystem::path& root, const ProjectManifest& manifest,
                       const std::filesystem::path& path) {
    const auto stored = load_document(root, path, [&](std::string_view text_value) {
        return parse_validation(manifest, text_value);
    });
    SceneResult result;
    result.errors = stored.errors;
    if (stored.contents) result = parse_scene(manifest, *stored.contents);
    if (result.ok() && path != scene_document_path(manifest, result.asset->document.id)) {
        result.asset.reset();
        error(result.errors, ErrorCode::invalid_path, "document", "document filename does not match its id");
    }
    return result;
}

SceneResult publish_scene(const std::filesystem::path& root, const ProjectManifest& manifest,
                          const SceneDocument& scene) {
    SceneResult result;
    const auto validation = validate_scene(manifest, scene);
    if (!validation.ok()) { result.errors = validation.errors; return result; }
    const auto path = scene_document_path(manifest, scene.document.id);
    const auto saved = save_document_atomic(root, path, serialize_scene(scene),
        [&](std::string_view text_value) { return parse_validation(manifest, text_value); });
    if (!saved.ok()) { result.errors = saved.errors; return result; }
    return load_scene(root, manifest, path);
}

SceneCompositionResult compose_scene_maps(
    const std::filesystem::path& root, const ProjectManifest& manifest,
    const SceneDocument& scene) {
    SceneCompositionResult result;
    const auto scene_validation = validate_scene(manifest, scene);
    if (!scene_validation.ok()) {
        result.errors = scene_validation.errors;
        return result;
    }
    MapDocument composed{
        .document = {.schema_version = current_map_schema_version,
                     .type = "map",
                     .id = scene.document.id,
                     .name = scene.document.name}};
    std::set<std::string> entry_point_ids;
    for (const auto& reference : scene.maps) {
        const auto loaded = load_map(
            root, manifest, map_document_path(manifest, reference.map.id));
        if (!loaded.ok()) {
            result.errors.insert(result.errors.end(), loaded.errors.begin(),
                                 loaded.errors.end());
            continue;
        }
        const auto& source = *loaded.asset;
        const auto prefixed = [&](const std::string& id) {
            return reference.layer_id + "-" + id;
        };
        const auto collision_offset = composed.collisions.size();
        for (auto layer : source.layers) {
            layer.id = prefixed(layer.id);
            composed.layers.push_back(std::move(layer));
        }
        for (auto prefab : source.prefabs) {
            prefab.id = prefixed(prefab.id);
            composed.prefabs.push_back(std::move(prefab));
        }
        for (auto instance : source.instances) {
            instance.id = prefixed(instance.id);
            instance.layer_id = prefixed(instance.layer_id);
            if (instance.prefab)
                instance.prefab->id.value = prefixed(instance.prefab->id.value);
            for (const auto& property : instance.properties) {
                if (property.id != "sceneEntryPoint") continue;
                const auto* entry_id = std::get_if<std::string>(&property.value);
                if (entry_id == nullptr ||
                    !core::ResourceId::is_valid(*entry_id) ||
                    !entry_point_ids.insert(*entry_id).second) {
                    error(result.errors, ErrorCode::duplicate_resource,
                          "instances.sceneEntryPoint",
                          "scene entry points must be unique valid text ids");
                    continue;
                }
                result.entry_points.push_back({
                    .id = *entry_id,
                    .instance_id = instance.id,
                    .position = instance.transform.position});
            }
            composed.instances.push_back(std::move(instance));
        }
        for (auto collision : source.collisions) {
            collision.layer_id = prefixed(collision.layer_id);
            composed.collisions.push_back(std::move(collision));
        }
        for (auto trigger : source.triggers) {
            trigger.id = prefixed(trigger.id);
            trigger.layer_id = prefixed(trigger.layer_id);
            trigger.collision_index += collision_offset;
            composed.triggers.push_back(std::move(trigger));
        }
        for (const auto& event : source.events) {
            const auto existing = std::ranges::find(
                composed.events, event.id, &MapEventDefinition::id);
            if (existing == composed.events.end()) {
                composed.events.push_back(event);
            } else if (existing->payload != event.payload) {
                error(result.errors, ErrorCode::duplicate_resource,
                      "events", "scene maps declare incompatible event payloads");
            }
        }
    }
    if (!result.errors.empty()) return result;
    const auto validation = validate_map(manifest, composed);
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }
    result.map = std::move(composed);
    return result;
}

} // namespace fabric::project
