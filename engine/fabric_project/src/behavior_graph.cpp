#include "fabric/project/behavior_graph.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
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

bool text(const Json& object, const char* key, std::string& output,
          std::vector<Error>& errors, const std::string& prefix = {}) {
    const auto found = object.find(key);
    const auto field = prefix.empty() ? std::string(key) : prefix + "." + key;
    if (found == object.end() || !found->is_string()) {
        error(errors, ErrorCode::invalid_asset, field, "expected a string");
        return false;
    }
    output = found->get<std::string>();
    return true;
}

void unknown(const Json& object, std::initializer_list<std::string_view> allowed,
             const std::string& prefix, std::vector<Error>& errors) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](auto key) {
                return key == it.key();
            }))
            error(errors, ErrorCode::invalid_asset,
                  prefix.empty() ? it.key() : prefix + "." + it.key(),
                  "unknown field");
}

std::optional<BehaviorValueType> value_type(std::string_view value) {
    if (value == "signal") return BehaviorValueType::signal;
    if (value == "boolean") return BehaviorValueType::boolean;
    if (value == "integer") return BehaviorValueType::integer;
    if (value == "float") return BehaviorValueType::scalar;
    if (value == "text") return BehaviorValueType::text;
    if (value == "vec2") return BehaviorValueType::vec2;
    if (value == "resource") return BehaviorValueType::resource;
    return std::nullopt;
}

Json reference_json(const ResourceReference& value) {
    return {{"id", value.id.value}, {"expectedType", value.expected_type}};
}

Json value_json(const BehaviorValue& value) {
    return std::visit([](const auto& item) -> Json {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) return {{"type", "boolean"}, {"value", item}};
        else if constexpr (std::is_same_v<T, std::int64_t>) return {{"type", "integer"}, {"value", item}};
        else if constexpr (std::is_same_v<T, float>) return {{"type", "float"}, {"value", item}};
        else if constexpr (std::is_same_v<T, std::string>) return {{"type", "text"}, {"value", item}};
        else if constexpr (std::is_same_v<T, core::Vec2>)
            return {{"type", "vec2"}, {"value", {{"x", item.x}, {"y", item.y}}}};
        else return {{"type", "resource"}, {"value", reference_json(item)}};
    }, value);
}

bool read_value(const Json& json, BehaviorValue& output,
                std::vector<Error>& errors, const std::string& prefix) {
    if (!json.is_object()) {
        error(errors, ErrorCode::invalid_asset, prefix, "expected a typed value");
        return false;
    }
    unknown(json, {"type", "value"}, prefix, errors);
    std::string type;
    if (!text(json, "type", type, errors, prefix)) return false;
    const auto value = json.find("value");
    if (value == json.end()) {
        error(errors, ErrorCode::invalid_asset, prefix + ".value", "is required");
        return false;
    }
    if (type == "boolean" && value->is_boolean()) output = value->get<bool>();
    else if (type == "integer" && value->is_number_integer()) output = value->get<std::int64_t>();
    else if (type == "float" && value->is_number()) {
        const float number = value->get<float>();
        if (!std::isfinite(number)) error(errors, ErrorCode::invalid_asset, prefix + ".value", "must be finite");
        else output = number;
    } else if (type == "text" && value->is_string()) output = value->get<std::string>();
    else if (type == "vec2" && value->is_object()) {
        const auto x = value->find("x");
        const auto y = value->find("y");
        if (x == value->end() || y == value->end() || !x->is_number() || !y->is_number())
            error(errors, ErrorCode::invalid_asset, prefix + ".value", "expected finite x and y");
        else {
            core::Vec2 parsed{x->get<float>(), y->get<float>()};
            if (!std::isfinite(parsed.x) || !std::isfinite(parsed.y))
                error(errors, ErrorCode::invalid_asset, prefix + ".value", "expected finite x and y");
            else output = parsed;
        }
    } else if (type == "resource" && value->is_object()) {
        ResourceReference reference;
        unknown(*value, {"id", "expectedType"}, prefix + ".value", errors);
        if (text(*value, "id", reference.id.value, errors, prefix + ".value") &&
            text(*value, "expectedType", reference.expected_type, errors, prefix + ".value"))
            output = std::move(reference);
    } else error(errors, ErrorCode::invalid_asset, prefix, "value does not match its type");
    return errors.empty();
}

struct PortSpec { std::string_view id; BehaviorPortDirection direction; BehaviorValueType type; };

std::vector<PortSpec> ports_for(const std::string_view type) {
    if (type.ends_with("_source")) return {{"out", BehaviorPortDirection::output, BehaviorValueType::signal}};
    if (type == "branch") return {{"in", BehaviorPortDirection::input, BehaviorValueType::signal},
                                  {"true", BehaviorPortDirection::output, BehaviorValueType::signal},
                                  {"false", BehaviorPortDirection::output, BehaviorValueType::signal}};
    return {{"in", BehaviorPortDirection::input, BehaviorValueType::signal},
            {"out", BehaviorPortDirection::output, BehaviorValueType::signal}};
}

bool needs_semantic_id(const std::string_view type) {
    return type == "action_source" || type == "ai_source" || type == "event_source" ||
           type == "trigger_source" || type == "timer_source" || type == "property_source";
}

ValidationReport parse_validation(const ProjectManifest& manifest,
                                  std::string_view json) {
    auto parsed = parse_behavior_graph(manifest, json);
    return {.errors = std::move(parsed.errors)};
}
} // namespace

std::string_view to_string(const BehaviorValueType type) noexcept {
    switch (type) {
    case BehaviorValueType::signal: return "signal";
    case BehaviorValueType::boolean: return "boolean";
    case BehaviorValueType::integer: return "integer";
    case BehaviorValueType::scalar: return "float";
    case BehaviorValueType::text: return "text";
    case BehaviorValueType::vec2: return "vec2";
    case BehaviorValueType::resource: return "resource";
    }
    return "signal";
}

bool behavior_value_matches(const BehaviorValueType type,
                            const BehaviorValue& value) noexcept {
    return (type == BehaviorValueType::boolean && std::holds_alternative<bool>(value)) ||
        (type == BehaviorValueType::integer && std::holds_alternative<std::int64_t>(value)) ||
        (type == BehaviorValueType::scalar && std::holds_alternative<float>(value)) ||
        (type == BehaviorValueType::text && std::holds_alternative<std::string>(value)) ||
        (type == BehaviorValueType::vec2 && std::holds_alternative<core::Vec2>(value)) ||
        (type == BehaviorValueType::resource && std::holds_alternative<ResourceReference>(value));
}

bool is_behavior_node_type(const std::string_view type) noexcept {
    static constexpr std::string_view types[] = {
        "action_source", "ai_source", "event_source", "trigger_source",
        "timer_source", "property_source", "condition", "branch", "sequence",
        "delay", "cooldown", "state", "transition", "set_property",
        "emit_event", "play_animation", "move", "activate_mechanic",
        "transform_entity"};
    return std::ranges::find(types, type) != std::end(types);
}

std::filesystem::path behavior_graph_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "behaviors" / (id.value + ".behavior.json");
}

ValidationReport validate_behavior_graph(const ProjectManifest&,
                                         const BehaviorGraph& graph) {
    ValidationReport report;
    if (graph.document.schema_version != current_behavior_graph_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version, "schemaVersion", "only behavior schema version 1 is supported");
    if (graph.document.type != "behavior")
        error(report.errors, ErrorCode::invalid_asset, "type", "must be behavior");
    if (!core::ResourceId::is_valid(graph.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (graph.document.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name", "must not be empty");

    std::set<std::string> parameter_ids;
    for (std::size_t index = 0; index < graph.parameters.size(); ++index) {
        const auto& parameter = graph.parameters[index];
        const auto prefix = "parameters[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(parameter.id) || !parameter_ids.insert(parameter.id).second)
            error(report.errors, ErrorCode::duplicate_resource, prefix + ".id", "must be valid and unique");
        if (parameter.type == BehaviorValueType::signal || !behavior_value_matches(parameter.type, parameter.default_value))
            error(report.errors, ErrorCode::resource_type_mismatch, prefix + ".defaultValue", "does not match parameter type");
    }

    std::map<std::string, const BehaviorNodeDefinition*> nodes;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const auto& node = graph.nodes[index];
        const auto prefix = "nodes[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(node.id) || !nodes.emplace(node.id, &node).second)
            error(report.errors, ErrorCode::duplicate_resource, prefix + ".id", "must be valid and unique");
        if (!is_behavior_node_type(node.type))
            error(report.errors, ErrorCode::invalid_asset, prefix + ".type", "unsupported behavior node type");
        std::set<std::string> ports;
        for (const auto& port : node.ports)
            if (!core::ResourceId::is_valid(port.id) || !ports.insert(port.id).second)
                error(report.errors, ErrorCode::duplicate_resource, prefix + ".ports", "port ids must be valid and unique");
        if (is_behavior_node_type(node.type)) {
            const auto expected = ports_for(node.type);
            if (node.ports.size() != expected.size())
                error(report.errors, ErrorCode::invalid_asset, prefix + ".ports", "ports must match the built-in node schema");
            for (const auto& spec : expected) {
                const auto found = std::ranges::find_if(node.ports, [&](const auto& port) { return port.id == spec.id; });
                if (found == node.ports.end() || found->direction != spec.direction || found->type != spec.type)
                    error(report.errors, ErrorCode::resource_type_mismatch, prefix + ".ports." + std::string(spec.id), "port does not match the built-in schema");
            }
        }
        std::set<std::string> properties;
        for (const auto& property : node.properties)
            if (!core::ResourceId::is_valid(property.id) || !properties.insert(property.id).second)
                error(report.errors, ErrorCode::duplicate_resource, prefix + ".properties", "property ids must be valid and unique");
        if (needs_semantic_id(node.type)) {
            const auto found = std::ranges::find_if(node.properties, [](const auto& property) { return property.id == "semantic_id"; });
            if (found == node.properties.end() || !std::holds_alternative<std::string>(found->value) || std::get<std::string>(found->value).empty())
                error(report.errors, ErrorCode::invalid_asset, prefix + ".properties.semantic_id", "source requires a non-empty semantic id");
        }
        const auto node_property = [&](std::string_view id) -> const BehaviorValue* {
            const auto found = std::ranges::find_if(node.properties,
                [&](const auto& property) { return property.id == id; });
            return found == node.properties.end() ? nullptr : &found->value;
        };
        const auto require_resource = [&](std::string_view id,
                                          std::string_view expected) {
            const auto* value = node_property(id);
            const auto* reference = value ? std::get_if<ResourceReference>(value) : nullptr;
            if (!reference || reference->expected_type != expected ||
                !core::ResourceId::is_valid(reference->id.value))
                error(report.errors, ErrorCode::resource_type_mismatch,
                      prefix + ".properties." + std::string(id),
                      "requires a " + std::string(expected) + " resource");
        };
        if (node.type == "move" &&
            (!node_property("vector") ||
             !std::holds_alternative<core::Vec2>(*node_property("vector"))))
            error(report.errors, ErrorCode::resource_type_mismatch,
                  prefix + ".properties.vector", "move requires a Vec2");
        if (node.type == "play_animation") require_resource("animation", "animation");
        if (node.type == "activate_mechanic") require_resource("mechanic", "mechanic");
        if (node.type == "transform_entity") require_resource("transformation", "transformation");
    }

    std::set<std::string> connection_ids;
    std::map<std::string, std::vector<std::string>> adjacency;
    for (std::size_t index = 0; index < graph.connections.size(); ++index) {
        const auto& connection = graph.connections[index];
        const auto prefix = "connections[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(connection.id) || !connection_ids.insert(connection.id).second)
            error(report.errors, ErrorCode::duplicate_resource, prefix + ".id", "must be valid and unique");
        const auto from_node = nodes.find(connection.from_node);
        const auto to_node = nodes.find(connection.to_node);
        if (from_node == nodes.end() || to_node == nodes.end()) {
            error(report.errors, ErrorCode::missing_resource, prefix, "connection node is missing");
            continue;
        }
        const auto from_port = std::ranges::find_if(from_node->second->ports, [&](const auto& port) { return port.id == connection.from_port; });
        const auto to_port = std::ranges::find_if(to_node->second->ports, [&](const auto& port) { return port.id == connection.to_port; });
        if (from_port == from_node->second->ports.end() || to_port == to_node->second->ports.end())
            error(report.errors, ErrorCode::missing_resource, prefix, "connection port is missing");
        else if (from_port->direction != BehaviorPortDirection::output || to_port->direction != BehaviorPortDirection::input || from_port->type != to_port->type)
            error(report.errors, ErrorCode::resource_type_mismatch, prefix, "connection ports are incompatible");
        else adjacency[connection.from_node].push_back(connection.to_node);
    }
    std::map<std::string, int> visit;
    const auto walk = [&](const auto& self, const std::string& node) -> void {
        visit[node] = 1;
        for (const auto& target : adjacency[node]) {
            if (visit[target] == 1) error(report.errors, ErrorCode::resource_cycle, "connections", "behavior flow cycle detected");
            else if (visit[target] == 0) self(self, target);
        }
        visit[node] = 2;
    };
    for (const auto& [id, unused] : nodes) if (visit[id] == 0) walk(walk, id);
    return report;
}

std::vector<ResourceReference> behavior_graph_resource_references(const BehaviorGraph& graph) {
    std::vector<ResourceReference> references;
    for (const auto& parameter : graph.parameters)
        if (const auto* value = std::get_if<ResourceReference>(&parameter.default_value)) references.push_back(*value);
    for (const auto& node : graph.nodes)
        for (const auto& property : node.properties)
            if (const auto* value = std::get_if<ResourceReference>(&property.value)) references.push_back(*value);
    return references;
}

std::string serialize_behavior_graph(const BehaviorGraph& graph) {
    Json json{{"schemaVersion", graph.document.schema_version}, {"type", graph.document.type},
              {"id", graph.document.id.value}, {"name", graph.document.name},
              {"parameters", Json::array()}, {"nodes", Json::array()}, {"connections", Json::array()}};
    for (const auto& parameter : graph.parameters)
        json["parameters"].push_back({{"id", parameter.id}, {"valueType", to_string(parameter.type)}, {"defaultValue", value_json(parameter.default_value)}});
    for (const auto& node : graph.nodes) {
        Json item{{"id", node.id}, {"nodeType", node.type}, {"ports", Json::array()}, {"properties", Json::array()}};
        for (const auto& port : node.ports)
            item["ports"].push_back({{"id", port.id}, {"direction", port.direction == BehaviorPortDirection::input ? "input" : "output"}, {"valueType", to_string(port.type)}});
        for (const auto& property : node.properties)
            item["properties"].push_back({{"id", property.id}, {"value", value_json(property.value)}});
        json["nodes"].push_back(std::move(item));
    }
    for (const auto& connection : graph.connections)
        json["connections"].push_back({{"id", connection.id}, {"fromNode", connection.from_node}, {"fromPort", connection.from_port}, {"toNode", connection.to_node}, {"toPort", connection.to_port}});
    return json.dump(2) + '\n';
}

BehaviorGraphResult parse_behavior_graph(const ProjectManifest& manifest,
                                         std::string_view contents) {
    BehaviorGraphResult result;
    Json json;
    try { json = Json::parse(contents); }
    catch (...) { error(result.errors, ErrorCode::invalid_json, "behavior", "cannot parse behavior JSON"); return result; }
    if (!json.is_object()) { error(result.errors, ErrorCode::invalid_asset, "behavior", "top-level value must be an object"); return result; }
    unknown(json, {"schemaVersion", "type", "id", "name", "parameters", "nodes", "connections"}, "", result.errors);
    BehaviorGraph graph;
    const auto schema = json.find("schemaVersion");
    if (schema == json.end() || !schema->is_number_unsigned()) error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected unsigned integer");
    else graph.document.schema_version = schema->get<std::uint32_t>();
    text(json, "type", graph.document.type, result.errors);
    text(json, "id", graph.document.id.value, result.errors);
    text(json, "name", graph.document.name, result.errors);
    const auto parameters = json.find("parameters");
    if (parameters == json.end() || !parameters->is_array()) error(result.errors, ErrorCode::invalid_asset, "parameters", "expected an array");
    else for (std::size_t index = 0; index < parameters->size(); ++index) {
        const auto& item = (*parameters)[index]; const auto prefix = "parameters[" + std::to_string(index) + "]";
        if (!item.is_object()) { error(result.errors, ErrorCode::invalid_asset, prefix, "expected an object"); continue; }
        unknown(item, {"id", "valueType", "defaultValue"}, prefix, result.errors);
        BehaviorParameterDefinition parameter; std::string type;
        text(item, "id", parameter.id, result.errors, prefix); text(item, "valueType", type, result.errors, prefix);
        const auto parsed_type = value_type(type); if (!parsed_type) error(result.errors, ErrorCode::invalid_asset, prefix + ".valueType", "unsupported value type"); else parameter.type = *parsed_type;
        const auto value = item.find("defaultValue"); if (value == item.end()) error(result.errors, ErrorCode::invalid_asset, prefix + ".defaultValue", "is required"); else read_value(*value, parameter.default_value, result.errors, prefix + ".defaultValue");
        graph.parameters.push_back(std::move(parameter));
    }
    const auto nodes = json.find("nodes");
    if (nodes == json.end() || !nodes->is_array()) error(result.errors, ErrorCode::invalid_asset, "nodes", "expected an array");
    else for (std::size_t index = 0; index < nodes->size(); ++index) {
        const auto& item = (*nodes)[index]; const auto prefix = "nodes[" + std::to_string(index) + "]";
        if (!item.is_object()) { error(result.errors, ErrorCode::invalid_asset, prefix, "expected an object"); continue; }
        unknown(item, {"id", "nodeType", "ports", "properties"}, prefix, result.errors);
        BehaviorNodeDefinition node; text(item, "id", node.id, result.errors, prefix); text(item, "nodeType", node.type, result.errors, prefix);
        const auto ports = item.find("ports"); if (ports == item.end() || !ports->is_array()) error(result.errors, ErrorCode::invalid_asset, prefix + ".ports", "expected an array");
        else for (std::size_t p = 0; p < ports->size(); ++p) {
            const auto& value = (*ports)[p]; const auto pp = prefix + ".ports[" + std::to_string(p) + "]"; BehaviorPortDefinition port; std::string direction, type;
            if (!value.is_object()) { error(result.errors, ErrorCode::invalid_asset, pp, "expected an object"); continue; }
            unknown(value, {"id", "direction", "valueType"}, pp, result.errors); text(value, "id", port.id, result.errors, pp); text(value, "direction", direction, result.errors, pp); text(value, "valueType", type, result.errors, pp);
            if (direction == "input") port.direction = BehaviorPortDirection::input; else if (direction == "output") port.direction = BehaviorPortDirection::output; else error(result.errors, ErrorCode::invalid_asset, pp + ".direction", "expected input or output");
            const auto parsed_type = value_type(type); if (!parsed_type) error(result.errors, ErrorCode::invalid_asset, pp + ".valueType", "unsupported value type"); else port.type = *parsed_type;
            node.ports.push_back(std::move(port));
        }
        const auto properties = item.find("properties"); if (properties == item.end() || !properties->is_array()) error(result.errors, ErrorCode::invalid_asset, prefix + ".properties", "expected an array");
        else for (std::size_t p = 0; p < properties->size(); ++p) {
            const auto& value = (*properties)[p]; const auto pp = prefix + ".properties[" + std::to_string(p) + "]"; BehaviorNodeProperty property;
            if (!value.is_object()) { error(result.errors, ErrorCode::invalid_asset, pp, "expected an object"); continue; }
            unknown(value, {"id", "value"}, pp, result.errors); text(value, "id", property.id, result.errors, pp);
            const auto typed = value.find("value"); if (typed == value.end()) error(result.errors, ErrorCode::invalid_asset, pp + ".value", "is required"); else read_value(*typed, property.value, result.errors, pp + ".value");
            node.properties.push_back(std::move(property));
        }
        graph.nodes.push_back(std::move(node));
    }
    const auto connections = json.find("connections");
    if (connections == json.end() || !connections->is_array()) error(result.errors, ErrorCode::invalid_asset, "connections", "expected an array");
    else for (std::size_t index = 0; index < connections->size(); ++index) {
        const auto& item = (*connections)[index]; const auto prefix = "connections[" + std::to_string(index) + "]"; BehaviorConnection connection;
        if (!item.is_object()) { error(result.errors, ErrorCode::invalid_asset, prefix, "expected an object"); continue; }
        unknown(item, {"id", "fromNode", "fromPort", "toNode", "toPort"}, prefix, result.errors);
        text(item, "id", connection.id, result.errors, prefix); text(item, "fromNode", connection.from_node, result.errors, prefix); text(item, "fromPort", connection.from_port, result.errors, prefix); text(item, "toNode", connection.to_node, result.errors, prefix); text(item, "toPort", connection.to_port, result.errors, prefix);
        graph.connections.push_back(std::move(connection));
    }
    if (!result.errors.empty()) return result;
    auto validation = validate_behavior_graph(manifest, graph);
    if (!validation.ok()) { result.errors = std::move(validation.errors); return result; }
    result.asset = std::move(graph); return result;
}

BehaviorGraphResult load_behavior_graph(const std::filesystem::path& root,
                                        const ProjectManifest& manifest,
                                        const std::filesystem::path& path) {
    auto storage = load_document(root, path, [&](std::string_view value) { return parse_validation(manifest, value); });
    BehaviorGraphResult result; result.errors = std::move(storage.errors);
    if (storage.contents) result = parse_behavior_graph(manifest, *storage.contents);
    if (result.ok() && path != behavior_graph_document_path(manifest, result.asset->document.id)) {
        result.asset.reset(); error(result.errors, ErrorCode::invalid_path, "document", "document filename does not match its id");
    }
    return result;
}

BehaviorGraphResult publish_behavior_graph(const std::filesystem::path& root,
                                           const ProjectManifest& manifest,
                                           const BehaviorGraph& graph) {
    BehaviorGraphResult result; auto validation = validate_behavior_graph(manifest, graph);
    if (!validation.ok()) { result.errors = std::move(validation.errors); return result; }
    const auto path = behavior_graph_document_path(manifest, graph.document.id);
    const auto saved = save_document_atomic(root, path, serialize_behavior_graph(graph), [&](std::string_view value) { return parse_validation(manifest, value); });
    if (!saved.ok()) { result.errors = saved.errors; return result; }
    return load_behavior_graph(root, manifest, path);
}
} // namespace fabric::project
