#pragma once

#include "fabric/editor/map_session.hpp"
#include "fabric/editor/scene_session.hpp"
#include "fabric/project/map_package.hpp"
#include "fabric/runtime/preview_runtime.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

struct SDL_Window;

namespace fabric::map_studio {

enum class PublishRoot { map, scene };

struct PublishWorkspaceState {
    PublishRoot root{PublishRoot::map};
    std::filesystem::path destination_parent;
    std::vector<project::MapPackageResource> resources;
    std::string minimum_runtime_version;
    std::filesystem::path published_destination;
    std::vector<std::string> runtime_errors;
    runtime::PreviewRuntimeStats runtime_stats;
    bool plan_valid{};
    bool runtime_verified{};
};

struct PublishWorkspaceProbe {
    bool enabled{};
    bool validate_seen{};
    bool validate_clicked{};
    bool publish_seen{};
    bool publish_clicked{};
    bool dependency_seen{};
    bool runtime_verified{};
    ImVec2 validate_screen{};
    ImVec2 publish_screen{};
};

using PublishFolderPicker = std::function<std::optional<std::filesystem::path>(
    SDL_Window*, std::string&)>;
using PublishPrepare = std::function<bool()>;

void draw_publish_workspace(
    editor::MapSession& map_session,
    editor::SceneSession& scene_session,
    SDL_Window* window,
    PublishWorkspaceState& state,
    std::string& status,
    std::vector<project::Error>& package_errors,
    bool publication_enabled,
    std::string_view publication_disabled_reason,
    const PublishPrepare& prepare_documents,
    const PublishFolderPicker& choose_folder,
    PublishWorkspaceProbe* probe = nullptr);

} // namespace fabric::map_studio
