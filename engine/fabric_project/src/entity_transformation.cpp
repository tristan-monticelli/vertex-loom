#include "fabric/project/entity_transformation.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <utility>

namespace fabric::project {
namespace {
using Json = nlohmann::json;

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

void unknown(const Json& value, std::initializer_list<std::string_view> fields,
             std::string_view prefix, std::vector<Error>& errors) {
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
        if (std::none_of(fields.begin(), fields.end(), [&](auto field) {
                return field == iterator.key();
            }))
            error(errors, ErrorCode::invalid_asset,
                  prefix.empty() ? iterator.key()
                                 : std::string(prefix) + "." + iterator.key(),
                  "unknown field");
}

bool text(const Json& value, const char* key, std::string& output,
          std::vector<Error>& errors, std::string_view prefix = {}) {
    const auto found = value.find(key);
    const auto field = prefix.empty() ? std::string(key)
                                      : std::string(prefix) + "." + key;
    if (found == value.end() || !found->is_string()) {
        error(errors, ErrorCode::invalid_asset, field, "expected a string");
        return false;
    }
    output = found->get<std::string>();
    return true;
}

std::optional<TransferMode> mode(std::string_view value) {
    if (value == "preserve") return TransferMode::preserve;
    if (value == "reset") return TransferMode::reset;
    if (value == "mapping") return TransferMode::mapping;
    if (value == "error") return TransferMode::error;
    return std::nullopt;
}

std::optional<TransferDomain> domain(std::string_view value) {
    if (value == "property") return TransferDomain::property;
    if (value == "behaviorParameter") return TransferDomain::behavior_parameter;
    if (value == "animation") return TransferDomain::animation;
    return std::nullopt;
}

bool read_mode(const Json& json, const char* key, TransferMode& output,
               std::vector<Error>& errors) {
    std::string value;
    if (!text(json, key, value, errors, "policy")) return false;
    const auto parsed = mode(value);
    if (!parsed) {
        error(errors, ErrorCode::invalid_asset, std::string{"policy."} + key,
              "unsupported transfer mode");
        return false;
    }
    output = *parsed;
    return true;
}

Json reference_json(const ResourceReference& value) {
    return {{"id", value.id.value}, {"expectedType", value.expected_type}};
}

bool read_reference(const Json& json, const char* key, ResourceReference& output,
                    std::vector<Error>& errors) {
    const auto found = json.find(key);
    if (found == json.end() || !found->is_object()) {
        error(errors, ErrorCode::invalid_asset, key, "expected a resource reference");
        return false;
    }
    unknown(*found, {"id", "expectedType"}, key, errors);
    return text(*found, "id", output.id.value, errors, key) &&
        text(*found, "expectedType", output.expected_type, errors, key);
}

ValidationReport parse_validation(const ProjectManifest& manifest,
                                  std::string_view contents) {
    auto parsed = parse_entity_transformation(manifest, contents);
    return {.errors = std::move(parsed.errors)};
}
} // namespace

std::string_view to_string(const TransferMode value) noexcept {
    switch (value) {
    case TransferMode::preserve: return "preserve";
    case TransferMode::reset: return "reset";
    case TransferMode::mapping: return "mapping";
    case TransferMode::error: return "error";
    }
    return "error";
}

std::string_view to_string(const TransferDomain value) noexcept {
    switch (value) {
    case TransferDomain::property: return "property";
    case TransferDomain::behavior_parameter: return "behaviorParameter";
    case TransferDomain::animation: return "animation";
    }
    return "property";
}

std::filesystem::path entity_transformation_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "transformations" /
        (id.value + ".transformation.json");
}

ValidationReport validate_entity_transformation(
    const ProjectManifest&, const EntityTransformation& transformation) {
    ValidationReport report;
    if (transformation.document.schema_version !=
        current_entity_transformation_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version,
              "schemaVersion", "only transformation schema version 1 is supported");
    if (transformation.document.type != "transformation")
        error(report.errors, ErrorCode::invalid_asset, "type",
              "must be transformation");
    if (!core::ResourceId::is_valid(transformation.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (transformation.document.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name", "must not be empty");
    const auto validate_entity = [&](const ResourceReference& reference,
                                     const char* field) {
        if (!core::ResourceId::is_valid(reference.id.value) ||
            reference.expected_type != "entity")
            error(report.errors, ErrorCode::resource_type_mismatch, field,
                  "must reference an entity");
    };
    validate_entity(transformation.source_entity, "sourceEntity");
    validate_entity(transformation.destination_entity, "destinationEntity");
    if (transformation.source_entity.id == transformation.destination_entity.id)
        error(report.errors, ErrorCode::resource_cycle, "destinationEntity",
              "source and destination must differ");

    const auto basic = [](TransferMode value) {
        return value == TransferMode::preserve || value == TransferMode::reset;
    };
    if (!basic(transformation.policy.world_transform) ||
        !basic(transformation.policy.instance_id) ||
        !basic(transformation.policy.layer_and_z) ||
        !basic(transformation.policy.physics) ||
        !basic(transformation.policy.timers_and_cooldowns) ||
        !basic(transformation.policy.camera_follow))
        error(report.errors, ErrorCode::invalid_asset, "policy",
              "structural domains only allow preserve or reset");
    if (transformation.policy.incompatible_values != TransferMode::reset &&
        transformation.policy.incompatible_values != TransferMode::error)
        error(report.errors, ErrorCode::invalid_asset, "policy.incompatibleValues",
              "must be reset or error");
    std::set<std::pair<TransferDomain, std::string>> sources;
    std::set<std::pair<TransferDomain, std::string>> targets;
    for (std::size_t index = 0; index < transformation.policy.mappings.size(); ++index) {
        const auto& mapping = transformation.policy.mappings[index];
        const auto prefix = "policy.mappings[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(mapping.source) ||
            !core::ResourceId::is_valid(mapping.target))
            error(report.errors, ErrorCode::invalid_resource_id, prefix,
                  "source and target must be valid identifiers");
        if (!sources.emplace(mapping.domain, mapping.source).second ||
            !targets.emplace(mapping.domain, mapping.target).second)
            error(report.errors, ErrorCode::duplicate_resource, prefix,
                  "mapping source and target must be unique per domain");
    }
    const auto needs_mapping = [&](TransferDomain domain_value, TransferMode mode_value) {
        return mode_value != TransferMode::mapping ||
            std::ranges::any_of(transformation.policy.mappings,
                [&](const auto& mapping) { return mapping.domain == domain_value; });
    };
    if (!needs_mapping(TransferDomain::property, transformation.policy.properties) ||
        !needs_mapping(TransferDomain::behavior_parameter,
                       transformation.policy.behavior_parameters) ||
        !needs_mapping(TransferDomain::animation, transformation.policy.animation))
        error(report.errors, ErrorCode::missing_resource, "policy.mappings",
              "mapping mode requires at least one mapping in its domain");
    return report;
}

std::vector<ResourceReference> entity_transformation_resource_references(
    const EntityTransformation& transformation) {
    // The source is a precondition and already belongs to the incoming closure.
    return {transformation.destination_entity};
}

std::string serialize_entity_transformation(
    const EntityTransformation& transformation) {
    const auto& policy = transformation.policy;
    Json json{{"schemaVersion", transformation.document.schema_version},
              {"type", transformation.document.type},
              {"id", transformation.document.id.value},
              {"name", transformation.document.name},
              {"sourceEntity", reference_json(transformation.source_entity)},
              {"destinationEntity", reference_json(transformation.destination_entity)},
              {"policy", {{"worldTransform", to_string(policy.world_transform)},
                          {"instanceId", to_string(policy.instance_id)},
                          {"layerAndZ", to_string(policy.layer_and_z)},
                          {"physics", to_string(policy.physics)},
                          {"properties", to_string(policy.properties)},
                          {"behaviorParameters", to_string(policy.behavior_parameters)},
                          {"animation", to_string(policy.animation)},
                          {"timersAndCooldowns", to_string(policy.timers_and_cooldowns)},
                          {"cameraFollow", to_string(policy.camera_follow)},
                          {"incompatibleValues", to_string(policy.incompatible_values)},
                          {"mappings", Json::array()}}}};
    for (const auto& mapping : policy.mappings)
        json["policy"]["mappings"].push_back({{"domain", to_string(mapping.domain)},
                                                {"source", mapping.source},
                                                {"target", mapping.target}});
    return json.dump(2) + '\n';
}

EntityTransformationResult parse_entity_transformation(
    const ProjectManifest& manifest, std::string_view contents) {
    EntityTransformationResult result;
    Json json;
    try { json = Json::parse(contents); }
    catch (...) { error(result.errors, ErrorCode::invalid_json, "transformation", "cannot parse transformation JSON"); return result; }
    if (!json.is_object()) { error(result.errors, ErrorCode::invalid_asset, "transformation", "top-level value must be an object"); return result; }
    unknown(json, {"schemaVersion", "type", "id", "name", "sourceEntity",
                   "destinationEntity", "policy"}, "", result.errors);
    EntityTransformation transformation;
    const auto schema = json.find("schemaVersion");
    if (schema == json.end() || !schema->is_number_unsigned())
        error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected unsigned integer");
    else transformation.document.schema_version = schema->get<std::uint32_t>();
    text(json, "type", transformation.document.type, result.errors);
    text(json, "id", transformation.document.id.value, result.errors);
    text(json, "name", transformation.document.name, result.errors);
    read_reference(json, "sourceEntity", transformation.source_entity, result.errors);
    read_reference(json, "destinationEntity", transformation.destination_entity, result.errors);
    const auto policy = json.find("policy");
    if (policy == json.end() || !policy->is_object())
        error(result.errors, ErrorCode::invalid_asset, "policy", "expected an object");
    else {
        unknown(*policy, {"worldTransform", "instanceId", "layerAndZ", "physics",
                          "properties", "behaviorParameters", "animation",
                          "timersAndCooldowns", "cameraFollow", "incompatibleValues",
                          "mappings"}, "policy", result.errors);
        auto& output = transformation.policy;
        read_mode(*policy, "worldTransform", output.world_transform, result.errors);
        read_mode(*policy, "instanceId", output.instance_id, result.errors);
        read_mode(*policy, "layerAndZ", output.layer_and_z, result.errors);
        read_mode(*policy, "physics", output.physics, result.errors);
        read_mode(*policy, "properties", output.properties, result.errors);
        read_mode(*policy, "behaviorParameters", output.behavior_parameters, result.errors);
        read_mode(*policy, "animation", output.animation, result.errors);
        read_mode(*policy, "timersAndCooldowns", output.timers_and_cooldowns, result.errors);
        read_mode(*policy, "cameraFollow", output.camera_follow, result.errors);
        read_mode(*policy, "incompatibleValues", output.incompatible_values, result.errors);
        const auto mappings = policy->find("mappings");
        if (mappings == policy->end() || !mappings->is_array())
            error(result.errors, ErrorCode::invalid_asset, "policy.mappings", "expected an array");
        else for (std::size_t index = 0; index < mappings->size(); ++index) {
            const auto& item = (*mappings)[index];
            const auto prefix = "policy.mappings[" + std::to_string(index) + "]";
            if (!item.is_object()) { error(result.errors, ErrorCode::invalid_asset, prefix, "expected an object"); continue; }
            unknown(item, {"domain", "source", "target"}, prefix, result.errors);
            TransferMapping mapping; std::string domain_value;
            text(item, "domain", domain_value, result.errors, prefix);
            text(item, "source", mapping.source, result.errors, prefix);
            text(item, "target", mapping.target, result.errors, prefix);
            const auto parsed = domain(domain_value);
            if (!parsed) error(result.errors, ErrorCode::invalid_asset, prefix + ".domain", "unsupported mapping domain");
            else mapping.domain = *parsed;
            output.mappings.push_back(std::move(mapping));
        }
    }
    if (!result.errors.empty()) return result;
    auto validation = validate_entity_transformation(manifest, transformation);
    if (!validation.ok()) { result.errors = std::move(validation.errors); return result; }
    result.asset = std::move(transformation); return result;
}

EntityTransformationResult load_entity_transformation(
    const std::filesystem::path& root, const ProjectManifest& manifest,
    const std::filesystem::path& path) {
    auto storage = load_document(root, path, [&](std::string_view value) {
        return parse_validation(manifest, value);
    });
    EntityTransformationResult result; result.errors = std::move(storage.errors);
    if (storage.contents) result = parse_entity_transformation(manifest, *storage.contents);
    if (result.ok() && path != entity_transformation_document_path(
            manifest, result.asset->document.id)) {
        result.asset.reset();
        error(result.errors, ErrorCode::invalid_path, "document",
              "document filename does not match its id");
    }
    return result;
}

EntityTransformationResult publish_entity_transformation(
    const std::filesystem::path& root, const ProjectManifest& manifest,
    const EntityTransformation& transformation) {
    EntityTransformationResult result;
    auto validation = validate_entity_transformation(manifest, transformation);
    if (!validation.ok()) { result.errors = std::move(validation.errors); return result; }
    const auto path = entity_transformation_document_path(
        manifest, transformation.document.id);
    const auto saved = save_document_atomic(
        root, path, serialize_entity_transformation(transformation),
        [&](std::string_view value) { return parse_validation(manifest, value); });
    if (!saved.ok()) { result.errors = saved.errors; return result; }
    return load_entity_transformation(root, manifest, path);
}
} // namespace fabric::project
