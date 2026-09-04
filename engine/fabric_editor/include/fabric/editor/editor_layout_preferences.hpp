#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace fabric::editor {

enum class EditorLayoutMode {
    automatic,
    compact,
    wide,
};

struct EditorLayoutPreferences {
    EditorLayoutMode mode{EditorLayoutMode::automatic};
    float primary_panel_width{280.0F};
    float secondary_panel_width{340.0F};
    float task_panel_height{260.0F};

    friend bool operator==(const EditorLayoutPreferences&,
                           const EditorLayoutPreferences&) = default;
};

[[nodiscard]] std::optional<EditorLayoutPreferences> load_layout_preferences(
    const std::filesystem::path& path, std::string* error = nullptr);

[[nodiscard]] bool save_layout_preferences(
    const std::filesystem::path& path,
    const EditorLayoutPreferences& preferences,
    std::string* error = nullptr);

} // namespace fabric::editor
