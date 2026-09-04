#pragma once

#include "fabric/editor/project_session.hpp"
#include "fabric/editor/scene_session.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace fabric::map_studio {

struct SceneWorkspaceState {
    std::string new_id;
    std::string new_name;
    std::string open_id;
    std::string edited_name;
    std::string map_id;
    std::string mount_id;
    std::string selected_map_mount_id;
    std::string transition_id;
    std::string target_scene_id;
    std::string entry_point;
    std::string event_id;
    std::string selected_transition_id;
    std::string remove_map_request_id;
    std::string remove_transition_request_id;
};

using SceneFolderPicker = std::function<std::optional<std::filesystem::path>(
    SDL_Window*, std::string&)>;

void draw_scene_workspace(
    editor::SceneSession& session,
    const std::filesystem::path& project_root,
    SDL_Window* window,
    SceneWorkspaceState& state,
    std::string& status,
    std::vector<project::Error>& package_errors,
    editor::ProjectSession& resource_catalog,
    const SceneFolderPicker& choose_folder);

} // namespace fabric::map_studio
