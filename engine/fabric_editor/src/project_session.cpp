#include "fabric/editor/project_session.hpp"

#include <utility>

namespace fabric::editor {

bool ProjectSession::create(const std::filesystem::path& project_root,
                            const project::ProjectManifest& manifest) {
    auto created = project::create_project(project_root, manifest);
    if (!created.ok()) {
        errors_ = std::move(created.errors);
        return false;
    }

    project_root_ = project_root;
    manifest_ = std::move(created.manifest);
    errors_.clear();
    return true;
}

bool ProjectSession::open(const std::filesystem::path& project_root) {
    auto loaded = project::load_project(project_root);
    if (!loaded.ok()) {
        errors_ = std::move(loaded.errors);
        return false;
    }

    project_root_ = project_root;
    manifest_ = std::move(loaded.manifest);
    errors_.clear();
    return true;
}

bool ProjectSession::has_project() const noexcept {
    return manifest_.has_value();
}

const std::filesystem::path& ProjectSession::project_root() const noexcept {
    return project_root_;
}

const std::optional<project::ProjectManifest>& ProjectSession::manifest() const noexcept {
    return manifest_;
}

const std::vector<project::Error>& ProjectSession::errors() const noexcept {
    return errors_;
}

} // namespace fabric::editor
