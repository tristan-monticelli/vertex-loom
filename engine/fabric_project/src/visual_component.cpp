#include "fabric/project/visual_component.hpp"

#include "fabric/project/document_storage.hpp"
#include "asset_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
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

std::string path(const std::string& field, const char* key) {
    return field.empty() ? std::string{key} : field + "." + key;
}

bool read_text(const Json& object, const char* key, std::string& output,
               std::vector<Error>& errors, const std::string& field = {}) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset, path(field, key),
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
        add_error(errors, ErrorCode::invalid_asset, path(field, key),
                  "expected a finite number");
        return false;
    }
    output = iterator->get<float>();
    if (!std::isfinite(output)) {
        add_error(errors, ErrorCode::invalid_asset, path(field, key),
                  "must be finite");
        return false;
    }
    return true;
}

Json serialize_vec2(const core::Vec2 value) {
    return {{"x", value.x}, {"y", value.y}};
}

bool read_vec2_value(const Json& object, core::Vec2& output,
                     std::vector<Error>& errors, const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field, "expected a Vec2");
        return false;
    }
    reject_unknown(object, {"x", "y"}, field, errors);
    return read_float(object, "x", output.x, errors, field) &&
        read_float(object, "y", output.y, errors, field);
}

bool read_vec2(const Json& object, const char* key, core::Vec2& output,
               std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        add_error(errors, ErrorCode::invalid_asset, path(field, key),
                  "expected a Vec2");
        return false;
    }
    return read_vec2_value(*iterator, output, errors, path(field, key));
}

Json serialize_color(const core::Color value) {
    return {{"red", value.red}, {"green", value.green},
            {"blue", value.blue}, {"alpha", value.alpha}};
}

bool read_color_value(const Json& object, core::Color& output,
                      std::vector<Error>& errors, const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field, "expected a color");
        return false;
    }
    reject_unknown(object, {"red", "green", "blue", "alpha"}, field,
                   errors);
    return read_float(object, "red", output.red, errors, field) &&
        read_float(object, "green", output.green, errors, field) &&
        read_float(object, "blue", output.blue, errors, field) &&
        read_float(object, "alpha", output.alpha, errors, field);
}

Json serialize_reference(const ResourceReference& reference) {
    return {{"id", reference.id.value},
            {"expectedType", reference.expected_type}};
}

bool read_reference_value(const Json& object, ResourceReference& output,
                          std::vector<Error>& errors,
                          const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a resource reference");
        return false;
    }
    reject_unknown(object, {"id", "expectedType"}, field, errors);
    return read_text(object, "id", output.id.value, errors, field) &&
        read_text(object, "expectedType", output.expected_type, errors, field);
}

bool read_reference(const Json& object, const char* key,
                    ResourceReference& output, std::vector<Error>& errors,
                    const std::string& field) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        add_error(errors, ErrorCode::invalid_asset, path(field, key),
                  "expected a resource reference");
        return false;
    }
    return read_reference_value(*iterator, output, errors, path(field, key));
}

Json serialize_rect(const core::Rect& value) {
    return {{"origin", serialize_vec2(value.origin)},
            {"size", serialize_vec2(value.size)}};
}

bool read_rect(const Json& object, const char* key, core::Rect& output,
               std::vector<Error>& errors, const std::string& field) {
    const auto iterator = object.find(key);
    const auto target = path(field, key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, target, "expected a Rect");
        return false;
    }
    reject_unknown(*iterator, {"origin", "size"}, target, errors);
    return read_vec2(*iterator, "origin", output.origin, errors, target) &&
        read_vec2(*iterator, "size", output.size, errors, target);
}

std::optional<VisualParameterType> parameter_type(
    const std::string_view type) {
    if (type == "scalar") return VisualParameterType::scalar;
    if (type == "angle") return VisualParameterType::angle;
    if (type == "integer") return VisualParameterType::integer;
    if (type == "boolean") return VisualParameterType::boolean;
    if (type == "text") return VisualParameterType::text;
    if (type == "vec2") return VisualParameterType::vec2;
    if (type == "color") return VisualParameterType::color;
    if (type == "resource") return VisualParameterType::resource;
    return std::nullopt;
}

Json serialize_parameter_value(const VisualParameterType type,
                               const VisualParameterValue& value) {
    Json result{{"type", std::string(to_string(type))}};
    switch (type) {
    case VisualParameterType::scalar:
    case VisualParameterType::angle:
        result["value"] = std::get<float>(value);
        break;
    case VisualParameterType::integer:
        result["value"] = std::get<std::int64_t>(value);
        break;
    case VisualParameterType::boolean:
        result["value"] = std::get<bool>(value);
        break;
    case VisualParameterType::text:
        result["value"] = std::get<std::string>(value);
        break;
    case VisualParameterType::vec2:
        result["value"] = serialize_vec2(std::get<core::Vec2>(value));
        break;
    case VisualParameterType::color:
        result["value"] = serialize_color(std::get<core::Color>(value));
        break;
    case VisualParameterType::resource:
        result["value"] = serialize_reference(
            std::get<ResourceReference>(value));
        break;
    }
    return result;
}

bool read_parameter_value(const Json& object, VisualParameterType& type,
                          VisualParameterValue& output,
                          std::vector<Error>& errors,
                          const std::string& field) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "expected a typed parameter value");
        return false;
    }
    reject_unknown(object, {"type", "value"}, field, errors);
    std::string type_name;
    if (!read_text(object, "type", type_name, errors, field)) return false;
    const auto parsed_type = parameter_type(type_name);
    if (!parsed_type) {
        add_error(errors, ErrorCode::invalid_asset, field + ".type",
                  "unsupported visual parameter type");
        return false;
    }
    type = *parsed_type;
    const auto value = object.find("value");
    if (value == object.end()) {
        add_error(errors, ErrorCode::invalid_asset, field + ".value",
                  "typed parameter value is missing");
        return false;
    }
    switch (type) {
    case VisualParameterType::scalar:
    case VisualParameterType::angle: {
        float parsed{};
        if (!read_float(object, "value", parsed, errors, field)) return false;
        output = parsed;
        return true;
    }
    case VisualParameterType::integer:
        if (!value->is_number_integer()) {
            add_error(errors, ErrorCode::invalid_asset, field + ".value",
                      "expected an integer");
            return false;
        }
        output = value->get<std::int64_t>();
        return true;
    case VisualParameterType::boolean:
        if (!value->is_boolean()) {
            add_error(errors, ErrorCode::invalid_asset, field + ".value",
                      "expected a boolean");
            return false;
        }
        output = value->get<bool>();
        return true;
    case VisualParameterType::text:
        if (!value->is_string()) {
            add_error(errors, ErrorCode::invalid_asset, field + ".value",
                      "expected text");
            return false;
        }
        output = value->get<std::string>();
        return true;
    case VisualParameterType::vec2: {
        core::Vec2 parsed;
        if (!read_vec2_value(*value, parsed, errors, field + ".value")) {
            return false;
        }
        output = parsed;
        return true;
    }
    case VisualParameterType::color: {
        core::Color parsed;
        if (!read_color_value(*value, parsed, errors, field + ".value")) {
            return false;
        }
        output = parsed;
        return true;
    }
    case VisualParameterType::resource: {
        ResourceReference parsed;
        if (!read_reference_value(*value, parsed, errors,
                                  field + ".value")) return false;
        output = std::move(parsed);
        return true;
    }
    }
    return false;
}

Json serialize_binding(const PropertyBinding& binding) {
    return {{"nodeId", binding.node_id},
            {"componentId", binding.component_id},
            {"propertyId", binding.property_id}};
}

bool read_binding(const Json& object, const char* key,
                  PropertyBinding& output, std::vector<Error>& errors,
                  const std::string& field) {
    const auto iterator = object.find(key);
    const auto target = path(field, key);
    if (iterator == object.end() || !iterator->is_object()) {
        add_error(errors, ErrorCode::invalid_asset, target,
                  "expected a property binding");
        return false;
    }
    reject_unknown(*iterator, {"nodeId", "componentId", "propertyId"},
                   target, errors);
    return read_text(*iterator, "nodeId", output.node_id, errors, target) &&
        read_text(*iterator, "componentId", output.component_id, errors,
                  target) &&
        read_text(*iterator, "propertyId", output.property_id, errors,
                  target);
}

Json serialize_override(const VisualParameterOverride& value,
                        const VisualParameterType type) {
    return {{"parameterId", value.parameter_id},
            {"value", serialize_parameter_value(type, value.value)}};
}

bool finite(const core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

const VisualComponentParameter* find_parameter(
    const VisualComponent& component, const std::string_view id) {
    const auto iterator = std::ranges::find_if(
        component.parameters, [&](const auto& parameter) {
            return parameter.id == id;
        });
    return iterator == component.parameters.end() ? nullptr : &*iterator;
}

bool validate_parameter_value(const VisualParameterType type,
                              const VisualParameterValue& value,
                              const std::string& field,
                              std::vector<Error>& errors) {
    if (!visual_parameter_value_matches(type, value)) {
        add_error(errors, ErrorCode::resource_type_mismatch, field,
                  "value does not match the declared parameter type");
        return false;
    }
    if (const auto* scalar = std::get_if<float>(&value);
        scalar != nullptr && !std::isfinite(*scalar)) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "numeric value must be finite");
        return false;
    }
    if (const auto* vec2 = std::get_if<core::Vec2>(&value);
        vec2 != nullptr && !finite(*vec2)) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "Vec2 value must be finite");
        return false;
    }
    if (const auto* color = std::get_if<core::Color>(&value);
        color != nullptr &&
        (!std::isfinite(color->red) || !std::isfinite(color->green) ||
         !std::isfinite(color->blue) || !std::isfinite(color->alpha) ||
         color->red < 0.0F || color->red > 1.0F ||
         color->green < 0.0F || color->green > 1.0F ||
         color->blue < 0.0F || color->blue > 1.0F ||
         color->alpha < 0.0F || color->alpha > 1.0F)) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "color channels must be finite in [0,1]");
        return false;
    }
    if (const auto* reference = std::get_if<ResourceReference>(&value);
        reference != nullptr &&
        (!core::ResourceId::is_valid(reference->id.value) ||
         reference->expected_type.empty())) {
        add_error(errors, ErrorCode::invalid_asset, field,
                  "resource value must be valid and typed");
        return false;
    }
    return true;
}

void validate_overrides(const VisualComponent& component,
                        const std::vector<VisualParameterOverride>& overrides,
                        const std::string& field, std::vector<Error>& errors) {
    std::set<std::string> ids;
    for (std::size_t index = 0; index < overrides.size(); ++index) {
        const auto& override = overrides[index];
        const auto target = field + "[" + std::to_string(index) + "]";
        if (!ids.insert(override.parameter_id).second) {
            add_error(errors, ErrorCode::duplicate_resource,
                      target + ".parameterId",
                      "parameter override must be unique");
        }
        const auto* parameter = find_parameter(component,
                                                override.parameter_id);
        if (parameter == nullptr) {
            add_error(errors, ErrorCode::missing_resource,
                      target + ".parameterId",
                      "parameter override is not declared by the component");
        } else {
            validate_parameter_value(parameter->type, override.value,
                                     target + ".value", errors);
        }
    }
}

PropertyValueKind property_kind(const VisualParameterType type) {
    switch (type) {
    case VisualParameterType::scalar: return PropertyValueKind::scalar;
    case VisualParameterType::integer: return PropertyValueKind::integer;
    case VisualParameterType::angle: return PropertyValueKind::angle;
    case VisualParameterType::boolean: return PropertyValueKind::boolean;
    case VisualParameterType::text: return PropertyValueKind::text;
    case VisualParameterType::vec2: return PropertyValueKind::vec2;
    case VisualParameterType::color: return PropertyValueKind::color;
    case VisualParameterType::resource: return PropertyValueKind::resource;
    }
    return PropertyValueKind::scalar;
}

ValidationReport parse_validation(const ProjectManifest& manifest,
                                  const std::string_view contents) {
    auto parsed = parse_visual_component(manifest, contents);
    return {.errors = std::move(parsed.errors)};
}

} // namespace

std::string_view to_string(const VisualParameterType type) noexcept {
    switch (type) {
    case VisualParameterType::scalar: return "scalar";
    case VisualParameterType::angle: return "angle";
    case VisualParameterType::integer: return "integer";
    case VisualParameterType::boolean: return "boolean";
    case VisualParameterType::text: return "text";
    case VisualParameterType::vec2: return "vec2";
    case VisualParameterType::color: return "color";
    case VisualParameterType::resource: return "resource";
    }
    return "scalar";
}

bool visual_parameter_value_matches(
    const VisualParameterType type,
    const VisualParameterValue& value) noexcept {
    switch (type) {
    case VisualParameterType::scalar:
    case VisualParameterType::angle: return std::holds_alternative<float>(value);
    case VisualParameterType::integer:
        return std::holds_alternative<std::int64_t>(value);
    case VisualParameterType::boolean: return std::holds_alternative<bool>(value);
    case VisualParameterType::text:
        return std::holds_alternative<std::string>(value);
    case VisualParameterType::vec2: return std::holds_alternative<core::Vec2>(value);
    case VisualParameterType::color:
        return std::holds_alternative<core::Color>(value);
    case VisualParameterType::resource:
        return std::holds_alternative<ResourceReference>(value);
    }
    return false;
}

std::filesystem::path visual_component_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "components" /
        (id.value + ".component.json");
}

ValidationReport validate_visual_component(
    const ProjectManifest&, const VisualComponent& component) {
    ValidationReport report;
    if (component.document.schema_version !=
        current_visual_component_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion",
                  "only visual component schema version 1 is supported");
    }
    if (component.document.type != "visualComponent") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be visualComponent");
    }
    if (!core::ResourceId::is_valid(component.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (component.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (!core::ResourceId::is_valid(component.composition.id.value) ||
        component.composition.expected_type != "visualComposition") {
        add_error(report.errors, ErrorCode::resource_type_mismatch,
                  "composition", "must reference a visualComposition");
    }
    if (!finite(component.bounds.origin) || !finite(component.bounds.size) ||
        component.bounds.size.x <= 0.0F || component.bounds.size.y <= 0.0F) {
        add_error(report.errors, ErrorCode::invalid_asset, "bounds",
                  "must be finite with a positive size");
    }
    std::set<std::string> anchor_ids;
    for (std::size_t index = 0; index < component.anchors.size(); ++index) {
        const auto& anchor = component.anchors[index];
        const auto field = "anchors[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(anchor.id)) {
            add_error(report.errors, ErrorCode::invalid_resource_id,
                      field + ".id", "must be a valid stable identifier");
        } else if (!anchor_ids.insert(anchor.id).second) {
            add_error(report.errors, ErrorCode::duplicate_resource,
                      field + ".id", "anchor identifier must be unique");
        }
        if (anchor.name.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".name",
                      "must not be empty");
        }
        if (!finite(anchor.position)) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".position", "must be finite");
        }
    }
    std::set<std::string> parameter_ids;
    for (std::size_t index = 0; index < component.parameters.size(); ++index) {
        const auto& parameter = component.parameters[index];
        const auto field = "parameters[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(parameter.id)) {
            add_error(report.errors, ErrorCode::invalid_resource_id,
                      field + ".id", "must be a valid stable identifier");
        } else if (!parameter_ids.insert(parameter.id).second) {
            add_error(report.errors, ErrorCode::duplicate_resource,
                      field + ".id", "parameter identifier must be unique");
        }
        if (parameter.name.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".name",
                      "must not be empty");
        }
        validate_parameter_value(parameter.type, parameter.default_value,
                                 field + ".default", report.errors);
        if (parameter.target.node_id.empty() ||
            parameter.target.component_id.empty() ||
            parameter.target.property_id.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".target", "binding fields must not be empty");
        }
        if (parameter.animatable &&
            (parameter.type == VisualParameterType::integer ||
             parameter.type == VisualParameterType::text)) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".animatable",
                      "integer and text parameters are not animatable in v1");
        }
    }
    std::set<std::string> variant_ids;
    for (std::size_t index = 0; index < component.variants.size(); ++index) {
        const auto& variant = component.variants[index];
        const auto field = "variants[" + std::to_string(index) + "]";
        if (!core::ResourceId::is_valid(variant.id)) {
            add_error(report.errors, ErrorCode::invalid_resource_id,
                      field + ".id", "must be a valid stable identifier");
        } else if (!variant_ids.insert(variant.id).second) {
            add_error(report.errors, ErrorCode::duplicate_resource,
                      field + ".id", "variant identifier must be unique");
        }
        if (variant.name.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".name",
                      "must not be empty");
        }
        validate_overrides(component, variant.overrides,
                           field + ".overrides", report.errors);
    }
    return report;
}

std::vector<ResourceReference> visual_component_resource_references(
    const VisualComponent& component) {
    std::vector<ResourceReference> references{component.composition};
    const auto collect = [&](const VisualParameterValue& value) {
        if (const auto* reference = std::get_if<ResourceReference>(&value)) {
            references.push_back(*reference);
        }
    };
    for (const auto& parameter : component.parameters) {
        collect(parameter.default_value);
    }
    for (const auto& variant : component.variants) {
        for (const auto& override : variant.overrides) collect(override.value);
    }
    return references;
}

std::vector<PropertyDescriptor> visual_component_property_descriptors(
    const VisualComponent& component) {
    std::vector<PropertyDescriptor> descriptors;
    descriptors.reserve(component.parameters.size());
    for (const auto& parameter : component.parameters) {
        descriptors.push_back({
            .component_id = component.document.id.value,
            .property_id = parameter.id,
            .display_path = component.document.name + "/" + parameter.name,
            .value_kind = property_kind(parameter.type),
            .readable = true,
            .writable = true,
            .animatable = parameter.animatable,
        });
    }
    return descriptors;
}

VisualComponentInstanceResult resolve_visual_component_instance(
    const VisualComponent& component, const VisualComponentInstance& instance) {
    VisualComponentInstanceResult result;
    const auto validation = validate_visual_component(ProjectManifest{}, component);
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }
    result.parameters.reserve(component.parameters.size());
    for (const auto& parameter : component.parameters) {
        result.parameters.push_back({parameter.id, parameter.default_value,
                                     parameter.target, parameter.animatable});
    }
    if (instance.anchor_id && !std::ranges::any_of(
            component.anchors, [&](const auto& anchor) {
                return anchor.id == *instance.anchor_id;
            })) {
        add_error(result.errors, ErrorCode::missing_resource, "anchorId",
                  "component instance anchor is not declared");
    }
    const VisualComponentVariant* variant = nullptr;
    if (instance.variant_id) {
        const auto iterator = std::ranges::find_if(
            component.variants, [&](const auto& candidate) {
                return candidate.id == *instance.variant_id;
            });
        if (iterator == component.variants.end()) {
            add_error(result.errors, ErrorCode::missing_resource, "variantId",
                      "component instance variant is not declared");
        } else {
            variant = &*iterator;
        }
    }
    validate_overrides(component, instance.overrides, "overrides",
                       result.errors);
    const auto apply = [&](const std::vector<VisualParameterOverride>& overrides) {
        for (const auto& override : overrides) {
            const auto resolved = std::ranges::find_if(
                result.parameters, [&](const auto& parameter) {
                    return parameter.id == override.parameter_id;
                });
            if (resolved != result.parameters.end() &&
                visual_parameter_value_matches(
                    find_parameter(component, override.parameter_id)->type,
                    override.value)) {
                resolved->value = override.value;
            }
        }
    };
    if (variant != nullptr) apply(variant->overrides);
    apply(instance.overrides);
    return result;
}

std::string serialize_visual_component(const VisualComponent& component) {
    Json document{{"schemaVersion", component.document.schema_version},
                  {"type", component.document.type},
                  {"id", component.document.id.value},
                  {"name", component.document.name},
                  {"composition", serialize_reference(component.composition)},
                  {"bounds", serialize_rect(component.bounds)},
                  {"anchors", Json::array()},
                  {"parameters", Json::array()},
                  {"variants", Json::array()}};
    for (const auto& anchor : component.anchors) {
        document["anchors"].push_back({{"id", anchor.id},
                                        {"name", anchor.name},
                                        {"position", serialize_vec2(anchor.position)}});
    }
    for (const auto& parameter : component.parameters) {
        document["parameters"].push_back({
            {"id", parameter.id},
            {"name", parameter.name},
            {"type", std::string(to_string(parameter.type))},
            {"default", serialize_parameter_value(parameter.type,
                                                   parameter.default_value)},
            {"target", serialize_binding(parameter.target)},
            {"animatable", parameter.animatable},
        });
    }
    for (const auto& variant : component.variants) {
        Json serialized{{"id", variant.id}, {"name", variant.name},
                        {"overrides", Json::array()}};
        for (const auto& override : variant.overrides) {
            const auto* parameter = find_parameter(component,
                                                    override.parameter_id);
            if (parameter != nullptr) {
                serialized["overrides"].push_back(
                    serialize_override(override, parameter->type));
            }
        }
        document["variants"].push_back(std::move(serialized));
    }
    return document.dump(2) + "\n";
}

VisualComponentResult parse_visual_component(
    const ProjectManifest& manifest, const std::string_view contents) {
    VisualComponentResult result;
    Json document;
    try {
        document = Json::parse(contents);
    } catch (...) {
        add_error(result.errors, ErrorCode::invalid_json, "visualComponent",
                  "cannot parse visual component JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "visualComponent",
                  "top-level value must be an object");
        return result;
    }
    reject_unknown(document, {"schemaVersion", "type", "id", "name",
                              "composition", "bounds", "anchors",
                              "parameters", "variants"},
                   "visualComponent", result.errors);
    VisualComponent component;
    const auto schema = document.find("schemaVersion");
    if (schema == document.end() || !schema->is_number_unsigned()) {
        add_error(result.errors, ErrorCode::invalid_asset, "schemaVersion",
                  "expected an unsigned integer");
    } else {
        component.document.schema_version = schema->get<std::uint32_t>();
    }
    read_text(document, "type", component.document.type, result.errors);
    read_text(document, "id", component.document.id.value, result.errors);
    read_text(document, "name", component.document.name, result.errors);
    read_reference(document, "composition", component.composition,
                   result.errors, "");
    read_rect(document, "bounds", component.bounds, result.errors, "");

    const auto anchors = document.find("anchors");
    if (anchors == document.end() || !anchors->is_array()) {
        add_error(result.errors, ErrorCode::invalid_asset, "anchors",
                  "expected an array");
    } else {
        for (std::size_t index = 0; index < anchors->size(); ++index) {
            const auto& object = (*anchors)[index];
            const auto field = "anchors[" + std::to_string(index) + "]";
            if (!object.is_object()) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "expected an object");
                continue;
            }
            reject_unknown(object, {"id", "name", "position"}, field,
                           result.errors);
            VisualComponentAnchor anchor;
            read_text(object, "id", anchor.id, result.errors, field);
            read_text(object, "name", anchor.name, result.errors, field);
            read_vec2(object, "position", anchor.position, result.errors,
                      field);
            component.anchors.push_back(std::move(anchor));
        }
    }
    const auto parameters = document.find("parameters");
    if (parameters == document.end() || !parameters->is_array()) {
        add_error(result.errors, ErrorCode::invalid_asset, "parameters",
                  "expected an array");
    } else {
        for (std::size_t index = 0; index < parameters->size(); ++index) {
            const auto& object = (*parameters)[index];
            const auto field = "parameters[" + std::to_string(index) + "]";
            if (!object.is_object()) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "expected an object");
                continue;
            }
            reject_unknown(object, {"id", "name", "type", "default",
                                    "target", "animatable"}, field,
                           result.errors);
            VisualComponentParameter parameter;
            std::string declared_type;
            read_text(object, "id", parameter.id, result.errors, field);
            read_text(object, "name", parameter.name, result.errors, field);
            read_text(object, "type", declared_type, result.errors, field);
            const auto parsed_type = parameter_type(declared_type);
            if (!parsed_type) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".type",
                          "unsupported visual parameter type");
            } else {
                parameter.type = *parsed_type;
            }
            const auto default_value = object.find("default");
            VisualParameterType value_type{VisualParameterType::scalar};
            if (default_value == object.end()) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".default", "default value is required");
            } else if (read_parameter_value(*default_value, value_type,
                                            parameter.default_value,
                                            result.errors,
                                            field + ".default") &&
                       parsed_type && value_type != *parsed_type) {
                add_error(result.errors, ErrorCode::resource_type_mismatch,
                          field + ".default",
                          "typed value does not match parameter type");
            }
            read_binding(object, "target", parameter.target, result.errors,
                         field);
            const auto animatable = object.find("animatable");
            if (animatable == object.end() || !animatable->is_boolean()) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".animatable", "expected a boolean");
            } else {
                parameter.animatable = animatable->get<bool>();
            }
            component.parameters.push_back(std::move(parameter));
        }
    }
    const auto variants = document.find("variants");
    if (variants == document.end() || !variants->is_array()) {
        add_error(result.errors, ErrorCode::invalid_asset, "variants",
                  "expected an array");
    } else {
        for (std::size_t index = 0; index < variants->size(); ++index) {
            const auto& object = (*variants)[index];
            const auto field = "variants[" + std::to_string(index) + "]";
            if (!object.is_object()) {
                add_error(result.errors, ErrorCode::invalid_asset, field,
                          "expected an object");
                continue;
            }
            reject_unknown(object, {"id", "name", "overrides"}, field,
                           result.errors);
            VisualComponentVariant variant;
            read_text(object, "id", variant.id, result.errors, field);
            read_text(object, "name", variant.name, result.errors, field);
            const auto overrides = object.find("overrides");
            if (overrides == object.end() || !overrides->is_array()) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".overrides", "expected an array");
            } else {
                for (std::size_t value_index = 0;
                     value_index < overrides->size(); ++value_index) {
                    const auto& value_object = (*overrides)[value_index];
                    const auto value_field = field + ".overrides[" +
                        std::to_string(value_index) + "]";
                    if (!value_object.is_object()) {
                        add_error(result.errors, ErrorCode::invalid_asset,
                                  value_field, "expected an object");
                        continue;
                    }
                    reject_unknown(value_object, {"parameterId", "value"},
                                   value_field, result.errors);
                    VisualParameterOverride override;
                    read_text(value_object, "parameterId",
                              override.parameter_id, result.errors,
                              value_field);
                    const auto typed_value = value_object.find("value");
                    VisualParameterType ignored_type{};
                    if (typed_value == value_object.end()) {
                        add_error(result.errors, ErrorCode::invalid_asset,
                                  value_field + ".value",
                                  "override value is required");
                    } else {
                        if (read_parameter_value(*typed_value, ignored_type,
                                                 override.value, result.errors,
                                                 value_field + ".value")) {
                            const auto* declaration = find_parameter(
                                component, override.parameter_id);
                            if (declaration != nullptr &&
                                ignored_type != declaration->type) {
                                add_error(result.errors,
                                          ErrorCode::resource_type_mismatch,
                                          value_field + ".value",
                                          "typed override does not match parameter type");
                            }
                        }
                    }
                    variant.overrides.push_back(std::move(override));
                }
            }
            component.variants.push_back(std::move(variant));
        }
    }
    if (!result.errors.empty()) return result;
    auto validation = validate_visual_component(manifest, component);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(component);
    return result;
}

VisualComponentResult load_visual_component(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const std::filesystem::path& path_value) {
    auto stored = load_document(project_root, path_value,
        [&](const std::string_view contents) {
            return parse_validation(manifest, contents);
        });
    VisualComponentResult result;
    result.errors = std::move(stored.errors);
    if (stored.contents) result = parse_visual_component(manifest, *stored.contents);
    if (result.ok() && path_value != visual_component_document_path(
            manifest, result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "document",
                  "document filename does not match its id");
    }
    return result;
}

VisualComponentResult publish_visual_component(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, const VisualComponent& component) {
    VisualComponentResult result;
    auto validation = validate_visual_component(manifest, component);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto document_path = visual_component_document_path(
        manifest, component.document.id);
    auto saved = save_document_atomic(
        project_root, document_path, serialize_visual_component(component),
        [&](const std::string_view contents) {
            return parse_validation(manifest, contents);
        });
    if (!saved.ok()) {
        result.errors = std::move(saved.errors);
        return result;
    }
    return load_visual_component(project_root, manifest, document_path);
}

} // namespace fabric::project
