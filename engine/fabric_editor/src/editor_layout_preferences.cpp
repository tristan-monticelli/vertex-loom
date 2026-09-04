#include "fabric/editor/editor_layout_preferences.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fabric::editor {
namespace {

constexpr std::uintmax_t maximum_preferences_bytes = 65'536;

std::string_view to_string(const EditorLayoutMode mode) noexcept {
    switch (mode) {
    case EditorLayoutMode::automatic: return "automatic";
    case EditorLayoutMode::compact: return "compact";
    case EditorLayoutMode::wide: return "wide";
    }
    return "automatic";
}

std::optional<EditorLayoutMode> layout_mode(const std::string_view value) {
    if (value == "automatic") return EditorLayoutMode::automatic;
    if (value == "compact") return EditorLayoutMode::compact;
    if (value == "wide") return EditorLayoutMode::wide;
    return std::nullopt;
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::filesystem::path temporary_path(
    const std::filesystem::path& destination) {
    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
    return destination.parent_path() /
        ("." + destination.filename().string() + "." +
         std::to_string(timestamp) + "." +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) +
         ".tmp");
}

bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

} // namespace

std::optional<EditorLayoutPreferences> load_layout_preferences(
    const std::filesystem::path& path, std::string* error) {
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(path, filesystem_error)) {
        if (filesystem_error) {
            set_error(error, "Cannot inspect editor layout preferences.");
        }
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size > maximum_preferences_bytes) {
        set_error(error, "Editor layout preferences exceed the safety limit.");
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    const auto document = nlohmann::json::parse(input, nullptr, false);
    if (!input || document.is_discarded() || !document.is_object() ||
        document.value("schemaVersion", 0) != 1) {
        set_error(error, "Editor layout preferences are invalid.");
        return std::nullopt;
    }
    try {
        const auto mode = layout_mode(document.value("mode", std::string{}));
        if (!mode.has_value()) {
            set_error(error, "Editor layout mode is invalid.");
            return std::nullopt;
        }

        EditorLayoutPreferences result;
        result.mode = *mode;
        result.primary_panel_width = std::clamp(
            document.value("primaryPanelWidth", result.primary_panel_width),
            180.0F, 1'200.0F);
        result.secondary_panel_width = std::clamp(
            document.value("secondaryPanelWidth",
                           result.secondary_panel_width),
            180.0F, 1'200.0F);
        result.task_panel_height = std::clamp(
            document.value("taskPanelHeight", result.task_panel_height),
            120.0F, 900.0F);
        return result;
    } catch (const nlohmann::json::exception&) {
        set_error(error, "Editor layout preferences have invalid field types.");
        return std::nullopt;
    }
}

bool save_layout_preferences(
    const std::filesystem::path& path,
    const EditorLayoutPreferences& preferences,
    std::string* error) {
    if (path.empty() || path.filename().empty()) {
        set_error(error, "Editor layout preference path is empty.");
        return false;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        set_error(error, "Cannot create editor preference directory.");
        return false;
    }

    const nlohmann::json document = {
        {"schemaVersion", 1},
        {"mode", to_string(preferences.mode)},
        {"primaryPanelWidth", preferences.primary_panel_width},
        {"secondaryPanelWidth", preferences.secondary_panel_width},
        {"taskPanelHeight", preferences.task_panel_height},
    };
    const auto temporary = temporary_path(path);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << document.dump(2) << '\n';
        output.flush();
        if (!output) {
            set_error(error, "Cannot write editor layout preferences.");
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    if (!replace_file(temporary, path, filesystem_error)) {
        set_error(error, "Cannot replace editor layout preferences.");
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    return true;
}

} // namespace fabric::editor
