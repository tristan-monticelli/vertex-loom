#pragma once

#include "fabric/editor/project_session.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace fabric::map_studio {

void draw_resource_picker(
    const char* label,
    const std::filesystem::path& directory,
    std::string_view suffix,
    std::string& selected_id,
    editor::ProjectSession* catalog = nullptr);

} // namespace fabric::map_studio
