#pragma once

#include "fabric/editor/command_stack.hpp"
#include "fabric/project/map.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace fabric::editor {

class MapSession {
public:
    [[nodiscard]] bool create(const std::filesystem::path& project_root,
                              const project::MapDocument& map);
    [[nodiscard]] bool open(const std::filesystem::path& project_root,
                            const core::ResourceId& map_id);
    [[nodiscard]] bool save();
    [[nodiscard]] bool place_instance(project::MapInstance instance);
    [[nodiscard]] bool remove_instance(const core::ResourceId& instance_id);
    [[nodiscard]] bool set_instance_transform(const core::ResourceId& instance_id,
                                               core::Transform transform);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool has_map() const noexcept { return map_.has_value(); }
    [[nodiscard]] bool dirty() const noexcept { return commands_.dirty(); }
    [[nodiscard]] bool can_undo() const noexcept { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return commands_.can_redo(); }
    [[nodiscard]] const std::optional<project::MapDocument>& map() const noexcept { return map_; }
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept { return errors_; }

private:
    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<project::MapDocument> map_;
    CommandStack commands_;
    std::vector<project::Error> errors_;
};

} // namespace fabric::editor
