#include "fabric/project/input.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

bool read_text(const Json& object, const char* key, std::string& value,
               std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto found = object.find(key);
    const std::string field = prefix.empty() ? key : std::string(prefix) + "." + key;
    if (found == object.end() || !found->is_string()) {
        error(errors, ErrorCode::invalid_asset, field, "expected a string");
        return false;
    }
    value = found->get<std::string>();
    return true;
}

bool read_int(const Json& object, const char* key, int& value,
              std::vector<Error>& errors, const std::string_view prefix = {}) {
    const auto found = object.find(key);
    const std::string field = prefix.empty() ? key : std::string(prefix) + "." + key;
    if (found == object.end() || !found->is_number_integer()) {
        error(errors, ErrorCode::invalid_asset, field, "expected an integer");
        return false;
    }
    const auto parsed = found->get<std::int64_t>();
    if (parsed < 0 || parsed > std::numeric_limits<int>::max()) {
        error(errors, ErrorCode::invalid_asset, field, "must be a non-negative int");
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

std::string_view device_name(const InputDevice device) noexcept {
    return device == InputDevice::keyboard ? "keyboard" : "gamepad";
}

std::optional<InputDevice> parse_device(const std::string_view value) {
    if (value == "keyboard") return InputDevice::keyboard;
    if (value == "gamepad") return InputDevice::gamepad;
    return std::nullopt;
}

std::string_view kind_name(const InputBindingKind kind) noexcept {
    return kind == InputBindingKind::axis ? "axis" : "button";
}

std::optional<InputBindingKind> parse_kind(const std::string_view value) {
    if (value == "button") return InputBindingKind::button;
    if (value == "axis") return InputBindingKind::axis;
    return std::nullopt;
}

ValidationReport parse_validation(const ProjectManifest&, const std::string_view text) {
    const auto parsed = parse_input(text);
    return {.errors = parsed.errors};
}

} // namespace

std::filesystem::path input_document_path(const ProjectManifest& manifest,
                                          const core::ResourceId& id) {
    return manifest.directories.assets / "input" / (id.value + ".input.json");
}

ValidationReport validate_input(const ProjectManifest&, const InputDocument& input) {
    ValidationReport report;
    if (input.document.schema_version != current_input_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version, "schemaVersion",
              "only input schema version 2 is supported");
    if (input.document.type != "input")
        error(report.errors, ErrorCode::invalid_asset, "type", "must be input");
    if (!core::ResourceId::is_valid(input.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (input.document.name.empty())
        error(report.errors, ErrorCode::invalid_asset, "name", "must not be empty");

    std::vector<std::string> action_ids;
    for (std::size_t action_index = 0; action_index < input.actions.size(); ++action_index) {
        const auto& action = input.actions[action_index];
        const auto field = "actions[" + std::to_string(action_index) + "]";
        if (!core::ResourceId::is_valid(action.id))
            error(report.errors, ErrorCode::invalid_resource_id, field + ".id", "must be valid");
        if (std::find(action_ids.begin(), action_ids.end(), action.id) != action_ids.end())
            error(report.errors, ErrorCode::duplicate_resource, field + ".id", "action is duplicated");
        action_ids.push_back(action.id);
        std::vector<InputBinding> bindings;
        for (std::size_t binding_index = 0; binding_index < action.bindings.size(); ++binding_index) {
            const auto& binding = action.bindings[binding_index];
            if (binding.code < 0)
                error(report.errors, ErrorCode::invalid_asset,
                      field + ".bindings[" + std::to_string(binding_index) + "].code",
                      "must be non-negative");
            if (binding.threshold < 0.0F || binding.threshold > 1.0F)
                error(report.errors, ErrorCode::invalid_asset, field + ".bindings[" + std::to_string(binding_index) + "].threshold", "must be in [0,1]");
            if (binding.dead_zone < 0.0F || binding.dead_zone >= 1.0F)
                error(report.errors, ErrorCode::invalid_asset, field + ".bindings[" + std::to_string(binding_index) + "].deadZone", "must be in [0,1)");
            if (std::find(bindings.begin(), bindings.end(), binding) != bindings.end())
                error(report.errors, ErrorCode::invalid_asset,
                      field + ".bindings[" + std::to_string(binding_index) + "]",
                      "binding is duplicated");
            bindings.push_back(binding);
        }
    }
    return report;
}

InputResult parse_input(const std::string_view serialized) {
    InputResult result;
    Json json;
    try {
        json = Json::parse(serialized);
    } catch (...) {
        error(result.errors, ErrorCode::invalid_json, "input", "cannot parse input JSON");
        return result;
    }
    if (!json.is_object()) {
        error(result.errors, ErrorCode::invalid_asset, "input", "top-level value must be an object");
        return result;
    }

    InputDocument input;
    if (const auto found = json.find("schemaVersion"); found != json.end() && found->is_number_unsigned())
        input.document.schema_version = found->get<std::uint32_t>();
    else error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected an unsigned integer");
    if (!read_text(json, "type", input.document.type, result.errors)) return result;
    if (!read_text(json, "id", input.document.id.value, result.errors)) return result;
    if (!read_text(json, "name", input.document.name, result.errors)) return result;
    if (input.document.schema_version == 1)
        input.document.schema_version = current_input_schema_version;

    const auto actions = json.find("actions");
    if (actions == json.end() || !actions->is_array()) {
        error(result.errors, ErrorCode::invalid_asset, "actions", "expected an array");
    } else {
        for (std::size_t i = 0; i < actions->size(); ++i) {
            const auto& item = (*actions)[i];
            const auto field = "actions[" + std::to_string(i) + "]";
            if (!item.is_object()) {
                error(result.errors, ErrorCode::invalid_asset, field, "expected an object");
                continue;
            }
            InputActionDefinition action;
            read_text(item, "id", action.id, result.errors, field);
            const auto bindings = item.find("bindings");
            if (bindings == item.end() || !bindings->is_array()) {
                error(result.errors, ErrorCode::invalid_asset, field + ".bindings", "expected an array");
            } else {
                for (std::size_t j = 0; j < bindings->size(); ++j) {
                    const auto& binding_json = (*bindings)[j];
                    const auto binding_field = field + ".bindings[" + std::to_string(j) + "]";
                    if (!binding_json.is_object()) {
                        error(result.errors, ErrorCode::invalid_asset, binding_field, "expected an object");
                        continue;
                    }
                    std::string device;
                    int code{};
                    const bool valid_device = read_text(binding_json, "device", device, result.errors, binding_field);
                    const bool valid_code = read_int(binding_json, "code", code, result.errors, binding_field);
                    if (valid_device && valid_code) {
                        const auto parsed_device = parse_device(device);
                        if (!parsed_device) error(result.errors, ErrorCode::invalid_asset, binding_field + ".device", "unsupported input device");
                        else {
                            InputBinding binding{*parsed_device, code};
                            if (const auto found_kind = binding_json.find("kind"); found_kind != binding_json.end() && found_kind->is_string()) {
                                if (const auto parsed_kind = parse_kind(found_kind->get<std::string>()); parsed_kind)
                                    binding.kind = *parsed_kind;
                            }
                            if (const auto found = binding_json.find("threshold"); found != binding_json.end() && found->is_number()) binding.threshold = found->get<float>();
                            if (const auto found = binding_json.find("deadZone"); found != binding_json.end() && found->is_number()) binding.dead_zone = found->get<float>();
                            if (const auto found = binding_json.find("modifiers"); found != binding_json.end() && found->is_object()) {
                                if (const auto value = found->find("ctrl"); value != found->end() && value->is_boolean()) binding.ctrl = value->get<bool>();
                                if (const auto value = found->find("shift"); value != found->end() && value->is_boolean()) binding.shift = value->get<bool>();
                                if (const auto value = found->find("alt"); value != found->end() && value->is_boolean()) binding.alt = value->get<bool>();
                                if (const auto value = found->find("super"); value != found->end() && value->is_boolean()) binding.super = value->get<bool>();
                            }
                            action.bindings.push_back(binding);
                        }
                    }
                }
            }
            input.actions.push_back(std::move(action));
        }
    }
    const auto validation = validate_input(ProjectManifest{}, input);
    result.errors.insert(result.errors.end(), validation.errors.begin(), validation.errors.end());
    if (result.errors.empty()) result.input = std::move(input);
    return result;
}

std::string serialize_input(const InputDocument& input) {
    Json json = {{"schemaVersion", input.document.schema_version},
                 {"type", input.document.type},
                 {"id", input.document.id.value},
                 {"name", input.document.name},
                 {"actions", Json::array()}};
    for (const auto& action : input.actions) {
        Json item = {{"id", action.id}, {"bindings", Json::array()}};
        for (const auto& binding : action.bindings)
            item["bindings"].push_back({{"device", device_name(binding.device)}, {"code", binding.code}, {"kind", kind_name(binding.kind)}, {"threshold", binding.threshold}, {"deadZone", binding.dead_zone}, {"modifiers", {{"ctrl", binding.ctrl}, {"shift", binding.shift}, {"alt", binding.alt}, {"super", binding.super}}}});
        json["actions"].push_back(std::move(item));
    }
    return json.dump(2) + "\n";
}

InputResult load_input(const std::filesystem::path& root, const ProjectManifest& manifest,
                       const std::filesystem::path& path) {
    const auto stored = load_document(root, path,
        [&](const std::string_view text) { return parse_validation(manifest, text); });
    InputResult result;
    result.errors = std::move(stored.errors);
    if (!stored.contents) return result;
    result = parse_input(*stored.contents);
    if (result.ok() && path != input_document_path(manifest, result.input->document.id)) {
        result.input.reset();
        error(result.errors, ErrorCode::invalid_path, "document", "document filename does not match its id");
    }
    return result;
}

InputResult publish_input(const std::filesystem::path& root, const ProjectManifest& manifest,
                          const InputDocument& input) {
    InputResult result;
    const auto validation = validate_input(manifest, input);
    if (!validation.ok()) { result.errors = validation.errors; return result; }
    const auto path = input_document_path(manifest, input.document.id);
    const auto saved = save_document_atomic(root, path, serialize_input(input),
        [&](const std::string_view text) { return parse_validation(manifest, text); });
    if (!saved.ok()) { result.errors = saved.errors; return result; }
    return load_input(root, manifest, path);
}

} // namespace fabric::project
