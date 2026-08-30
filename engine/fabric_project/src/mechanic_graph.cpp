#include "fabric/project/mechanic_graph.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void error(std::vector<Error>& errors, const ErrorCode code,
           std::string field, std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

void reject_unknown(const Json& object,
                    const std::initializer_list<std::string_view> allowed,
                    const std::string& field, std::vector<Error>& errors) {
    for (const auto& [key, _] : object.items()) {
        if (!std::ranges::any_of(allowed,
                [&](const auto candidate) { return key == candidate; }))
            error(errors, ErrorCode::invalid_asset,
                  field.empty() ? key : field + "." + key, "unknown field");
    }
}

std::string_view value_type_name(const MechanicValueType type) {
    return to_string(type);
}

std::optional<MechanicValueType> parse_value_type(const std::string_view type) {
    if (type == "bool") return MechanicValueType::boolean;
    if (type == "integer") return MechanicValueType::integer;
    if (type == "scalar") return MechanicValueType::scalar;
    if (type == "text") return MechanicValueType::text;
    if (type == "vec2") return MechanicValueType::vec2;
    if (type == "resource") return MechanicValueType::resource;
    if (type == "body") return MechanicValueType::body_handle;
    if (type == "pivot") return MechanicValueType::pivot_handle;
    if (type == "joint") return MechanicValueType::joint_handle;
    return std::nullopt;
}

bool read_text(const Json& object, const char* key, std::string& value,
               std::vector<Error>& errors, const std::string& prefix = {}) {
    const auto found = object.find(key);
    const auto field = prefix.empty() ? std::string{key} : prefix + "." + key;
    if (found == object.end() || !found->is_string()) {
        error(errors, ErrorCode::invalid_asset, field, "expected a string");
        return false;
    }
    value = found->get<std::string>();
    return true;
}

bool read_value_type(const Json& object, const char* key,
                     MechanicValueType& value, std::vector<Error>& errors,
                     const std::string& prefix) {
    std::string text;
    if (!read_text(object, key, text, errors, prefix)) return false;
    const auto parsed = parse_value_type(text);
    if (!parsed) {
        error(errors, ErrorCode::invalid_asset, prefix + "." + key,
              "unsupported mechanic value type");
        return false;
    }
    value = *parsed;
    return true;
}

Json serialize_value(const MechanicValue& value) {
    return std::visit([](const auto& item) -> Json {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, bool>)
            return {{"kind", "bool"}, {"value", item}};
        else if constexpr (std::is_same_v<Value, std::int64_t>)
            return {{"kind", "integer"}, {"value", item}};
        else if constexpr (std::is_same_v<Value, float>)
            return {{"kind", "scalar"}, {"value", item}};
        else if constexpr (std::is_same_v<Value, std::string>)
            return {{"kind", "text"}, {"value", item}};
        else if constexpr (std::is_same_v<Value, core::Vec2>)
            return {{"kind", "vec2"},
                    {"value", {{"x", item.x}, {"y", item.y}}}};
        else
            return {{"kind", "resource"},
                    {"value", {{"id", item.id.value},
                               {"expectedType", item.expected_type}}}};
    }, value);
}

std::optional<MechanicValue> parse_value(
    const Json& object, const std::string& field, std::vector<Error>& errors) {
    if (!object.is_object()) {
        error(errors, ErrorCode::invalid_asset, field, "expected an object");
        return std::nullopt;
    }
    reject_unknown(object, {"kind", "value"}, field, errors);
    std::string kind;
    if (!read_text(object, "kind", kind, errors, field)) return std::nullopt;
    const auto value = object.find("value");
    if (value == object.end()) {
        error(errors, ErrorCode::invalid_asset, field + ".value",
              "field is required");
        return std::nullopt;
    }
    if (kind == "bool" && value->is_boolean())
        return value->get<bool>();
    if (kind == "integer" && value->is_number_integer())
        return value->get<std::int64_t>();
    if (kind == "scalar" && value->is_number())
        return static_cast<float>(value->get<double>());
    if (kind == "text" && value->is_string())
        return value->get<std::string>();
    if (kind == "vec2" && value->is_object()) {
        reject_unknown(*value, {"x", "y"}, field + ".value", errors);
        const auto x = value->find("x");
        const auto y = value->find("y");
        if (x != value->end() && y != value->end() && x->is_number() &&
            y->is_number())
            return core::Vec2{static_cast<float>(x->get<double>()),
                              static_cast<float>(y->get<double>())};
    }
    if (kind == "resource" && value->is_object()) {
        reject_unknown(*value, {"id", "expectedType"}, field + ".value",
                       errors);
        ResourceReference reference;
        if (read_text(*value, "id", reference.id.value, errors,
                      field + ".value") &&
            read_text(*value, "expectedType", reference.expected_type,
                      errors, field + ".value"))
            return reference;
        return std::nullopt;
    }
    error(errors, ErrorCode::invalid_asset, field,
          "kind and value are incompatible");
    return std::nullopt;
}

bool finite_value(const MechanicValue& value) {
    if (const auto* scalar = std::get_if<float>(&value))
        return std::isfinite(*scalar);
    if (const auto* vector = std::get_if<core::Vec2>(&value))
        return std::isfinite(vector->x) && std::isfinite(vector->y);
    return true;
}

void validate_value(const MechanicValue& value, const std::string& field,
                    std::vector<Error>& errors) {
    if (!finite_value(value))
        error(errors, ErrorCode::invalid_asset, field, "must be finite");
    if (const auto* reference = std::get_if<ResourceReference>(&value);
        reference != nullptr &&
        (!core::ResourceId::is_valid(reference->id.value) ||
         reference->expected_type.empty()))
        error(errors, ErrorCode::resource_type_mismatch, field,
              "resource reference must have a valid id and expected type");
}

const MechanicNodeDefinition* find_node(const MechanicGraph& graph,
                                        const std::string& id) {
    const auto found = std::ranges::find(graph.nodes, id,
                                         &MechanicNodeDefinition::id);
    return found == graph.nodes.end() ? nullptr : &*found;
}

const MechanicPortDefinition* find_port(const MechanicNodeDefinition* node,
                                        const std::string& id) {
    if (node == nullptr) return nullptr;
    const auto found = std::ranges::find(node->ports, id,
                                         &MechanicPortDefinition::id);
    return found == node->ports.end() ? nullptr : &*found;
}

const MechanicNodeProperty* find_property(const MechanicNodeDefinition& node,
                                          const std::string_view id) {
    const auto found = std::ranges::find(node.properties, id,
                                         &MechanicNodeProperty::id);
    return found == node.properties.end() ? nullptr : &*found;
}

template <typename Value>
const Value* property_value(const MechanicNodeDefinition& node,
                            const std::string_view id) {
    const auto* property = find_property(node, id);
    return property == nullptr ? nullptr : std::get_if<Value>(&property->value);
}

void validate_builtin_node(const MechanicNodeDefinition& node,
                           const std::string& field,
                           std::vector<Error>& errors) {
    const auto kind = mechanic_node_kind(node.type);
    if (!kind) {
        error(errors, ErrorCode::invalid_asset, field + ".nodeType",
              "unsupported mechanic node type");
        return;
    }
    const auto& schema = mechanic_node_schema(*kind);
    for (const auto& expected : schema.ports) {
        const auto* port = find_port(&node, std::string{expected.id});
        if (port == nullptr) {
            if (expected.required)
                error(errors, ErrorCode::missing_resource, field + ".ports",
                      "required port is missing: " + std::string{expected.id});
            continue;
        }
        if (port->direction != expected.direction || port->type != expected.type)
            error(errors, ErrorCode::resource_type_mismatch,
                  field + ".ports." + std::string{expected.id},
                  "port does not match its built-in schema");
    }
    for (const auto& port : node.ports)
        if (!std::ranges::any_of(schema.ports, [&](const auto& expected) {
                return expected.id == port.id;
            }))
            error(errors, ErrorCode::invalid_asset,
                  field + ".ports." + port.id,
                  "port is not supported by this node type");

    for (const auto& expected : schema.properties) {
        const auto* property = find_property(node, expected.id);
        if (property == nullptr) {
            if (expected.required)
                error(errors, ErrorCode::missing_resource,
                      field + ".properties",
                      "required property is missing: " +
                          std::string{expected.id});
            continue;
        }
        if (!mechanic_value_matches(expected.type, property->value))
            error(errors, ErrorCode::resource_type_mismatch,
                  field + ".properties." + std::string{expected.id},
                  "property does not match its built-in schema");
    }
    for (const auto& property : node.properties)
        if (!std::ranges::any_of(schema.properties, [&](const auto& expected) {
                return expected.id == property.id;
            }))
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties." + property.id,
                  "property is not supported by this node type");

    if (*kind == MechanicNodeKind::body) {
        const auto* body_type = property_value<std::string>(node, "body-type");
        if (body_type && *body_type != "static" && *body_type != "kinematic" &&
            *body_type != "dynamic")
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.body-type", "unsupported body type");
        const auto* size = property_value<core::Vec2>(node, "size");
        if (size && (size->x <= 0.0F || size->y <= 0.0F))
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.size", "body size must be positive");
        const auto* density = property_value<float>(node, "density");
        if (density && *density < 0.0F)
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.density", "must be non-negative");
        const auto* friction = property_value<float>(node, "friction");
        if (friction && (*friction < 0.0F || *friction > 1.0F))
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.friction", "must be in [0,1]");
    } else if (*kind == MechanicNodeKind::joint ||
               *kind == MechanicNodeKind::constraint) {
        const auto* minimum = property_value<float>(node, "min-angle");
        const auto* maximum = property_value<float>(node, "max-angle");
        if (minimum && maximum && *minimum > *maximum)
            error(errors, ErrorCode::invalid_asset, field + ".properties",
                  "minimum angle must not exceed maximum angle");
        constexpr float maximum_revolute_angle = 178.0F;
        if ((minimum && *minimum < -maximum_revolute_angle) ||
            (maximum && *maximum > maximum_revolute_angle))
            error(errors, ErrorCode::invalid_asset, field + ".properties",
                  "revolute angles must stay in [-178,178] degrees");
    } else if (*kind == MechanicNodeKind::motor) {
        const auto* torque = property_value<float>(node, "max-torque");
        if (torque && *torque < 0.0F)
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.max-torque", "must be non-negative");
        const auto* acceleration = property_value<float>(node, "acceleration");
        if (acceleration && *acceleration < 0.0F)
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.acceleration", "must be non-negative");
        const auto* direction = property_value<std::int64_t>(node, "direction");
        if (direction && *direction != -1 && *direction != 1)
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.direction", "must be -1 or 1");
    } else if (*kind == MechanicNodeKind::sensor) {
        const auto* size = property_value<core::Vec2>(node, "size");
        if (size && (size->x <= 0.0F || size->y <= 0.0F))
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.size", "sensor size must be positive");
    } else if (*kind == MechanicNodeKind::event) {
        const auto* event_id = property_value<std::string>(node, "event-id");
        if (event_id && !core::ResourceId::is_valid(*event_id))
            error(errors, ErrorCode::invalid_resource_id,
                  field + ".properties.event-id", "must be a valid event id");
        const auto* mode = property_value<std::string>(node, "mode");
        if (mode && *mode != "emit" && *mode != "listen")
            error(errors, ErrorCode::invalid_asset,
                  field + ".properties.mode", "must be emit or listen");
        if (mode && *mode == "listen" && find_port(&node, "active") == nullptr)
            error(errors, ErrorCode::missing_resource, field + ".ports.active",
                  "listen events require the active output port");
    }
}

bool has_connection_cycle(const MechanicGraph& graph) {
    std::unordered_map<std::string, std::vector<std::string>> edges;
    for (const auto& connection : graph.connections)
        edges[connection.from_node].push_back(connection.to_node);
    std::unordered_map<std::string, int> state;
    const std::function<bool(const std::string&)> visit =
        [&](const std::string& node) {
            if (state[node] == 1) return true;
            if (state[node] == 2) return false;
            state[node] = 1;
            for (const auto& target : edges[node])
                if (visit(target)) return true;
            state[node] = 2;
            return false;
        };
    for (const auto& node : graph.nodes)
        if (visit(node.id)) return true;
    return false;
}

ValidationReport parse_validation(const ProjectManifest& manifest,
                                  const std::string_view text) {
    const auto parsed = parse_mechanic_graph(manifest, text);
    return {.errors = parsed.errors};
}

} // namespace

bool mechanic_value_matches(const MechanicValueType type,
                            const MechanicValue& value) noexcept {
    switch (type) {
    case MechanicValueType::boolean: return std::holds_alternative<bool>(value);
    case MechanicValueType::integer:
        return std::holds_alternative<std::int64_t>(value);
    case MechanicValueType::scalar: return std::holds_alternative<float>(value);
    case MechanicValueType::text:
        return std::holds_alternative<std::string>(value);
    case MechanicValueType::vec2: return std::holds_alternative<core::Vec2>(value);
    case MechanicValueType::resource:
        return std::holds_alternative<ResourceReference>(value);
    case MechanicValueType::body_handle:
    case MechanicValueType::pivot_handle:
    case MechanicValueType::joint_handle:
        return false;
    }
    return false;
}

std::string_view to_string(const MechanicValueType type) noexcept {
    switch (type) {
    case MechanicValueType::boolean: return "bool";
    case MechanicValueType::integer: return "integer";
    case MechanicValueType::scalar: return "scalar";
    case MechanicValueType::text: return "text";
    case MechanicValueType::vec2: return "vec2";
    case MechanicValueType::resource: return "resource";
    case MechanicValueType::body_handle: return "body";
    case MechanicValueType::pivot_handle: return "pivot";
    case MechanicValueType::joint_handle: return "joint";
    }
    return "scalar";
}

std::string_view to_string(const MechanicNodeKind kind) noexcept {
    switch (kind) {
    case MechanicNodeKind::body: return "body";
    case MechanicNodeKind::pivot: return "pivot";
    case MechanicNodeKind::joint: return "joint";
    case MechanicNodeKind::motor: return "motor";
    case MechanicNodeKind::sensor: return "sensor";
    case MechanicNodeKind::constraint: return "constraint";
    case MechanicNodeKind::event: return "event";
    }
    return "body";
}

std::optional<MechanicNodeKind> mechanic_node_kind(
    const std::string_view type) noexcept {
    if (type == "body") return MechanicNodeKind::body;
    if (type == "pivot") return MechanicNodeKind::pivot;
    if (type == "joint") return MechanicNodeKind::joint;
    if (type == "motor") return MechanicNodeKind::motor;
    if (type == "sensor") return MechanicNodeKind::sensor;
    if (type == "constraint") return MechanicNodeKind::constraint;
    if (type == "event") return MechanicNodeKind::event;
    return std::nullopt;
}

const MechanicNodeSchema& mechanic_node_schema(const MechanicNodeKind kind) {
    using Direction = MechanicPortDirection;
    using Type = MechanicValueType;
    static const std::array<MechanicNodeSchema, 7> schemas{{
        {MechanicNodeKind::body, "body",
         {{"body", Direction::output, Type::body_handle}},
         {{"body-type", Type::text}, {"position", Type::vec2},
          {"size", Type::vec2}, {"rotation", Type::scalar},
          {"density", Type::scalar}, {"friction", Type::scalar},
          {"entity", Type::resource, false}}},
        {MechanicNodeKind::pivot, "pivot",
         {{"body", Direction::input, Type::body_handle},
          {"pivot", Direction::output, Type::pivot_handle}},
         {{"position", Type::vec2}}},
        {MechanicNodeKind::joint, "joint",
         {{"body-a", Direction::input, Type::body_handle},
          {"body-b", Direction::input, Type::body_handle},
          {"pivot", Direction::input, Type::pivot_handle},
          {"joint", Direction::output, Type::joint_handle}},
         {{"limit-enabled", Type::boolean}, {"min-angle", Type::scalar},
          {"max-angle", Type::scalar}}},
        {MechanicNodeKind::motor, "motor",
         {{"joint", Direction::input, Type::joint_handle},
          {"enabled", Direction::input, Type::boolean},
          {"active", Direction::output, Type::boolean}},
         {{"speed", Type::scalar}, {"max-torque", Type::scalar},
          {"direction", Type::integer, false},
          {"acceleration", Type::scalar, false}}},
        {MechanicNodeKind::sensor, "sensor",
         {{"body", Direction::input, Type::body_handle},
          {"active", Direction::output, Type::boolean}},
         {{"center", Type::vec2}, {"size", Type::vec2},
          {"layer-mask", Type::integer, false}}},
        {MechanicNodeKind::constraint, "constraint",
         {{"body", Direction::input, Type::body_handle},
          {"pivot", Direction::input, Type::pivot_handle},
          {"active", Direction::output, Type::boolean}},
         {{"min-angle", Type::scalar}, {"max-angle", Type::scalar}}},
        {MechanicNodeKind::event, "event",
         {{"trigger", Direction::input, Type::boolean},
          {"active", Direction::output, Type::boolean, false}},
         {{"event-id", Type::text}, {"mode", Type::text, false}}},
    }};
    return schemas[static_cast<std::size_t>(kind)];
}

std::filesystem::path mechanic_graph_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "mechanics" /
        (id.value + ".mechanic.json");
}

ValidationReport validate_mechanic_graph(const ProjectManifest&,
                                         const MechanicGraph& graph) {
    ValidationReport report;
    if (graph.document.schema_version != current_mechanic_graph_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version,
              "schemaVersion", "only mechanic schema version 1 is supported");
    if (graph.document.type != "mechanic")
        error(report.errors, ErrorCode::invalid_asset, "type",
              "must be mechanic");
    if (!core::ResourceId::is_valid(graph.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id",
              "must be valid");
    if (graph.document.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name",
              "must not be empty");

    std::unordered_set<std::string> parameter_ids;
    std::unordered_set<std::string> parameter_targets;
    for (std::size_t index = 0; index < graph.parameters.size(); ++index) {
        const auto& parameter = graph.parameters[index];
        const auto field = "parameters[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(parameter.id))
            error(report.errors, ErrorCode::invalid_resource_id,
                  field + ".id", "must be valid");
        if (!parameter_ids.insert(parameter.id).second)
            error(report.errors, ErrorCode::duplicate_resource,
                  field + ".id", "parameter is duplicated");
        if (parameter.name.empty())
            error(report.errors, ErrorCode::invalid_asset,
                  field + ".name", "must not be empty");
        if (!mechanic_value_matches(parameter.type, parameter.default_value))
            error(report.errors, ErrorCode::resource_type_mismatch,
                  field + ".defaultValue", "does not match parameter type");
        validate_value(parameter.default_value, field + ".defaultValue",
                       report.errors);
        const auto* target_node = find_node(graph, parameter.target_node);
        const auto* target_property = target_node == nullptr
            ? nullptr : find_property(*target_node, parameter.target_property);
        if (target_node == nullptr || target_property == nullptr) {
            error(report.errors, ErrorCode::missing_resource, field + ".target",
                  "parameter target node or property is missing");
        } else if (const auto kind = mechanic_node_kind(target_node->type)) {
            const auto& schema = mechanic_node_schema(*kind);
            const auto expected = std::ranges::find(
                schema.properties, parameter.target_property,
                &MechanicNodePropertySchema::id);
            if (expected == schema.properties.end() ||
                expected->type != parameter.type)
                error(report.errors, ErrorCode::resource_type_mismatch,
                      field + ".target",
                      "parameter type does not match target property");
        }
        const auto target_key = parameter.target_node + "." +
            parameter.target_property;
        if (!parameter_targets.insert(target_key).second)
            error(report.errors, ErrorCode::duplicate_resource,
                  field + ".target", "parameter target is duplicated");
    }

    std::unordered_set<std::string> node_ids;
    for (std::size_t node_index = 0; node_index < graph.nodes.size(); ++node_index) {
        const auto& node = graph.nodes[node_index];
        const auto field = "nodes[" + std::to_string(node_index) + "]";
        if (!core::ResourceId::is_valid(node.id))
            error(report.errors, ErrorCode::invalid_resource_id,
                  field + ".id", "must be valid");
        if (!node_ids.insert(node.id).second)
            error(report.errors, ErrorCode::duplicate_resource,
                  field + ".id", "node is duplicated");
        if (!core::ResourceId::is_valid(node.type))
            error(report.errors, ErrorCode::invalid_resource_id,
                  field + ".nodeType", "must be valid");
        std::unordered_set<std::string> port_ids;
        for (std::size_t port_index = 0; port_index < node.ports.size(); ++port_index) {
            const auto& port = node.ports[port_index];
            const auto port_field = field + ".ports[" +
                std::to_string(port_index) + "]";
            if (!core::ResourceId::is_valid(port.id))
                error(report.errors, ErrorCode::invalid_resource_id,
                      port_field + ".id", "must be valid");
            if (!port_ids.insert(port.id).second)
                error(report.errors, ErrorCode::duplicate_resource,
                      port_field + ".id", "port is duplicated");
            if (port.name.empty())
                error(report.errors, ErrorCode::invalid_asset,
                      port_field + ".name", "must not be empty");
        }
        std::unordered_set<std::string> property_ids;
        for (std::size_t property_index = 0;
             property_index < node.properties.size(); ++property_index) {
            const auto& property = node.properties[property_index];
            const auto property_field = field + ".properties[" +
                std::to_string(property_index) + "]";
            if (!core::ResourceId::is_valid(property.id))
                error(report.errors, ErrorCode::invalid_resource_id,
                      property_field + ".id", "must be valid");
            if (!property_ids.insert(property.id).second)
                error(report.errors, ErrorCode::duplicate_resource,
                      property_field + ".id", "property is duplicated");
            validate_value(property.value, property_field + ".value",
                           report.errors);
        }
        validate_builtin_node(node, field, report.errors);
    }

    std::unordered_set<std::string> connections;
    std::unordered_set<std::string> connected_inputs;
    for (std::size_t index = 0; index < graph.connections.size(); ++index) {
        const auto& connection = graph.connections[index];
        const auto field = "connections[" + std::to_string(index) + "]";
        const auto* from_node = find_node(graph, connection.from_node);
        const auto* to_node = find_node(graph, connection.to_node);
        const auto* from_port = find_port(from_node, connection.from_port);
        const auto* to_port = find_port(to_node, connection.to_port);
        if (from_node == nullptr || to_node == nullptr)
            error(report.errors, ErrorCode::missing_resource, field,
                  "connection node is missing");
        else if (from_port == nullptr || to_port == nullptr)
            error(report.errors, ErrorCode::missing_resource, field,
                  "connection port is missing");
        else {
            if (from_port->direction != MechanicPortDirection::output ||
                to_port->direction != MechanicPortDirection::input)
                error(report.errors, ErrorCode::invalid_asset, field,
                      "connection must go from output to input");
            if (from_port->type != to_port->type)
                error(report.errors, ErrorCode::resource_type_mismatch, field,
                      "connected port types must match");
        }
        const auto key = connection.from_node + "." + connection.from_port +
            "->" + connection.to_node + "." + connection.to_port;
        if (!connections.insert(key).second)
            error(report.errors, ErrorCode::duplicate_resource, field,
                  "connection is duplicated");
        const auto input = connection.to_node + "." + connection.to_port;
        if (!connected_inputs.insert(input).second)
            error(report.errors, ErrorCode::invalid_asset, field,
                  "input port already has a connection");
    }
    if (has_connection_cycle(graph))
        error(report.errors, ErrorCode::resource_cycle, "connections",
              "mechanic control graph must be acyclic");
    return report;
}

ValidationReport validate_mechanic_parameter_overrides(
    const MechanicGraph& graph,
    const std::vector<MechanicParameterOverride>& overrides) {
    ValidationReport report;
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < overrides.size(); ++index) {
        const auto& override = overrides[index];
        const auto field = "mechanicOverrides[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(override.parameter_id))
            error(report.errors, ErrorCode::invalid_resource_id,
                  field + ".parameter", "must be a valid parameter id");
        if (!ids.insert(override.parameter_id).second)
            error(report.errors, ErrorCode::duplicate_resource,
                  field + ".parameter", "parameter override is duplicated");
        validate_value(override.value, field + ".value", report.errors);
        const auto parameter = std::ranges::find(
            graph.parameters, override.parameter_id,
            &MechanicParameterDefinition::id);
        if (parameter == graph.parameters.end()) {
            error(report.errors, ErrorCode::missing_resource,
                  field + ".parameter", "mechanic parameter does not exist");
        } else if (!mechanic_value_matches(parameter->type, override.value)) {
            error(report.errors, ErrorCode::resource_type_mismatch,
                  field + ".value", "override does not match parameter type");
        }
    }
    return report;
}

std::vector<ResourceReference> mechanic_parameter_override_resource_references(
    const std::vector<MechanicParameterOverride>& overrides) {
    std::vector<ResourceReference> references;
    for (const auto& override : overrides)
        if (const auto* reference =
                std::get_if<ResourceReference>(&override.value))
            references.push_back(*reference);
    return references;
}

std::vector<ResourceReference> mechanic_graph_resource_references(
    const MechanicGraph& graph) {
    std::vector<ResourceReference> references;
    const auto collect = [&](const MechanicValue& value) {
        if (const auto* reference = std::get_if<ResourceReference>(&value))
            references.push_back(*reference);
    };
    for (const auto& parameter : graph.parameters)
        collect(parameter.default_value);
    for (const auto& node : graph.nodes)
        for (const auto& property : node.properties) collect(property.value);
    return references;
}

std::string serialize_mechanic_graph(const MechanicGraph& graph) {
    Json json{{"schemaVersion", graph.document.schema_version},
              {"type", graph.document.type},
              {"id", graph.document.id.value},
              {"name", graph.document.name},
              {"parameters", Json::array()},
              {"nodes", Json::array()},
              {"connections", Json::array()}};
    for (const auto& parameter : graph.parameters)
        json["parameters"].push_back({
            {"id", parameter.id}, {"name", parameter.name},
            {"valueType", value_type_name(parameter.type)},
            {"defaultValue", serialize_value(parameter.default_value)},
            {"targetNode", parameter.target_node},
            {"targetProperty", parameter.target_property}});
    for (const auto& node : graph.nodes) {
        Json item{{"id", node.id}, {"nodeType", node.type},
                  {"ports", Json::array()}, {"properties", Json::array()}};
        for (const auto& port : node.ports)
            item["ports"].push_back({
                {"id", port.id}, {"name", port.name},
                {"direction", port.direction == MechanicPortDirection::input
                    ? "input" : "output"},
                {"valueType", value_type_name(port.type)}});
        for (const auto& property : node.properties)
            item["properties"].push_back({
                {"id", property.id}, {"value", serialize_value(property.value)}});
        json["nodes"].push_back(std::move(item));
    }
    for (const auto& connection : graph.connections)
        json["connections"].push_back({
            {"fromNode", connection.from_node},
            {"fromPort", connection.from_port},
            {"toNode", connection.to_node},
            {"toPort", connection.to_port}});
    return json.dump(2) + "\n";
}

MechanicGraphResult parse_mechanic_graph(
    const ProjectManifest& manifest, const std::string_view serialized) {
    MechanicGraphResult result;
    Json json;
    try { json = Json::parse(serialized); }
    catch (...) {
        error(result.errors, ErrorCode::invalid_json, "mechanic",
              "cannot parse mechanic JSON");
        return result;
    }
    if (!json.is_object()) {
        error(result.errors, ErrorCode::invalid_asset, "mechanic",
              "top-level value must be an object");
        return result;
    }
    reject_unknown(json, {"schemaVersion", "type", "id", "name",
                          "parameters", "nodes", "connections"},
                   {}, result.errors);
    MechanicGraph graph;
    const auto schema = json.find("schemaVersion");
    if (schema != json.end() && schema->is_number_unsigned())
        graph.document.schema_version = schema->get<std::uint32_t>();
    else
        error(result.errors, ErrorCode::invalid_asset, "schemaVersion",
              "expected an unsigned integer");
    read_text(json, "type", graph.document.type, result.errors);
    read_text(json, "id", graph.document.id.value, result.errors);
    read_text(json, "name", graph.document.name, result.errors);

    const auto parameters = json.find("parameters");
    if (parameters == json.end() || !parameters->is_array())
        error(result.errors, ErrorCode::invalid_asset, "parameters",
              "expected an array");
    else for (std::size_t index = 0; index < parameters->size(); ++index) {
        const auto& item = (*parameters)[index];
        const auto field = "parameters[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            error(result.errors, ErrorCode::invalid_asset, field,
                  "expected an object");
            continue;
        }
        reject_unknown(item, {"id", "name", "valueType", "defaultValue",
                              "targetNode", "targetProperty"},
                       field, result.errors);
        MechanicParameterDefinition parameter;
        read_text(item, "id", parameter.id, result.errors, field);
        read_text(item, "name", parameter.name, result.errors, field);
        read_value_type(item, "valueType", parameter.type, result.errors, field);
        read_text(item, "targetNode", parameter.target_node, result.errors,
                  field);
        read_text(item, "targetProperty", parameter.target_property,
                  result.errors, field);
        const auto value = item.find("defaultValue");
        if (value == item.end())
            error(result.errors, ErrorCode::invalid_asset,
                  field + ".defaultValue", "field is required");
        else if (auto parsed = parse_value(*value, field + ".defaultValue",
                                           result.errors))
            parameter.default_value = std::move(*parsed);
        graph.parameters.push_back(std::move(parameter));
    }

    const auto nodes = json.find("nodes");
    if (nodes == json.end() || !nodes->is_array())
        error(result.errors, ErrorCode::invalid_asset, "nodes",
              "expected an array");
    else for (std::size_t node_index = 0; node_index < nodes->size(); ++node_index) {
        const auto& item = (*nodes)[node_index];
        const auto field = "nodes[" + std::to_string(node_index) + "]";
        if (!item.is_object()) {
            error(result.errors, ErrorCode::invalid_asset, field,
                  "expected an object");
            continue;
        }
        reject_unknown(item, {"id", "nodeType", "ports", "properties"},
                       field, result.errors);
        MechanicNodeDefinition node;
        read_text(item, "id", node.id, result.errors, field);
        read_text(item, "nodeType", node.type, result.errors, field);
        const auto ports = item.find("ports");
        if (ports == item.end() || !ports->is_array())
            error(result.errors, ErrorCode::invalid_asset, field + ".ports",
                  "expected an array");
        else for (std::size_t port_index = 0; port_index < ports->size(); ++port_index) {
            const auto& port_json = (*ports)[port_index];
            const auto port_field = field + ".ports[" +
                std::to_string(port_index) + "]";
            if (!port_json.is_object()) {
                error(result.errors, ErrorCode::invalid_asset, port_field,
                      "expected an object");
                continue;
            }
            reject_unknown(port_json,
                           {"id", "name", "direction", "valueType"},
                           port_field, result.errors);
            MechanicPortDefinition port;
            read_text(port_json, "id", port.id, result.errors, port_field);
            read_text(port_json, "name", port.name, result.errors, port_field);
            std::string direction;
            if (read_text(port_json, "direction", direction, result.errors,
                          port_field)) {
                if (direction == "input")
                    port.direction = MechanicPortDirection::input;
                else if (direction == "output")
                    port.direction = MechanicPortDirection::output;
                else error(result.errors, ErrorCode::invalid_asset,
                           port_field + ".direction",
                           "expected input or output");
            }
            read_value_type(port_json, "valueType", port.type,
                            result.errors, port_field);
            node.ports.push_back(std::move(port));
        }
        const auto properties = item.find("properties");
        if (properties == item.end() || !properties->is_array())
            error(result.errors, ErrorCode::invalid_asset,
                  field + ".properties", "expected an array");
        else for (std::size_t property_index = 0;
                 property_index < properties->size(); ++property_index) {
            const auto& property_json = (*properties)[property_index];
            const auto property_field = field + ".properties[" +
                std::to_string(property_index) + "]";
            if (!property_json.is_object()) {
                error(result.errors, ErrorCode::invalid_asset, property_field,
                      "expected an object");
                continue;
            }
            reject_unknown(property_json, {"id", "value"}, property_field,
                           result.errors);
            MechanicNodeProperty property;
            read_text(property_json, "id", property.id, result.errors,
                      property_field);
            const auto value = property_json.find("value");
            if (value == property_json.end())
                error(result.errors, ErrorCode::invalid_asset,
                      property_field + ".value", "field is required");
            else if (auto parsed = parse_value(*value,
                                               property_field + ".value",
                                               result.errors))
                property.value = std::move(*parsed);
            node.properties.push_back(std::move(property));
        }
        graph.nodes.push_back(std::move(node));
    }

    const auto connections = json.find("connections");
    if (connections == json.end() || !connections->is_array())
        error(result.errors, ErrorCode::invalid_asset, "connections",
              "expected an array");
    else for (std::size_t index = 0; index < connections->size(); ++index) {
        const auto& item = (*connections)[index];
        const auto field = "connections[" + std::to_string(index) + "]";
        if (!item.is_object()) {
            error(result.errors, ErrorCode::invalid_asset, field,
                  "expected an object");
            continue;
        }
        reject_unknown(item, {"fromNode", "fromPort", "toNode", "toPort"},
                       field, result.errors);
        MechanicConnection connection;
        read_text(item, "fromNode", connection.from_node, result.errors, field);
        read_text(item, "fromPort", connection.from_port, result.errors, field);
        read_text(item, "toNode", connection.to_node, result.errors, field);
        read_text(item, "toPort", connection.to_port, result.errors, field);
        graph.connections.push_back(std::move(connection));
    }

    const auto validation = validate_mechanic_graph(manifest, graph);
    result.errors.insert(result.errors.end(), validation.errors.begin(),
                         validation.errors.end());
    if (result.errors.empty()) result.asset = std::move(graph);
    return result;
}

MechanicGraphResult load_mechanic_graph(
    const std::filesystem::path& root, const ProjectManifest& manifest,
    const std::filesystem::path& path) {
    const auto stored = load_document(root, path,
        [&](const std::string_view text) {
            return parse_validation(manifest, text);
        });
    MechanicGraphResult result;
    result.errors = stored.errors;
    if (!stored.contents) return result;
    result = parse_mechanic_graph(manifest, *stored.contents);
    if (result.ok() && path != mechanic_graph_document_path(
            manifest, result.asset->document.id)) {
        result.asset.reset();
        error(result.errors, ErrorCode::invalid_path, "document",
              "document filename does not match its id");
    }
    return result;
}

MechanicGraphResult publish_mechanic_graph(
    const std::filesystem::path& root, const ProjectManifest& manifest,
    const MechanicGraph& graph) {
    MechanicGraphResult result;
    const auto validation = validate_mechanic_graph(manifest, graph);
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }
    const auto path = mechanic_graph_document_path(manifest, graph.document.id);
    const auto saved = save_document_atomic(
        root, path, serialize_mechanic_graph(graph),
        [&](const std::string_view text) {
            return parse_validation(manifest, text);
        });
    if (!saved.ok()) {
        result.errors = saved.errors;
        return result;
    }
    return load_mechanic_graph(root, manifest, path);
}

} // namespace fabric::project
