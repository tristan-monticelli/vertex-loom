#pragma once

#include "fabric/project/manifest.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace fabric::editor {

class ProjectSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path& project_root,
                              const project::ProjectManifest& manifest);
    [[nodiscard]] bool open(const std::filesystem::path& project_root);

    [[nodiscard]] bool has_project() const noexcept;
    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;
    [[nodiscard]] const std::optional<project::ProjectManifest>& manifest() const noexcept;
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept;

private:
    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
