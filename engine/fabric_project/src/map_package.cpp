#include "fabric/project/map_package.hpp"

#include "fabric/project/animation.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/input.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/mechanic_graph.hpp"
#include "fabric/project/replay.hpp"
#include "fabric/project/scene.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/textured_path.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/project/visual_composition.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
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

struct ResolvedPackageResource {
    MapPackageResource package_resource;
    std::vector<ResourceReference> references;
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

void append_errors(std::vector<Error>& destination,
                   const std::vector<Error>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

std::optional<ResolvedPackageResource> resolve_package_resource(
    const std::filesystem::path& root, const ProjectManifest& manifest,
    const ResourceReference& reference, std::vector<Error>& errors) {
    const auto result = [&](const DocumentHeader& document,
                            std::filesystem::path path,
                            std::vector<ResourceReference> references,
                            std::vector<std::filesystem::path> payloads = {}) {
        return ResolvedPackageResource{
            .package_resource = {
                .resource = {{.value = document.id.value}, document.type},
                .document_path = std::move(path),
                .payload_paths = std::move(payloads)},
            .references = std::move(references)};
    };
    const auto missing = [&] {
        error(errors, ErrorCode::resource_type_mismatch, reference.id.value,
              "unsupported package resource type: " + reference.expected_type);
        return std::optional<ResolvedPackageResource>{};
    };

    if (reference.expected_type == "texture") {
        const auto path = texture_document_path(manifest, reference.id);
        const auto loaded = load_texture_asset(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path, {}, {loaded.asset->source});
    }
    if (reference.expected_type == "vector") {
        const auto path = vector_document_path(manifest, reference.id);
        const auto loaded = load_vector_asset(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        std::vector<std::filesystem::path> payloads;
        if (loaded.asset->source_kind == VectorSourceKind::linked_svg)
            payloads.push_back(loaded.asset->source);
        return result(loaded.asset->document, path,
                      vector_resource_references(*loaded.asset),
                      std::move(payloads));
    }
    if (reference.expected_type == "material") {
        const auto path = material_document_path(manifest, reference.id);
        const auto loaded = load_material(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      material_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "visualComposition") {
        const auto path = visual_composition_document_path(manifest, reference.id);
        const auto loaded = load_visual_composition(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      visual_composition_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "visualComponent") {
        const auto path = visual_component_document_path(manifest, reference.id);
        const auto loaded = load_visual_component(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      visual_component_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "texturedPath") {
        const auto path = textured_path_document_path(manifest, reference.id);
        const auto loaded = load_textured_path(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      textured_path_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "mechanic") {
        const auto path = mechanic_graph_document_path(manifest, reference.id);
        const auto loaded = load_mechanic_graph(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      mechanic_graph_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "animation") {
        const auto path = animation_document_path(manifest, reference.id);
        const auto loaded = load_animation(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      animation_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "entity") {
        const auto path = entity_document_path(manifest, reference.id);
        const auto loaded = load_entity(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.entity->document, path,
                      entity_resource_references(*loaded.entity));
    }
    if (reference.expected_type == "map") {
        const auto path = map_document_path(manifest, reference.id);
        const auto loaded = load_map(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      map_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "scene") {
        const auto path = scene_document_path(manifest, reference.id);
        const auto loaded = load_scene(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      scene_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "replay") {
        const auto path = replay_document_path(manifest, reference.id);
        const auto loaded = load_replay(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.asset->document, path,
                      replay_resource_references(*loaded.asset));
    }
    if (reference.expected_type == "input") {
        const auto path = input_document_path(manifest, reference.id);
        const auto loaded = load_input(root, manifest, path);
        if (!loaded.ok()) { append_errors(errors, loaded.errors); return {}; }
        return result(loaded.input->document, path, {});
    }
    return missing();
}

void enqueue_property_references(const std::vector<MapProperty>& properties,
                                 std::vector<ResourceReference>& references) {
    for (const auto& property : properties)
        if (const auto* reference =
                std::get_if<ResourceReference>(&property.value))
            references.push_back(*reference);
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

MapPackageManifestResult plan_map_package(
    const std::filesystem::path& project_root, const core::ResourceId& map_id,
    const std::string_view minimum_runtime_version) {
    MapPackageManifestResult output;
    const auto loaded_manifest = load_manifest(project_root);
    if (!loaded_manifest.ok()) {
        output.errors = loaded_manifest.errors;
        return output;
    }
    const auto root_path = map_document_path(*loaded_manifest.manifest, map_id);
    const auto loaded_map = load_map(project_root, *loaded_manifest.manifest,
                                     root_path);
    if (!loaded_map.ok()) {
        output.errors = loaded_map.errors;
        return output;
    }

    MapPackageManifest package{
        .schema_version = current_map_package_schema_version,
        .type = "map-package",
        .id = map_id,
        .name = loaded_map.asset->document.name,
        .minimum_runtime_version = std::string(minimum_runtime_version),
        .root_map = {map_id, "map"}};
    using Key = std::pair<std::string, std::string>;
    std::set<Key> pending{{"map", map_id.value}};
    std::set<Key> processed;
    std::map<Key, std::vector<Key>> adjacency;
    std::map<std::string, std::string> types_by_identifier;
    while (!pending.empty()) {
        const auto key = *pending.begin();
        pending.erase(pending.begin());
        if (!processed.insert(key).second) continue;
        const auto [identity, inserted] =
            types_by_identifier.emplace(key.second, key.first);
        if (!inserted && identity->second != key.first) {
            error(output.errors, ErrorCode::duplicate_resource, key.second,
                  "resource identifier is used by both " + identity->second +
                      " and " + key.first);
        }
        const ResourceReference reference{{.value = key.second}, key.first};
        if (reference.expected_type == "prefab") {
            const auto prefab = std::find_if(
                loaded_map.asset->prefabs.begin(), loaded_map.asset->prefabs.end(),
                [&](const PrefabDefinition& value) {
                    return value.id == reference.id.value;
                });
            if (prefab == loaded_map.asset->prefabs.end()) {
                error(output.errors, ErrorCode::missing_resource,
                      reference.id.value, "inline prefab is missing from root map");
                continue;
            }
            std::vector<ResourceReference> references{prefab->entity};
            if (prefab->mechanic) references.push_back(*prefab->mechanic);
            auto mechanic_references =
                mechanic_parameter_override_resource_references(
                    prefab->mechanic_overrides);
            references.insert(references.end(), mechanic_references.begin(),
                              mechanic_references.end());
            enqueue_property_references(prefab->overrides, references);
            for (const auto& dependency : references) {
                adjacency[key].emplace_back(dependency.expected_type,
                                            dependency.id.value);
                pending.emplace(dependency.expected_type, dependency.id.value);
            }
            continue;
        }
        auto resolved = resolve_package_resource(
            project_root, *loaded_manifest.manifest, reference, output.errors);
        if (!resolved) continue;
        if (resolved->package_resource.resource != reference) {
            error(output.errors, ErrorCode::resource_type_mismatch,
                  reference.id.value, "loaded resource identity does not match reference");
            continue;
        }
        if (reference.expected_type == "map") {
            for (const auto& prefab : loaded_map.asset->prefabs)
                enqueue_property_references(prefab.overrides,
                                            resolved->references);
        }
        for (const auto& dependency : resolved->references) {
            adjacency[key].emplace_back(dependency.expected_type,
                                        dependency.id.value);
            pending.emplace(dependency.expected_type, dependency.id.value);
        }
        package.resources.push_back(std::move(resolved->package_resource));
    }
    if (!output.errors.empty()) return output;
    for (auto& [source, targets] : adjacency) {
        (void)source;
        std::ranges::sort(targets);
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }
    std::map<Key, unsigned char> state;
    for (const auto& start : processed) {
        if (state[start] != 0) continue;
        state[start] = 1;
        std::vector<std::pair<Key, std::size_t>> stack{{start, 0}};
        while (!stack.empty()) {
            auto& [current, edge_index] = stack.back();
            const auto edges = adjacency.find(current);
            if (edges == adjacency.end() || edge_index == edges->second.size()) {
                state[current] = 2;
                stack.pop_back();
                continue;
            }
            const auto target = edges->second[edge_index++];
            if (state[target] == 0) {
                state[target] = 1;
                stack.emplace_back(target, 0);
            } else if (state[target] == 1) {
                error(output.errors, ErrorCode::resource_cycle, target.second,
                      "resource dependency cycle detected in map package");
            }
        }
    }
    if (!output.errors.empty()) return output;
    std::ranges::sort(package.resources, [](const auto& left, const auto& right) {
        return std::pair{left.resource.expected_type, left.resource.id.value} <
            std::pair{right.resource.expected_type, right.resource.id.value};
    });
    for (auto& resource : package.resources)
        std::ranges::sort(resource.payload_paths, {},
                          [](const auto& path) { return path.generic_string(); });
    const auto validation = validate_map_package_manifest(package);
    if (!validation.ok()) {
        output.errors = validation.errors;
        return output;
    }
    output.manifest = std::move(package);
    return output;
}

MapPackagePublishResult publish_map_package(
    const std::filesystem::path& project_root, const core::ResourceId& map_id,
    const std::filesystem::path& destination,
    const std::string_view minimum_runtime_version) {
    MapPackagePublishResult output{.destination = destination};
    const auto planned = plan_map_package(project_root, map_id,
                                          minimum_runtime_version);
    if (!planned.ok()) {
        output.errors = planned.errors;
        return output;
    }
    std::error_code filesystem_error;
    if (std::filesystem::exists(destination, filesystem_error)) {
        error(output.errors, ErrorCode::asset_already_exists, "destination",
              "package destination already exists");
        return output;
    }
    if (filesystem_error || destination.empty()) {
        error(output.errors, ErrorCode::invalid_path, "destination",
              "package destination is invalid");
        return output;
    }
    std::filesystem::create_directories(destination, filesystem_error);
    if (filesystem_error) {
        error(output.errors, ErrorCode::io_error, "destination",
              "cannot create package destination");
        return output;
    }
    const auto rollback = [&] {
        std::error_code ignored;
        std::filesystem::remove_all(destination, ignored);
    };
    const auto copy_file = [&](const std::filesystem::path& relative) {
        const auto source = project_root / relative;
        const auto target = destination / relative;
        std::filesystem::create_directories(target.parent_path(),
                                             filesystem_error);
        if (filesystem_error ||
            !std::filesystem::copy_file(source, target,
                                        std::filesystem::copy_options::none,
                                        filesystem_error)) {
            error(output.errors, ErrorCode::io_error, relative.generic_string(),
                  "cannot copy package file");
            return false;
        }
        return true;
    };
    bool copied = true;
    for (const auto& resource : planned.manifest->resources) {
        copied = copy_file(resource.document_path) && copied;
        for (const auto& payload : resource.payload_paths)
            copied = copy_file(payload) && copied;
    }
    if (copied) {
        std::ofstream manifest_file(
            destination / map_package_manifest_filename,
            std::ios::binary | std::ios::trunc);
        manifest_file << serialize_map_package_manifest(*planned.manifest);
        if (!manifest_file) {
            error(output.errors, ErrorCode::io_error, "map-package.json",
                  "cannot write package manifest");
        }
    }
    if (!output.errors.empty()) {
        rollback();
        return output;
    }
    output.manifest = planned.manifest;
    return output;
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
