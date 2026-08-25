#pragma once

#include "fabric/project/map.hpp"
#include "fabric/project/scene.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::runtime {

class SceneRuntimeSession {
public:
    [[nodiscard]] bool load(const std::filesystem::path& project_root,
                            const core::ResourceId& scene_id);
    [[nodiscard]] bool transition(std::string_view transition_id);

    [[nodiscard]] const std::optional<project::SceneDocument>& scene() const noexcept {
        return scene_;
    }
    [[nodiscard]] const std::optional<project::MapDocument>& map() const noexcept {
        return map_;
    }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return errors_; }

private:
    [[nodiscard]] bool load_scene(const core::ResourceId& scene_id);
    [[nodiscard]] bool stage_scene(const core::ResourceId& scene_id,
                                   std::optional<project::SceneDocument>& scene,
                                   std::optional<project::MapDocument>& map,
                                   std::vector<std::string>& errors) const;

    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::SceneDocument> scene_;
    std::optional<project::MapDocument> map_;
    std::vector<std::string> errors_;
};

} // namespace fabric::runtime
