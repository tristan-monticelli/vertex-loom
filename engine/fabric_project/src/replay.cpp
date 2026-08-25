#include "fabric/project/replay.hpp"

#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
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

bool frame(const Json& object, std::uint64_t& out, std::vector<Error>& errors) {
    const auto item = object.find("frame");
    if (item == object.end() || !item->is_number_unsigned()) {
        error(errors, ErrorCode::invalid_asset, "frame", "expected an unsigned integer");
        return false;
    }
    out = item->get<std::uint64_t>();
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
    auto result = parse_replay(manifest, json);
    return {.errors = std::move(result.errors)};
}
}

std::int64_t quantize_replay_position(const float value) noexcept {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(value) * replay_position_quantization));
}

float dequantize_replay_position(const std::int64_t value) noexcept {
    return static_cast<float>(static_cast<double>(value) / replay_position_quantization);
}

std::int64_t quantize_replay_rotation(const float turns) noexcept {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(turns) * replay_rotation_quantization));
}

float dequantize_replay_rotation(const std::int64_t value) noexcept {
    return static_cast<float>(static_cast<double>(value) / replay_rotation_quantization);
}

std::filesystem::path replay_document_path(const ProjectManifest& manifest,
                                            const core::ResourceId& id) {
    return manifest.directories.assets / "replays" / (id.value + ".replay.json");
}

ValidationReport validate_replay(const ProjectManifest&, const ReplayDocument& replay) {
    ValidationReport report;
    if (replay.document.schema_version != current_replay_schema_version)
        error(report.errors, ErrorCode::unsupported_schema_version, "schemaVersion",
              "only replay schema version 1 is supported");
    if (replay.document.type != "replay")
        error(report.errors, ErrorCode::invalid_asset, "type", "must be replay");
    if (!core::ResourceId::is_valid(replay.document.id.value))
        error(report.errors, ErrorCode::invalid_resource_id, "id", "must be valid");
    if (replay.document.name.empty() || replay.build.empty())
        error(report.errors, ErrorCode::invalid_asset, "document", "name and build must not be empty");
    if (replay.source_scene &&
        (!core::ResourceId::is_valid(replay.source_scene->id.value) ||
         replay.source_scene->expected_type != "scene"))
        error(report.errors, ErrorCode::resource_type_mismatch, "sourceScene", "must reference a scene");

    std::uint64_t previous_frame = 0;
    bool first = true;
    std::set<std::pair<std::uint64_t, std::string>> input_keys;
    for (const auto& input : replay.inputs) {
        if (!core::ResourceId::is_valid(input.action))
            error(report.errors, ErrorCode::invalid_resource_id, "inputs.action", "must be a valid action id");
        if (!input_keys.emplace(input.frame, input.action).second)
            error(report.errors, ErrorCode::duplicate_resource, "inputs", "frame/action pairs must be unique");
        if (!first && input.frame < previous_frame)
            error(report.errors, ErrorCode::invalid_asset, "inputs", "must be ordered by frame");
        first = false;
        previous_frame = input.frame;
    }

    first = true;
    previous_frame = 0;
    for (const auto& event : replay.events) {
        if (!core::ResourceId::is_valid(event.name))
            error(report.errors, ErrorCode::invalid_resource_id, "events.name", "must be a valid event id");
        if (!first && event.frame < previous_frame)
            error(report.errors, ErrorCode::invalid_asset, "events", "must be ordered by frame");
        first = false;
        previous_frame = event.frame;
    }

    first = true;
    previous_frame = 0;
    for (const auto& checkpoint : replay.checkpoints) {
        if (!first && checkpoint.frame <= previous_frame)
            error(report.errors, ErrorCode::invalid_asset, "checkpoints", "must be strictly ordered by frame");
        first = false;
        previous_frame = checkpoint.frame;
        std::set<std::string> nodes;
        for (const auto& state : checkpoint.states) {
            if (!core::ResourceId::is_valid(state.node_id) || !nodes.insert(state.node_id).second)
                error(report.errors, ErrorCode::duplicate_resource, "checkpoints.states", "node ids must be valid and unique");
        }
    }
    return report;
}

std::vector<ResourceReference> replay_resource_references(const ReplayDocument& replay) {
    if (!replay.source_scene) return {};
    return {*replay.source_scene};
}

std::string serialize_replay(const ReplayDocument& replay) {
    Json json = {{"schemaVersion", replay.document.schema_version},
                 {"type", replay.document.type}, {"id", replay.document.id.value},
                 {"name", replay.document.name}, {"build", replay.build},
                 {"seed", replay.seed}, {"inputs", Json::array()},
                 {"events", Json::array()}, {"checkpoints", Json::array()}};
    if (replay.source_scene) json["sourceScene"] = reference(*replay.source_scene);
    for (const auto& input : replay.inputs)
        json["inputs"].push_back({{"frame", input.frame}, {"action", input.action},
                                   {"pressed", input.pressed}, {"released", input.released}});
    for (const auto& event : replay.events)
        json["events"].push_back({{"frame", event.frame}, {"name", event.name},
                                   {"payload", event.payload}});
    for (const auto& checkpoint : replay.checkpoints) {
        Json states = Json::array();
        for (const auto& state : checkpoint.states)
            states.push_back({{"node", state.node_id}, {"x", state.x}, {"y", state.y},
                              {"rotation", state.rotation}});
        json["checkpoints"].push_back({{"frame", checkpoint.frame}, {"states", states}});
    }
    return json.dump(2) + "\n";
}

ReplayResult parse_replay(const ProjectManifest& manifest, std::string_view json_text) {
    ReplayResult result;
    Json json;
    try { json = Json::parse(json_text); }
    catch (...) { error(result.errors, ErrorCode::invalid_json, "replay", "cannot parse replay JSON"); return result; }
    if (!json.is_object()) { error(result.errors, ErrorCode::invalid_asset, "replay", "top-level value must be an object"); return result; }
    ReplayDocument replay;
    const auto schema = json.find("schemaVersion");
    if (schema == json.end() || !schema->is_number_unsigned()) error(result.errors, ErrorCode::invalid_asset, "schemaVersion", "expected an unsigned integer");
    else replay.document.schema_version = schema->get<std::uint32_t>();
    text(json, "type", replay.document.type, result.errors);
    text(json, "id", replay.document.id.value, result.errors);
    text(json, "name", replay.document.name, result.errors);
    text(json, "build", replay.build, result.errors);
    const auto seed = json.find("seed");
    if (seed == json.end() || !seed->is_number_unsigned()) error(result.errors, ErrorCode::invalid_asset, "seed", "expected an unsigned integer");
    else replay.seed = seed->get<std::uint64_t>();
    const auto source = json.find("sourceScene");
    if (source != json.end()) {
        ResourceReference value;
        if (source->is_object() && read_reference(json, "sourceScene", value, result.errors)) replay.source_scene = std::move(value);
        else if (!source->is_object()) error(result.errors, ErrorCode::invalid_asset, "sourceScene", "expected a resource reference");
    }
    const auto inputs = json.find("inputs");
    if (inputs == json.end() || !inputs->is_array()) error(result.errors, ErrorCode::invalid_asset, "inputs", "expected an array");
    else for (const auto& item : *inputs) {
        ReplayInput value;
        frame(item, value.frame, result.errors); text(item, "action", value.action, result.errors);
        const auto pressed = item.find("pressed");
        const auto released = item.find("released");
        if (pressed == item.end() || !pressed->is_boolean()) error(result.errors, ErrorCode::invalid_asset, "pressed", "expected a boolean"); else value.pressed = pressed->get<bool>();
        if (released == item.end() || !released->is_boolean()) error(result.errors, ErrorCode::invalid_asset, "released", "expected a boolean"); else value.released = released->get<bool>();
        replay.inputs.push_back(std::move(value));
    }
    const auto events = json.find("events");
    if (events == json.end() || !events->is_array()) error(result.errors, ErrorCode::invalid_asset, "events", "expected an array");
    else for (const auto& item : *events) {
        ReplayEvent value; frame(item, value.frame, result.errors); text(item, "name", value.name, result.errors); text(item, "payload", value.payload, result.errors); replay.events.push_back(std::move(value));
    }
    const auto checkpoints = json.find("checkpoints");
    if (checkpoints == json.end() || !checkpoints->is_array()) error(result.errors, ErrorCode::invalid_asset, "checkpoints", "expected an array");
    else for (const auto& item : *checkpoints) {
        ReplayCheckpoint checkpoint; frame(item, checkpoint.frame, result.errors);
        const auto states = item.find("states");
        if (states == item.end() || !states->is_array()) error(result.errors, ErrorCode::invalid_asset, "states", "expected an array");
        else for (const auto& state : *states) {
            ReplayEntityState value; text(state, "node", value.node_id, result.errors);
            const auto read_int = [&](const char* key, std::int64_t& destination) {
                const auto field = state.find(key);
                if (field == state.end() || !field->is_number_integer()) { error(result.errors, ErrorCode::invalid_asset, key, "expected an integer"); return; }
                destination = field->get<std::int64_t>();
            };
            read_int("x", value.x); read_int("y", value.y); read_int("rotation", value.rotation);
            checkpoint.states.push_back(std::move(value));
        }
        replay.checkpoints.push_back(std::move(checkpoint));
    }
    if (!result.errors.empty()) return result;
    const auto validation = validate_replay(manifest, replay);
    if (!validation.ok()) { result.errors = validation.errors; return result; }
    result.asset = std::move(replay);
    return result;
}

ReplayResult load_replay(const std::filesystem::path& root, const ProjectManifest& manifest,
                         const std::filesystem::path& path) {
    const auto stored = load_document(root, path, [&](std::string_view text_value) { return parse_validation(manifest, text_value); });
    ReplayResult result; result.errors = stored.errors;
    if (stored.contents) result = parse_replay(manifest, *stored.contents);
    if (result.ok() && path != replay_document_path(manifest, result.asset->document.id)) {
        result.asset.reset(); error(result.errors, ErrorCode::invalid_path, "document", "document filename does not match its id");
    }
    return result;
}

ReplayResult publish_replay(const std::filesystem::path& root, const ProjectManifest& manifest,
                            const ReplayDocument& replay) {
    ReplayResult result;
    const auto validation = validate_replay(manifest, replay);
    if (!validation.ok()) { result.errors = validation.errors; return result; }
    const auto path = replay_document_path(manifest, replay.document.id);
    const auto saved = save_document_atomic(root, path, serialize_replay(replay), [&](std::string_view text_value) { return parse_validation(manifest, text_value); });
    if (!saved.ok()) { result.errors = saved.errors; return result; }
    return load_replay(root, manifest, path);
}

} // namespace fabric::project
