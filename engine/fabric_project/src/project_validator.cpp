#include "fabric/project/manifest.hpp"
#include "fabric/project/texture_asset.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <utility>

namespace fabric::project {
namespace {

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool is_within_project(const std::filesystem::path& canonical_root,
                       const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(canonical_root);
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

} // namespace

ManifestResult load_manifest(const std::filesystem::path& project_root) {
    ManifestResult result;
    const auto manifest_path = project_root / "project.json";
    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::missing_file, "project.json",
                  "cannot open project manifest");
        return result;
    }

    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        add_error(result.errors, ErrorCode::io_error, "project.json",
                  "failed while reading project manifest");
        return result;
    }
    return parse_manifest(contents);
}

ManifestResult load_project(const std::filesystem::path& project_root) {
    ManifestResult result;
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(project_root, filesystem_error)) {
        add_error(result.errors, ErrorCode::missing_directory, "project",
                  "project root is not an accessible directory");
        return result;
    }

    ManifestResult loaded = load_manifest(project_root);
    if (!loaded.ok()) {
        return loaded;
    }

    filesystem_error.clear();
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return result;
    }

    const auto& directories = loaded.manifest->directories;
    const std::array required_directories{
        std::pair{"directories.assets", &directories.assets},
        std::pair{"directories.entities", &directories.entities},
        std::pair{"directories.maps", &directories.maps},
        std::pair{"directories.scenes", &directories.scenes},
        std::pair{"directories.schemas", &directories.schemas},
    };
    for (const auto& [field, relative_path] : required_directories) {
        filesystem_error.clear();
        if (!std::filesystem::is_directory(project_root / *relative_path,
                                           filesystem_error)) {
            add_error(result.errors, ErrorCode::missing_directory, field,
                      "required project directory is missing or inaccessible");
            continue;
        }
        filesystem_error.clear();
        const auto canonical_directory = std::filesystem::weakly_canonical(
            project_root / *relative_path, filesystem_error);
        if (filesystem_error ||
            !is_within_project(canonical_root, canonical_directory)) {
            add_error(result.errors, ErrorCode::invalid_path, field,
                      "resolved directory must remain inside the project root");
        }
    }
    if (!result.errors.empty()) {
        return result;
    }

    const auto texture_directory = project_root /
        loaded.manifest->directories.assets / "textures";
    filesystem_error.clear();
    if (std::filesystem::exists(texture_directory, filesystem_error)) {
        const auto canonical_textures = std::filesystem::weakly_canonical(
            texture_directory, filesystem_error);
        if (filesystem_error ||
            !is_within_project(canonical_root, canonical_textures)) {
            add_error(result.errors, ErrorCode::invalid_path,
                      "assets.textures",
                      "texture directory must remain inside the project root");
            return result;
        }
        for (std::filesystem::directory_iterator iterator(
                 texture_directory, filesystem_error), end;
             !filesystem_error && iterator != end; iterator.increment(filesystem_error)) {
            const auto& entry = *iterator;
            const std::string filename = entry.path().filename().string();
            if (!entry.is_regular_file(filesystem_error) ||
                !filename.ends_with(".texture.json")) {
                continue;
            }
            auto loaded_texture = load_texture_asset(
                project_root, *loaded.manifest,
                entry.path().lexically_relative(project_root));
            result.errors.insert(result.errors.end(),
                                 std::make_move_iterator(loaded_texture.errors.begin()),
                                 std::make_move_iterator(loaded_texture.errors.end()));
        }
        if (filesystem_error) {
            add_error(result.errors, ErrorCode::io_error, "assets.textures",
                      "cannot inspect texture assets");
        }
    } else if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "assets.textures",
                  "cannot inspect the texture directory");
    }
    if (result.errors.empty()) {
        result.manifest = std::move(loaded.manifest);
    }
    return result;
}

ValidationReport validate_project(const std::filesystem::path& project_root) {
    auto loaded = load_project(project_root);
    return ValidationReport{.errors = std::move(loaded.errors)};
}

} // namespace fabric::project
