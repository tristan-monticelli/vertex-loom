#include "fabric/project/manifest.hpp"

#include <array>
#include <utility>

namespace fabric::project {
namespace {

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

ManifestResult failure_from(ValidationReport report) {
    return ManifestResult{.errors = std::move(report.errors)};
}

} // namespace

ManifestResult create_project(const std::filesystem::path& project_root,
                              const ProjectManifest& manifest) {
    auto manifest_validation = validate_manifest(manifest);
    if (!manifest_validation.ok()) {
        return failure_from(std::move(manifest_validation));
    }

    ManifestResult result;
    std::error_code filesystem_error;
    const bool root_exists = std::filesystem::exists(project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot inspect the project destination");
        return result;
    }

    if (root_exists) {
        const bool root_is_directory = std::filesystem::is_directory(
            project_root, filesystem_error);
        if (filesystem_error) {
            add_error(result.errors, ErrorCode::io_error, "project",
                      "cannot inspect the project destination type");
            return result;
        }
        if (!root_is_directory) {
            add_error(result.errors, ErrorCode::invalid_path, "project",
                      "project destination must be a directory");
            return result;
        }
        const bool root_is_empty = std::filesystem::is_empty(
            project_root, filesystem_error);
        if (filesystem_error) {
            add_error(result.errors, ErrorCode::io_error, "project",
                      "cannot inspect the project destination contents");
            return result;
        }
        if (!root_is_empty) {
            add_error(result.errors, ErrorCode::directory_not_empty, "project",
                      "project destination must be empty");
            return result;
        }
    } else {
        std::filesystem::create_directories(project_root, filesystem_error);
        if (filesystem_error) {
            add_error(result.errors, ErrorCode::io_error, "project",
                      "cannot create the project destination");
            return result;
        }
    }

    const auto& directories = manifest.directories;
    const std::array required_directories{
        std::pair{"directories.assets", &directories.assets},
        std::pair{"directories.entities", &directories.entities},
        std::pair{"directories.maps", &directories.maps},
        std::pair{"directories.scenes", &directories.scenes},
        std::pair{"directories.schemas", &directories.schemas},
    };
    for (const auto& [field, relative_path] : required_directories) {
        filesystem_error.clear();
        std::filesystem::create_directories(project_root / *relative_path,
                                            filesystem_error);
        if (filesystem_error) {
            add_error(result.errors, ErrorCode::io_error, field,
                      "cannot create the project directory structure");
            return result;
        }
    }

    auto save_report = save_manifest_atomic(project_root, manifest);
    if (!save_report.ok()) {
        return failure_from(std::move(save_report));
    }
    return load_project(project_root);
}

} // namespace fabric::project
