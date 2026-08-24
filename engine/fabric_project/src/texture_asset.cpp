#include "fabric/project/texture_asset.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <system_error>
#include <unistd.h>
#endif

namespace fabric::project {
namespace {

using Json = nlohmann::json;

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool is_portable_relative_path(const std::filesystem::path& path) {
    const std::string value = path.generic_string();
    if (value.empty() || value == "." || path.is_absolute() ||
        value.starts_with('/') || value.starts_with('\\') ||
        (value.size() >= 2 && value[1] == ':') ||
        value.find('\\') != std::string::npos) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool is_within(const std::filesystem::path& root,
               const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative == "." || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

std::filesystem::path temporary_path(const std::filesystem::path& destination) {
    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
    return destination.parent_path() /
           ("." + destination.filename().string() + "." +
            std::to_string(timestamp) + "." +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) +
            ".tmp");
}

bool publish_no_replace(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    if (::link(temporary.c_str(), destination.c_str()) != 0) {
        error = std::error_code(errno, std::generic_category());
        return false;
    }
    static_cast<void>(::unlink(temporary.c_str()));
    return true;
#endif
}

bool read_string(const Json& object, const char* key, std::string& destination,
                 std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_string()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected a JSON string");
        return false;
    }
    destination = iterator->get<std::string>();
    return true;
}

bool read_dimension(const Json& object, const char* key,
                    std::uint32_t& destination, std::vector<Error>& errors) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number_unsigned() ||
        iterator->get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
        add_error(errors, ErrorCode::invalid_asset, key,
                  "expected an unsigned 32-bit integer");
        return false;
    }
    destination = iterator->get<std::uint32_t>();
    return true;
}

} // namespace

std::filesystem::path texture_source_path(const ProjectManifest& manifest,
                                          const core::ResourceId& id) {
    return manifest.directories.assets / "textures" / (id.value + ".png");
}

std::filesystem::path texture_document_path(const ProjectManifest& manifest,
                                            const core::ResourceId& id) {
    return manifest.directories.assets / "textures" /
           (id.value + ".texture.json");
}

ValidationReport validate_texture_asset(const ProjectManifest& manifest,
                                        const TextureAsset& asset) {
    ValidationReport report;
    if (asset.document.schema_version != current_texture_schema_version) {
        add_error(report.errors, ErrorCode::unsupported_schema_version,
                  "schemaVersion", "only texture schema version 1 is supported");
    }
    if (asset.document.type != "texture") {
        add_error(report.errors, ErrorCode::invalid_asset, "type",
                  "must be texture");
    }
    if (!core::ResourceId::is_valid(asset.document.id.value)) {
        add_error(report.errors, ErrorCode::invalid_resource_id, "id",
                  "must be a valid resource identifier");
    }
    if (asset.document.name.empty()) {
        add_error(report.errors, ErrorCode::invalid_asset, "name",
                  "must not be empty");
    }
    if (!is_portable_relative_path(asset.source) ||
        asset.source != texture_source_path(manifest, asset.document.id)) {
        add_error(report.errors, ErrorCode::invalid_path, "source",
                  "must be the canonical project-relative texture path");
    }
    constexpr std::uint32_t maximum_dimension = 16'384;
    constexpr std::uint64_t maximum_pixels = 67'108'864;
    const auto pixels = static_cast<std::uint64_t>(asset.width) * asset.height;
    if (asset.width == 0 || asset.height == 0 ||
        asset.width > maximum_dimension || asset.height > maximum_dimension ||
        pixels > maximum_pixels) {
        add_error(report.errors, ErrorCode::invalid_asset, "dimensions",
                  "dimensions exceed the texture safety limits");
    }
    if (asset.pixel_format != "rgba8") {
        add_error(report.errors, ErrorCode::invalid_asset, "pixelFormat",
                  "only rgba8 is supported");
    }
    return report;
}

std::string serialize_texture_asset(const TextureAsset& asset) {
    const Json document = {
        {"schemaVersion", asset.document.schema_version},
        {"type", asset.document.type},
        {"id", asset.document.id.value},
        {"name", asset.document.name},
        {"source", asset.source.generic_string()},
        {"width", asset.width},
        {"height", asset.height},
        {"pixelFormat", asset.pixel_format},
    };
    return document.dump(2) + '\n';
}

TextureAssetResult parse_texture_asset(const ProjectManifest& manifest,
                                       const std::string_view json_text) {
    TextureAssetResult result;
    Json document;
    try {
        document = Json::parse(json_text);
    } catch (const Json::parse_error&) {
        add_error(result.errors, ErrorCode::invalid_json, "texture",
                  "cannot parse texture asset JSON");
        return result;
    }
    if (!document.is_object()) {
        add_error(result.errors, ErrorCode::invalid_asset, "texture",
                  "top-level value must be an object");
        return result;
    }

    TextureAsset asset;
    read_dimension(document, "schemaVersion", asset.document.schema_version,
                   result.errors);
    read_string(document, "type", asset.document.type, result.errors);
    read_string(document, "id", asset.document.id.value, result.errors);
    read_string(document, "name", asset.document.name, result.errors);
    std::string source;
    if (read_string(document, "source", source, result.errors)) {
        asset.source = source;
    }
    read_dimension(document, "width", asset.width, result.errors);
    read_dimension(document, "height", asset.height, result.errors);
    read_string(document, "pixelFormat", asset.pixel_format, result.errors);
    if (!result.errors.empty()) {
        return result;
    }
    auto validation = validate_texture_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    result.asset = std::move(asset);
    return result;
}

TextureAssetResult load_texture_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& document_path) {
    TextureAssetResult result;
    if (!is_portable_relative_path(document_path)) {
        add_error(result.errors, ErrorCode::invalid_path, "texture",
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
    if (filesystem_error || !is_within(canonical_root, canonical_document)) {
        add_error(result.errors, ErrorCode::invalid_path, "texture",
                  "texture document must resolve inside the project");
        return result;
    }
    std::ifstream input(canonical_document, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::missing_file, "texture",
                  "cannot open texture asset document");
        return result;
    }
    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    result = parse_texture_asset(manifest, contents);
    if (!result.ok()) {
        return result;
    }
    if (document_path != texture_document_path(manifest,
                                               result.asset->document.id)) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::invalid_path, "texture",
                  "document filename does not match its resource identifier");
        return result;
    }

    filesystem_error.clear();
    const auto canonical_source = std::filesystem::weakly_canonical(
        project_root / result.asset->source, filesystem_error);
    const bool source_is_file = !filesystem_error &&
        std::filesystem::is_regular_file(canonical_source, filesystem_error);
    if (filesystem_error || !is_within(canonical_root, canonical_source) ||
        !source_is_file) {
        result.asset.reset();
        add_error(result.errors, ErrorCode::missing_file, "source",
                  "texture source is missing or outside the project");
    }
    return result;
}

TextureAssetResult publish_texture_asset(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const TextureAsset& asset,
    const std::filesystem::path& validated_source) {
    TextureAssetResult result;
    auto validation = validate_texture_asset(manifest, asset);
    if (!validation.ok()) {
        result.errors = std::move(validation.errors);
        return result;
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(validated_source, filesystem_error)) {
        add_error(result.errors, ErrorCode::missing_file, "source",
                  "validated source is not a readable file");
        return result;
    }

    const auto texture_directory = project_root /
        manifest.directories.assets / "textures";
    std::filesystem::create_directories(texture_directory, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "textures",
                  "cannot create the texture directory");
        return result;
    }
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return result;
    }
    filesystem_error.clear();
    const auto canonical_directory = std::filesystem::weakly_canonical(
        texture_directory, filesystem_error);
    if (filesystem_error || !is_within(canonical_root, canonical_directory)) {
        add_error(result.errors, ErrorCode::invalid_path, "textures",
                  "texture directory must resolve inside the project");
        return result;
    }

    const auto source_destination = project_root / asset.source;
    const auto document_relative = texture_document_path(
        manifest, asset.document.id);
    const auto document_destination = project_root / document_relative;
    const bool source_exists = std::filesystem::exists(
        source_destination, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "source",
                  "cannot inspect the texture destination");
        return result;
    }
    filesystem_error.clear();
    const bool document_exists = std::filesystem::exists(
        document_destination, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "texture",
                  "cannot inspect the texture document destination");
        return result;
    }
    if (source_exists || document_exists) {
        add_error(result.errors, ErrorCode::asset_already_exists, "id",
                  "texture identifier already exists");
        return result;
    }

    const auto source_temporary = temporary_path(source_destination);
    if (!std::filesystem::copy_file(validated_source, source_temporary,
                                    std::filesystem::copy_options::none,
                                    filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(source_temporary, cleanup_error);
        add_error(result.errors, ErrorCode::io_error, "source",
                  "cannot stage the texture source");
        return result;
    }
    const auto document_temporary = temporary_path(document_destination);
    {
        std::ofstream output(document_temporary,
                             std::ios::binary | std::ios::trunc);
        output << serialize_texture_asset(asset);
        output.flush();
        if (!output) {
            std::error_code cleanup_error;
            std::filesystem::remove(source_temporary, cleanup_error);
            std::filesystem::remove(document_temporary, cleanup_error);
            add_error(result.errors, ErrorCode::io_error, "texture",
                      "cannot stage the texture document");
            return result;
        }
    }
    if (!publish_no_replace(source_temporary, source_destination,
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(source_temporary, cleanup_error);
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(result.errors, ErrorCode::io_error, "source",
                  "cannot publish the texture source");
        return result;
    }
    if (!publish_no_replace(document_temporary, document_destination,
                            filesystem_error)) {
        std::error_code cleanup_error;
        std::filesystem::remove(document_temporary, cleanup_error);
        add_error(result.errors, ErrorCode::io_error, "texture",
                  "cannot publish the texture document");
        return result;
    }
    return load_texture_asset(project_root, manifest, document_relative);
}

} // namespace fabric::project
