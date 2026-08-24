#include "fabric/project/sprite_sheet.hpp"

#include "asset_storage.hpp"
#include "fabric/project/document_storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool has_exact_keys(const Json& object,
                    const std::initializer_list<std::string_view> required,
                    const std::initializer_list<std::string_view> optional,
                    const std::string_view field,
                    std::vector<Error>& errors) {
    if (!object.is_object()) {
        add_error(errors, ErrorCode::invalid_asset, std::string(field),
                  "expected a JSON object");
        return false;
    }
    std::set<std::string_view> allowed;
    allowed.insert(required.begin(), required.end());
    allowed.insert(optional.begin(), optional.end());
    bool valid = true;
    for (const auto key : required) {
        if (!object.contains(std::string(key))) {
            add_error(errors, ErrorCode::invalid_asset,
                      std::string(field) + "." + std::string(key),
                      "required field is missing");
            valid = false;
        }
    }
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            add_error(errors, ErrorCode::invalid_asset,
                      std::string(field) + "." + iterator.key(),
                      "unknown field is not allowed");
            valid = false;
        }
    }
    return valid;
}

bool read_string(const Json& object, const char* key, std::string& destination,
                 const std::string_view field, std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset,
                  std::string(field) + "." + key,
                  "expected a JSON string");
        return false;
    }
    destination = iterator->get<std::string>();
    return true;
}

bool read_u32(const Json& object, const char* key, std::uint32_t& destination,
              const std::string_view field, std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number_unsigned() ||
        iterator->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
        add_error(errors, ErrorCode::invalid_asset,
                  std::string(field) + "." + key,
                  "expected an unsigned 32-bit integer");
        return false;
    }
    destination = iterator->get<std::uint32_t>();
    return true;
}

bool read_i32(const Json& object, const char* key, std::int32_t& destination,
              const std::string_view field, std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number_integer()) {
        add_error(errors, ErrorCode::invalid_asset,
                  std::string(field) + "." + key,
                  "expected a signed 32-bit integer");
        return false;
    }
    if (iterator->is_number_unsigned()) {
        const auto value = iterator->get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max())) {
            add_error(errors, ErrorCode::invalid_asset,
                      std::string(field) + "." + key,
                      "signed integer exceeds 32-bit range");
            return false;
        }
        destination = static_cast<std::int32_t>(value);
        return true;
    }
    const auto value = iterator->get<std::int64_t>();
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
        add_error(errors, ErrorCode::invalid_asset,
                  std::string(field) + "." + key,
                  "signed integer exceeds 32-bit range");
        return false;
    }
    destination = static_cast<std::int32_t>(value);
    return true;
}

Json rect_json(const SpriteRect& value) {
    return {{"x", value.x},
            {"y", value.y},
            {"width", value.width},
            {"height", value.height}};
}

Json size_json(const SpriteSize& value) {
    return {{"width", value.width}, {"height", value.height}};
}

Json point_json(const SpritePoint& value) {
    return {{"x", value.x}, {"y", value.y}};
}

Json slice_rect_json(const SpriteSliceRect& value) {
    return {{"x", value.x},
            {"y", value.y},
            {"width", value.width},
            {"height", value.height}};
}

bool parse_rect(const Json& value, SpriteRect& result,
                const std::string_view field, std::vector<Error>& errors) {
    if (!has_exact_keys(value, {"x", "y", "width", "height"}, {}, field,
                        errors)) {
        return false;
    }
    const bool x = read_u32(value, "x", result.x, field, errors);
    const bool y = read_u32(value, "y", result.y, field, errors);
    const bool width = read_u32(value, "width", result.width, field, errors);
    const bool height = read_u32(value, "height", result.height, field, errors);
    return x && y && width && height;
}

bool parse_size(const Json& value, SpriteSize& result,
                const std::string_view field, std::vector<Error>& errors) {
    if (!has_exact_keys(value, {"width", "height"}, {}, field, errors)) {
        return false;
    }
    return read_u32(value, "width", result.width, field, errors) &&
        read_u32(value, "height", result.height, field, errors);
}

bool parse_point(const Json& value, SpritePoint& result,
                 const std::string_view field, std::vector<Error>& errors) {
    if (!has_exact_keys(value, {"x", "y"}, {}, field, errors)) {
        return false;
    }
    return read_i32(value, "x", result.x, field, errors) &&
        read_i32(value, "y", result.y, field, errors);
}

bool parse_slice_rect(const Json& value, SpriteSliceRect& result,
                      const std::string_view field,
                      std::vector<Error>& errors) {
    if (!has_exact_keys(value, {"x", "y", "width", "height"}, {}, field,
                        errors)) {
        return false;
    }
    const bool x = read_i32(value, "x", result.x, field, errors);
    const bool y = read_i32(value, "y", result.y, field, errors);
    const bool width = read_u32(value, "width", result.width, field, errors);
    const bool height = read_u32(value, "height", result.height, field, errors);
    return x && y && width && height;
}

std::string_view source_kind_name(const SpriteSourceKind kind) {
    return kind == SpriteSourceKind::aseprite ? "aseprite" : "png";
}

std::optional<SpriteSourceKind> parse_source_kind(const std::string& value) {
    if (value == "aseprite") {
        return SpriteSourceKind::aseprite;
    }
    if (value == "png") {
        return SpriteSourceKind::png;
    }
    return std::nullopt;
}

bool rect_fits(const SpriteRect& value, const SpriteSize& size,
               const bool allow_empty = false) {
    return (allow_empty || (value.width > 0 && value.height > 0)) &&
        value.x <= size.width && value.y <= size.height &&
        value.width <= size.width - value.x &&
        value.height <= size.height - value.y;
}

std::uint32_t read_big_endian_u32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
        (static_cast<std::uint32_t>(bytes[1]) << 16U) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        static_cast<std::uint32_t>(bytes[3]);
}

ValidationReport validate_atlas_header(const std::filesystem::path& path,
                                       const SpriteSize& expected) {
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, 24> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    constexpr std::array<std::uint8_t, 8> signature{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    ValidationReport report;
    if (input.gcount() != static_cast<std::streamsize>(header.size()) ||
        !std::equal(signature.begin(), signature.end(), header.begin()) ||
        std::memcmp(header.data() + 12, "IHDR", 4) != 0) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlas",
                  "atlas does not contain a complete PNG IHDR");
        return report;
    }
    if (read_big_endian_u32(header.data() + 16) != expected.width ||
        read_big_endian_u32(header.data() + 20) != expected.height) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlasSize",
                  "atlas PNG dimensions differ from the document");
    }
    return report;
}

ValidationReport validate_atlas_bytes(const std::string_view contents,
                                      const SpriteSize& expected) {
    ValidationReport report;
    constexpr std::array<std::uint8_t, 8> signature{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if (contents.size() < 24) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlas",
                  "atlas does not contain a complete PNG IHDR");
        return report;
    }
    const auto* header = reinterpret_cast<const std::uint8_t*>(contents.data());
    if (!std::equal(signature.begin(), signature.end(), header) ||
        std::memcmp(header + 12, "IHDR", 4) != 0) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlas",
                  "atlas does not contain a complete PNG IHDR");
        return report;
    }
    if (read_big_endian_u32(header + 16) != expected.width ||
        read_big_endian_u32(header + 20) != expected.height) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlasSize",
                  "atlas PNG dimensions differ from the document");
    }
    return report;
}

ValidationReport validate_serialized_sprite_sheet(
    const ProjectManifest& manifest, const std::string_view contents) {
    auto parsed = parse_sprite_sheet(manifest, contents);
    return {.errors = std::move(parsed.errors)};
}

} // namespace

std::filesystem::path sprite_sheet_source_path(
    const ProjectManifest& manifest, const core::ResourceId& id,
    const SpriteSourceKind kind) {
    return manifest.directories.assets / "textures" /
        (id.value +
         (kind == SpriteSourceKind::aseprite ? ".aseprite" : ".source.png"));
}

std::filesystem::path sprite_sheet_atlas_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "textures" /
        (id.value + ".atlas.png");
}

std::filesystem::path sprite_sheet_document_path(
    const ProjectManifest& manifest, const core::ResourceId& id) {
    return manifest.directories.assets / "textures" /
        (id.value + ".sprite.json");
}

ValidationReport validate_sprite_sheet(
    const ProjectManifest& manifest, const SpriteSheetDefinition& definition) {
    ValidationReport report;
    if (definition.document.schema_version !=
        current_sprite_sheet_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion",
                  "only sprite sheet schema version 1 is supported");
    }
    if (definition.document.type != "spriteSheet") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be spriteSheet");
    }
    if (!core::ResourceId::is_valid(definition.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (definition.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (!detail::is_portable_relative_path(definition.source) ||
        definition.source != sprite_sheet_source_path(
                                 manifest, definition.document.id,
                                 definition.source_kind)) {
        add_error(report.errors, ErrorCode::invalid_path, "source",
                  "must be the canonical sprite source path");
    }
    if (!detail::is_portable_relative_path(definition.atlas) ||
        definition.atlas !=
            sprite_sheet_atlas_path(manifest, definition.document.id)) {
        add_error(report.errors, ErrorCode::invalid_path, "atlas",
                  "must be the canonical sprite atlas path");
    }
    constexpr std::uint32_t maximum_dimension = 16'384;
    constexpr std::uint64_t maximum_pixels = 67'108'864;
    const std::uint64_t atlas_pixels =
        static_cast<std::uint64_t>(definition.atlas_size.width) *
        definition.atlas_size.height;
    if (definition.atlas_size.width == 0 ||
        definition.atlas_size.height == 0 ||
        definition.atlas_size.width > maximum_dimension ||
        definition.atlas_size.height > maximum_dimension ||
        atlas_pixels > maximum_pixels) {
        add_error(report.errors, ErrorCode::invalid_asset, "atlasSize",
                  "atlas dimensions exceed the raster safety limits");
    }
    if (definition.padding != 1 || definition.extrusion != 1) {
        add_error(report.errors, ErrorCode::invalid_asset, "packing",
                  "version 1 requires one pixel of padding and extrusion");
    }
    if (definition.frames.empty() || definition.frames.size() > 65'535) {
        add_error(report.errors, ErrorCode::invalid_asset, "frames",
                  "must contain between 1 and 65535 frames");
    }
    std::set<std::string> frame_names;
    for (std::size_t index = 0; index < definition.frames.size(); ++index) {
        const auto& frame = definition.frames[index];
        const std::string field = "frames[" + std::to_string(index) + "]";
        if (frame.name.empty() || !frame_names.insert(frame.name).second) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".name",
                      "frame name must be non-empty and unique");
        }
        if (!rect_fits(frame.atlas_bounds, definition.atlas_size)) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".atlasBounds",
                      "frame rectangle must fit inside the atlas");
        }
        const std::uint64_t source_pixels =
            static_cast<std::uint64_t>(frame.source_size.width) *
            frame.source_size.height;
        if (frame.source_size.width == 0 || frame.source_size.height == 0 ||
            frame.source_size.width > maximum_dimension ||
            frame.source_size.height > maximum_dimension ||
            source_pixels > maximum_pixels ||
            !rect_fits(frame.source_bounds, frame.source_size)) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".sourceBounds",
                      "source rectangle and size are invalid");
        }
        if (frame.duration_ms == 0) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".durationMs", "duration must be positive");
        }
        if (definition.source_kind == SpriteSourceKind::png) {
            if (!frame.input_bounds.has_value() ||
                frame.input_bounds->width == 0 ||
                frame.input_bounds->height == 0 ||
                frame.input_bounds->width > maximum_dimension ||
                frame.input_bounds->height > maximum_dimension) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          field + ".inputBounds",
                          "PNG frames require a bounded input rectangle");
            }
        } else if (frame.input_bounds.has_value()) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".inputBounds",
                      "Aseprite frames must not declare PNG input bounds");
        }
    }
    std::set<std::string> tag_names;
    constexpr std::array directions{"forward", "reverse", "pingPong",
                                    "pingPongReverse"};
    for (std::size_t index = 0; index < definition.tags.size(); ++index) {
        const auto& tag = definition.tags[index];
        const std::string field = "tags[" + std::to_string(index) + "]";
        if (tag.name.empty() || !tag_names.insert(tag.name).second) {
            add_error(report.errors, ErrorCode::invalid_asset, field + ".name",
                      "tag name must be non-empty and unique");
        }
        if (tag.from_frame > tag.to_frame ||
            tag.to_frame >= definition.frames.size()) {
            add_error(report.errors, ErrorCode::invalid_asset, field,
                      "tag range must reference existing frames");
        }
        if (std::ranges::find(directions, tag.direction) == directions.end()) {
            add_error(report.errors, ErrorCode::invalid_asset,
                      field + ".direction", "tag direction is invalid");
        }
    }
    std::set<std::string> slice_names;
    for (std::size_t index = 0; index < definition.slices.size(); ++index) {
        const auto& slice = definition.slices[index];
        const std::string field = "slices[" + std::to_string(index) + "]";
        if (slice.name.empty() || !slice_names.insert(slice.name).second ||
            slice.keys.empty()) {
            add_error(report.errors, ErrorCode::invalid_asset, field,
                      "slice name must be unique and keys must not be empty");
        }
        std::uint32_t previous = 0;
        bool first = true;
        for (const auto& key : slice.keys) {
            if (key.frame >= definition.frames.size() ||
                (!first && key.frame <= previous)) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          field + ".keys",
                          "slice frame keys must be valid and increasing");
                break;
            }
            if (key.bounds.width > maximum_dimension ||
                key.bounds.height > maximum_dimension ||
                (key.center.has_value() &&
                 (key.center->width > maximum_dimension ||
                  key.center->height > maximum_dimension))) {
                add_error(report.errors, ErrorCode::invalid_asset,
                          field + ".keys",
                          "slice bounds exceed the raster safety limits");
                break;
            }
            previous = key.frame;
            first = false;
        }
    }
    return report;
}

std::string serialize_sprite_sheet(const SpriteSheetDefinition& definition) {
    Json frames = Json::array();
    for (const auto& frame : definition.frames) {
        Json value{{"name", frame.name},
                   {"atlasBounds", rect_json(frame.atlas_bounds)},
                   {"sourceBounds", rect_json(frame.source_bounds)},
                   {"sourceSize", size_json(frame.source_size)},
                   {"durationMs", frame.duration_ms}};
        if (frame.pivot.has_value()) {
            value["pivot"] = point_json(*frame.pivot);
        }
        if (frame.input_bounds.has_value()) {
            value["inputBounds"] = rect_json(*frame.input_bounds);
        }
        frames.push_back(std::move(value));
    }
    Json tags = Json::array();
    for (const auto& tag : definition.tags) {
        tags.push_back({{"name", tag.name},
                        {"fromFrame", tag.from_frame},
                        {"toFrame", tag.to_frame},
                        {"direction", tag.direction},
                        {"repeat", tag.repeat}});
    }
    Json slices = Json::array();
    for (const auto& slice : definition.slices) {
        Json keys = Json::array();
        for (const auto& key : slice.keys) {
            Json value{{"frame", key.frame},
                       {"bounds", slice_rect_json(key.bounds)}};
            if (key.center.has_value()) {
                value["center"] = slice_rect_json(*key.center);
            }
            if (key.pivot.has_value()) {
                value["pivot"] = point_json(*key.pivot);
            }
            keys.push_back(std::move(value));
        }
        slices.push_back({{"name", slice.name}, {"keys", std::move(keys)}});
    }
    const Json document{
        {"schemaVersion", definition.document.schema_version},
        {"type", definition.document.type},
        {"id", definition.document.id.value},
        {"name", definition.document.name},
        {"sourceKind", source_kind_name(definition.source_kind)},
        {"source", definition.source.generic_string()},
        {"atlas", definition.atlas.generic_string()},
        {"atlasSize", size_json(definition.atlas_size)},
        {"padding", definition.padding},
        {"extrusion", definition.extrusion},
        {"frames", std::move(frames)},
        {"tags", std::move(tags)},
        {"slices", std::move(slices)},
    };
    return document.dump(2) + '\n';
}

SpriteSheetResult parse_sprite_sheet(const ProjectManifest& manifest,
                                     const std::string_view json_text) {
    SpriteSheetResult result;
    Json document;
    try {
        document = Json::parse(json_text);
    } catch (const Json::parse_error&) {
        add_error(result.errors, ErrorCode::invalid_json, "spriteSheet",
                  "cannot parse sprite sheet JSON");
        return result;
    }
    if (!has_exact_keys(
            document,
            {"schemaVersion", "type", "id", "name", "sourceKind", "source",
             "atlas", "atlasSize", "padding", "extrusion", "frames", "tags",
             "slices"},
            {}, "spriteSheet", result.errors)) {
        return result;
    }
    SpriteSheetDefinition definition;
    read_u32(document, "schemaVersion", definition.document.schema_version,
             "spriteSheet", result.errors);
    read_string(document, "type", definition.document.type, "spriteSheet",
                result.errors);
    read_string(document, "id", definition.document.id.value, "spriteSheet",
                result.errors);
    read_string(document, "name", definition.document.name, "spriteSheet",
                result.errors);
    std::string source_kind;
    if (read_string(document, "sourceKind", source_kind, "spriteSheet",
                    result.errors)) {
        const auto parsed_kind = parse_source_kind(source_kind);
        if (!parsed_kind.has_value()) {
            add_error(result.errors, ErrorCode::invalid_asset,
                      "spriteSheet.sourceKind", "must be aseprite or png");
        } else {
            definition.source_kind = *parsed_kind;
        }
    }
    std::string source;
    if (read_string(document, "source", source, "spriteSheet", result.errors)) {
        definition.source = source;
    }
    std::string atlas;
    if (read_string(document, "atlas", atlas, "spriteSheet", result.errors)) {
        definition.atlas = atlas;
    }
    if (document.contains("atlasSize")) {
        parse_size(document["atlasSize"], definition.atlas_size,
                   "spriteSheet.atlasSize", result.errors);
    }
    read_u32(document, "padding", definition.padding, "spriteSheet",
             result.errors);
    read_u32(document, "extrusion", definition.extrusion, "spriteSheet",
             result.errors);

    const auto frames = document.find("frames");
    if (frames == document.end() || !frames->is_array() ||
        frames->size() > 65'535) {
        add_error(result.errors, ErrorCode::invalid_asset,
                  "spriteSheet.frames", "expected at most 65535 frame objects");
    } else {
        definition.frames.reserve(frames->size());
        for (std::size_t index = 0; index < frames->size(); ++index) {
            const Json& value = (*frames)[index];
            const std::string field =
                "spriteSheet.frames[" + std::to_string(index) + "]";
            if (!has_exact_keys(
                    value,
                    {"name", "atlasBounds", "sourceBounds", "sourceSize",
                     "durationMs"},
                    {"pivot", "inputBounds"}, field, result.errors)) {
                continue;
            }
            SpriteFrameDefinition frame;
            read_string(value, "name", frame.name, field, result.errors);
            parse_rect(value["atlasBounds"], frame.atlas_bounds,
                       field + ".atlasBounds", result.errors);
            parse_rect(value["sourceBounds"], frame.source_bounds,
                       field + ".sourceBounds", result.errors);
            parse_size(value["sourceSize"], frame.source_size,
                       field + ".sourceSize", result.errors);
            read_u32(value, "durationMs", frame.duration_ms, field,
                     result.errors);
            if (value.contains("pivot")) {
                SpritePoint pivot;
                if (parse_point(value["pivot"], pivot, field + ".pivot",
                                result.errors)) {
                    frame.pivot = pivot;
                }
            }
            if (value.contains("inputBounds")) {
                SpriteRect input_bounds;
                if (parse_rect(value["inputBounds"], input_bounds,
                               field + ".inputBounds", result.errors)) {
                    frame.input_bounds = input_bounds;
                }
            }
            definition.frames.push_back(std::move(frame));
        }
    }

    const auto tags = document.find("tags");
    if (tags == document.end() || !tags->is_array() || tags->size() > 65'535) {
        add_error(result.errors, ErrorCode::invalid_asset, "spriteSheet.tags",
                  "expected an array of tag objects");
    } else {
        definition.tags.reserve(tags->size());
        for (std::size_t index = 0; index < tags->size(); ++index) {
            const Json& value = (*tags)[index];
            const std::string field =
                "spriteSheet.tags[" + std::to_string(index) + "]";
            if (!has_exact_keys(value,
                                {"name", "fromFrame", "toFrame", "direction",
                                 "repeat"},
                                {}, field, result.errors)) {
                continue;
            }
            SpriteTagDefinition tag;
            read_string(value, "name", tag.name, field, result.errors);
            read_u32(value, "fromFrame", tag.from_frame, field, result.errors);
            read_u32(value, "toFrame", tag.to_frame, field, result.errors);
            read_string(value, "direction", tag.direction, field,
                        result.errors);
            read_u32(value, "repeat", tag.repeat, field, result.errors);
            definition.tags.push_back(std::move(tag));
        }
    }

    const auto slices = document.find("slices");
    if (slices == document.end() || !slices->is_array() ||
        slices->size() > 65'535) {
        add_error(result.errors, ErrorCode::invalid_asset,
                  "spriteSheet.slices", "expected an array of slice objects");
    } else {
        definition.slices.reserve(slices->size());
        for (std::size_t index = 0; index < slices->size(); ++index) {
            const Json& value = (*slices)[index];
            const std::string field =
                "spriteSheet.slices[" + std::to_string(index) + "]";
            if (!has_exact_keys(value, {"name", "keys"}, {}, field,
                                result.errors)) {
                continue;
            }
            SpriteSliceDefinition slice;
            read_string(value, "name", slice.name, field, result.errors);
            if (!value["keys"].is_array() ||
                value["keys"].size() > 65'535) {
                add_error(result.errors, ErrorCode::invalid_asset,
                          field + ".keys", "expected an array of slice keys");
            } else {
                for (std::size_t key_index = 0;
                     key_index < value["keys"].size(); ++key_index) {
                    const Json& key_value = value["keys"][key_index];
                    const std::string key_field = field + ".keys[" +
                        std::to_string(key_index) + "]";
                    if (!has_exact_keys(key_value, {"frame", "bounds"},
                                        {"center", "pivot"}, key_field,
                                        result.errors)) {
                        continue;
                    }
                    SpriteSliceKeyDefinition key;
                    read_u32(key_value, "frame", key.frame, key_field,
                             result.errors);
                    parse_slice_rect(key_value["bounds"], key.bounds,
                                     key_field + ".bounds", result.errors);
                    if (key_value.contains("center")) {
                        SpriteSliceRect center;
                        if (parse_slice_rect(key_value["center"], center,
                                             key_field + ".center",
                                             result.errors)) {
                            key.center = center;
                        }
                    }
                    if (key_value.contains("pivot")) {
                        SpritePoint pivot;
                        if (parse_point(key_value["pivot"], pivot,
                                        key_field + ".pivot", result.errors)) {
                            key.pivot = pivot;
                        }
                    }
                    slice.keys.push_back(std::move(key));
                }
            }
            definition.slices.push_back(std::move(slice));
        }
    }
    if (!result.errors.empty()) {
        return result;
    }
    auto validation = validate_sprite_sheet(manifest, definition);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(definition);
    return result;
}

SpriteSheetResult load_sprite_sheet(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path) {
    SpriteSheetResult result;
    if (!detail::is_portable_relative_path(document_path)) {
        add_error(result.errors, ErrorCode::invalid_path, "spriteSheet",
                  "document path must be project-relative");
        return result;
    }
    std::error_code filesystem_error;
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return result;
    }
    filesystem_error.clear();
    const auto canonical_document = std::filesystem::weakly_canonical(
        project_root / document_path, filesystem_error);
    if (filesystem_error ||
        !detail::is_within(canonical_root, canonical_document)) {
        add_error(result.errors, ErrorCode::invalid_path, "spriteSheet",
                  "sprite sheet document must resolve inside the project");
        return result;
    }
    const auto document_size =
        std::filesystem::file_size(canonical_document, filesystem_error);
    if (filesystem_error || document_size > 256U * 1024U * 1024U) {
        add_error(result.errors, ErrorCode::io_error, "spriteSheet",
                  "sprite sheet document is inaccessible or too large");
        return result;
    }
    std::ifstream input(canonical_document, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    if (!input && !input.eof()) {
        add_error(result.errors, ErrorCode::io_error, "spriteSheet",
                  "cannot read sprite sheet document");
        return result;
    }
    result = parse_sprite_sheet(manifest, contents);
    if (!result.ok()) {
        return result;
    }
    if (document_path !=
        sprite_sheet_document_path(manifest, result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "spriteSheet",
                  "document filename does not match its resource identifier");
        return result;
    }
    const std::array resources{
        std::pair{"source", result.asset->source},
        std::pair{"atlas", result.asset->atlas},
    };
    std::filesystem::path canonical_atlas;
    for (const auto& [field, relative] : resources) {
        filesystem_error.clear();
        const auto canonical = std::filesystem::weakly_canonical(
            project_root / relative, filesystem_error);
        const bool is_file = !filesystem_error &&
            std::filesystem::is_regular_file(canonical, filesystem_error);
        if (filesystem_error || !is_file ||
            !detail::is_within(canonical_root, canonical)) {
            result.asset.reset();
            add_error(result.errors, ErrorCode::missing_file, field,
                      std::string(field) +
                          " is missing or outside the project");
            return result;
        }
        if (std::string_view(field) == "atlas") {
            canonical_atlas = canonical;
        }
    }
    auto atlas_validation = validate_atlas_header(
        canonical_atlas, result.asset->atlas_size);
    if (!atlas_validation.ok()) {
        result.asset.reset();
        result.errors = std::move(atlas_validation.errors);
    }
    return result;
}

SpriteSheetResult publish_sprite_sheet(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const SpriteSheetDefinition& definition,
    const std::filesystem::path& validated_source,
    const std::span<const std::uint8_t> atlas_png) {
    SpriteSheetResult result;
    auto validation = validate_sprite_sheet(manifest, definition);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    auto publication = detail::publish_asset_bundle(
        project_root, manifest.directories.assets / "textures",
        definition.source, definition.atlas,
        sprite_sheet_document_path(manifest, definition.document.id),
        validated_source, atlas_png, serialize_sprite_sheet(definition),
        "sprite sheet");
    if (!publication.ok()) {
        result.errors = std::move(publication.errors);
        return result;
    }
    return load_sprite_sheet(
        project_root, manifest,
        sprite_sheet_document_path(manifest, definition.document.id));
}

SpriteSheetResult regenerate_sprite_sheet(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const SpriteSheetDefinition& definition,
    const std::span<const std::uint8_t> atlas_png) {
    SpriteSheetResult result;
    auto validation = validate_sprite_sheet(manifest, definition);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    const auto document_path =
        sprite_sheet_document_path(manifest, definition.document.id);
    auto current = load_sprite_sheet(project_root, manifest, document_path);
    if (!current.ok()) {
        return current;
    }
    if (current.asset->source != definition.source ||
        current.asset->source_kind != definition.source_kind) {
        add_error(result.errors, ErrorCode::invalid_asset, "source",
                  "regeneration cannot change the preserved source");
        return result;
    }
    const std::string atlas_contents(
        reinterpret_cast<const char*>(atlas_png.data()), atlas_png.size());
    auto atlas_validation =
        validate_atlas_bytes(atlas_contents, definition.atlas_size);
    if (!atlas_validation.ok()) {
        result.errors = std::move(atlas_validation.errors);
        return result;
    }

    std::error_code filesystem_error;
    const auto old_atlas_size = std::filesystem::file_size(
        project_root / current.asset->atlas, filesystem_error);
    if (filesystem_error || old_atlas_size > 256U * 1024U * 1024U) {
        add_error(result.errors, ErrorCode::io_error, "atlas",
                  "current atlas is inaccessible or too large for rollback");
        return result;
    }
    std::ifstream old_atlas_input(project_root / current.asset->atlas,
                                  std::ios::binary);
    const std::string old_atlas{
        std::istreambuf_iterator<char>{old_atlas_input},
        std::istreambuf_iterator<char>{}};
    if (!old_atlas_input && !old_atlas_input.eof()) {
        add_error(result.errors, ErrorCode::io_error, "atlas",
                  "cannot preserve the current atlas for rollback");
        return result;
    }
    auto atlas_save = save_document_atomic(
        project_root, definition.atlas, atlas_contents,
        [&definition](const std::string_view contents) {
            return validate_atlas_bytes(contents, definition.atlas_size);
        });
    if (!atlas_save.ok()) {
        result.errors = std::move(atlas_save.errors);
        return result;
    }

    const std::string serialized = serialize_sprite_sheet(definition);
    auto document_save = save_document_atomic(
        project_root, document_path, serialized,
        [&manifest](const std::string_view contents) {
            return validate_serialized_sprite_sheet(manifest, contents);
        });
    if (!document_save.ok()) {
        auto rollback = save_document_atomic(
            project_root, current.asset->atlas, old_atlas,
            [&current](const std::string_view contents) {
                return validate_atlas_bytes(contents,
                                            current.asset->atlas_size);
            });
        result.errors = std::move(document_save.errors);
        result.errors.insert(result.errors.end(),
                             std::make_move_iterator(rollback.errors.begin()),
                             std::make_move_iterator(rollback.errors.end()));
        return result;
    }
    return load_sprite_sheet(project_root, manifest, document_path);
}

} // namespace fabric::project
