#include "fabric/editor/map_session.hpp"
#include "fabric/editor/mechanic_presets.hpp"
#include "fabric/editor/mechanic_session.hpp"
#include "fabric/editor/scene_session.hpp"
#include "fabric/editor/session_transition.hpp"
#include "fabric/editor/transformation_session.hpp"
#include "fabric/project/document_storage.hpp"
#include "fabric/project/map_package.hpp"
#include "fabric/project/entity_transformation.hpp"
#include "fabric/render/map_preview.hpp"
#include "fabric/render/opengl_vector_renderer.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/runtime/preview_runtime.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>
#include <nfd.h>
#include <nfd_sdl2.h>

#include <filesystem>
#include <array>
#include <cctype>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <limits>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

std::vector<fabric::project::EntityTransformation> load_transformations(
    const std::filesystem::path& root,
    const fabric::project::ProjectManifest& manifest) {
    std::vector<fabric::project::EntityTransformation> result;
    const auto directory = root / manifest.directories.assets / "transformations";
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return result;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        const auto filename = iterator->path().filename().string();
        if (!iterator->is_regular_file(error) ||
            !filename.ends_with(".transformation.json")) continue;
        auto loaded = fabric::project::load_entity_transformation(
            root, manifest, iterator->path().lexically_relative(root));
        if (loaded.ok()) result.push_back(std::move(*loaded.asset));
    }
    std::ranges::sort(result, {}, [](const auto& value) {
        return value.document.name;
    });
    return result;
}

enum class CloseE2eMode { clean, window, system_shortcut, save, save_failure };

std::optional<CloseE2eMode> close_e2e_mode(const std::string_view value) {
    if (value == "clean") return CloseE2eMode::clean;
    if (value == "window") return CloseE2eMode::window;
    if (value == "shortcut") return CloseE2eMode::system_shortcut;
    if (value == "save") return CloseE2eMode::save;
    if (value == "save-failure") return CloseE2eMode::save_failure;
    return std::nullopt;
}

std::optional<std::string> read_binary_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{input},
                       std::istreambuf_iterator<char>{}};
}

void draw_errors(const fabric::editor::MapSession& session) {
    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s: %s", error.field.c_str(), error.message.c_str());
        ImGui::PopStyleColor();
    }
}

void draw_field_errors(const std::vector<fabric::project::Error>& errors,
                       const std::string_view field,
                       const std::string_view correction) {
    const auto qualified_field = "." + std::string(field);
    for (const auto& error : errors) {
        if (error.field != field && !error.field.starts_with(field) &&
            !error.field.ends_with(qualified_field)) continue;
        ImGui::PushStyleColor(ImGuiCol_Text, {0.98F, 0.48F, 0.42F, 1.0F});
        ImGui::TextWrapped("%s: %s", error.field.c_str(), error.message.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Correction: %s", std::string(correction).c_str());
    }
}

void draw_disabled_reason(const bool disabled, const std::string_view reason) {
    if (!disabled || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        return;
    ImGui::SetTooltip("%s", std::string(reason).c_str());
}

void draw_scene_errors(const fabric::editor::SceneSession& session) {
    for (const auto& error : session.errors()) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("%s: %s", error.field.c_str(),
                           error.message.c_str());
        ImGui::PopStyleColor();
    }
}

void draw_package_errors(
    const std::vector<fabric::project::Error>& errors) {
    for (const auto& error : errors) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.42F, 0.38F, 1.0F});
        ImGui::TextWrapped("Package %s: %s", error.field.c_str(),
                           error.message.c_str());
        ImGui::PopStyleColor();
    }
}

std::optional<std::filesystem::path> choose_folder(SDL_Window* window,
                                                    std::string& status) {
    nfdu8char_t* selected_path = nullptr;
    nfdpickfolderu8args_t arguments{};
    NFD_GetNativeWindowFromSDLWindow(window, &arguments.parentWindow);
    const auto result = NFD_PickFolderU8_With(&selected_path, &arguments);
    if (result == NFD_CANCEL) return std::nullopt;
    if (result == NFD_ERROR) {
        status = "Native folder dialog failed: " +
            std::string(NFD_GetError() == nullptr ? "unknown error"
                                                   : NFD_GetError());
        return std::nullopt;
    }
    const std::filesystem::path path{selected_path};
    NFD_FreePathU8(selected_path);
    return path;
}

std::optional<fabric::project::MapPropertyValue> parse_override_value(
    const int kind, const std::string_view text) {
    try {
        switch (kind) {
        case 0:
            if (text == "true") return true;
            if (text == "false") return false;
            return std::nullopt;
        case 1: {
            std::size_t consumed = 0;
            const auto value = std::stoll(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return static_cast<std::int64_t>(value);
        }
        case 2: {
            std::size_t consumed = 0;
            const auto value = std::stof(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return value;
        }
        case 3:
            return std::string{text};
        case 4: {
            const auto separator = text.find(',');
            if (separator == std::string_view::npos) return std::nullopt;
            std::size_t consumed_x = 0;
            std::size_t consumed_y = 0;
            const auto x = std::stof(std::string{text.substr(0, separator)}, &consumed_x);
            const auto y = std::stof(std::string{text.substr(separator + 1)}, &consumed_y);
            if (consumed_x != separator || consumed_y != text.size() - separator - 1U)
                return std::nullopt;
            return fabric::core::Vec2{x, y};
        }
        case 5:
            if (text.empty()) return std::nullopt;
            return fabric::project::ResourceReference{{.value = std::string{text}}, "resource"};
        default:
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<fabric::project::MechanicValue> parse_mechanic_override_value(
    const fabric::project::MechanicParameterDefinition& parameter,
    const std::string_view text) {
    try {
        using Type = fabric::project::MechanicValueType;
        switch (parameter.type) {
        case Type::boolean:
            if (text == "true") return true;
            if (text == "false") return false;
            return std::nullopt;
        case Type::integer: {
            std::size_t consumed = 0;
            const auto value = std::stoll(std::string{text}, &consumed);
            if (consumed != text.size()) return std::nullopt;
            return static_cast<std::int64_t>(value);
        }
        case Type::scalar: {
            std::size_t consumed = 0;
            const auto value = std::stof(std::string{text}, &consumed);
            if (consumed != text.size() || !std::isfinite(value))
                return std::nullopt;
            return value;
        }
        case Type::text: return std::string{text};
        case Type::vec2: {
            const auto separator = text.find(',');
            if (separator == std::string_view::npos) return std::nullopt;
            std::size_t consumed_x = 0;
            std::size_t consumed_y = 0;
            const auto x = std::stof(std::string{text.substr(0, separator)},
                                     &consumed_x);
            const auto y = std::stof(std::string{text.substr(separator + 1)},
                                     &consumed_y);
            if (consumed_x != separator ||
                consumed_y != text.size() - separator - 1U ||
                !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
            return fabric::core::Vec2{x, y};
        }
        case Type::resource: {
            if (text.empty()) return std::nullopt;
            const auto* fallback = std::get_if<fabric::project::ResourceReference>(
                &parameter.default_value);
            return fabric::project::ResourceReference{
                {.value = std::string{text}},
                fallback == nullptr ? "resource" : fallback->expected_type};
        }
        case Type::body_handle:
        case Type::pivot_handle:
        case Type::joint_handle: return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

std::string property_value_text(const fabric::project::MapPropertyValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<Value, std::int64_t>) return std::to_string(item);
        else if constexpr (std::is_same_v<Value, float>) return std::to_string(item);
        else if constexpr (std::is_same_v<Value, std::string>) return item;
        else if constexpr (std::is_same_v<Value, fabric::core::Vec2>)
            return std::to_string(item.x) + "," + std::to_string(item.y);
        else return item.id.value;
    }, value);
}

void draw_resource_picker(const char* label,
                          const std::filesystem::path& directory,
                          const std::string_view suffix,
                          std::string& selected_id) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return;
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) selected_id.clear();
    ImGui::PushID(label);
    static std::unordered_map<std::string, std::string> filters;
    auto& filter = filters[label];
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputText("Search", &filter);
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) continue;
        auto filename = iterator->path().filename().string();
        if (!filename.ends_with(suffix)) continue;
        filename.resize(filename.size() - suffix.size());
        if (!filter.empty()) {
            auto haystack = filename;
            auto needle = filter;
            std::ranges::transform(haystack, haystack.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            std::ranges::transform(needle, needle.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (haystack.find(needle) == std::string::npos) continue;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(filename.c_str())) selected_id = filename;
    }
    if (!selected_id.empty()) {
        const auto selected_path = directory /
            (selected_id + std::string{suffix});
        if (std::filesystem::is_regular_file(selected_path, error)) {
            ImGui::TextDisabled("Type: %s", std::string{suffix}.c_str());
            ImGui::TextDisabled("%s", selected_path.generic_string().c_str());
        } else {
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                               "Missing resource: %s", selected_id.c_str());
        }
    }
    ImGui::PopID();
}

struct SceneEditorState {
    std::string new_id;
    std::string new_name;
    std::string open_id;
    std::string edited_name;
    std::string map_id;
    std::string mount_id;
    int selected_map{-1};
    std::string transition_id;
    std::string target_scene_id;
    std::string entry_point;
    std::string event_id;
    int selected_transition{-1};
};

void draw_scene_editor(fabric::editor::SceneSession& session,
                       const std::filesystem::path& project_root,
                       SDL_Window* window, SceneEditorState& state,
                       std::string& status,
                       std::vector<fabric::project::Error>& package_errors) {
    ImGui::Begin("Scene Studio");
    if (project_root.empty()) {
        ImGui::TextDisabled("Open a project map to author its scenes.");
        ImGui::End();
        return;
    }
    const auto loaded_manifest = fabric::project::load_manifest(project_root);
    if (!loaded_manifest.ok()) {
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                           "Project manifest unavailable");
        ImGui::End();
        return;
    }
    const auto maps_directory = project_root /
        loaded_manifest.manifest->directories.maps;
    const auto scenes_directory = project_root /
        loaded_manifest.manifest->directories.scenes;
    std::vector<std::string> map_event_ids;
    if (session.scene()) {
        for (const auto& mounted : session.scene()->maps) {
            const auto map = fabric::project::load_map(
                project_root, *loaded_manifest.manifest,
                fabric::project::map_document_path(*loaded_manifest.manifest,
                                                   mounted.map.id));
            if (!map.ok()) continue;
            for (const auto& event : map.asset->events)
                if (std::ranges::find(map_event_ids, event.id.value) == map_event_ids.end())
                    map_event_ids.push_back(event.id.value);
        }
    }

    ImGui::SeparatorText("Create or open");
    ImGui::SetNextItemWidth(150.0F);
    ImGui::InputText("Scene id", &state.new_id);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputText("Scene name", &state.new_name);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.new_id.empty() || state.new_name.empty());
    if (ImGui::Button("Create scene")) {
        const fabric::project::SceneDocument scene{
            .document = {.schema_version = 1, .type = "scene",
                         .id = {.value = state.new_id},
                         .name = state.new_name}};
        const auto created = session.create(project_root, scene);
        status = created ? "Scene created" : "Scene creation rejected";
        if (created) {
            state.open_id = state.new_id;
            state.edited_name = state.new_name;
            state.new_id.clear();
            state.new_name.clear();
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.new_id.empty() || state.new_name.empty(),
                         "Enter both a scene id and a scene name.");
    draw_field_errors(session.errors(), "id",
                      "Use a unique non-empty scene id.");
    draw_field_errors(session.errors(), "name",
                      "Enter a visible non-empty scene name.");
    draw_resource_picker("Scenes:", scenes_directory, ".scene.json",
                         state.open_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.open_id.empty());
    if (ImGui::Button("Open scene")) {
        const auto opened = session.open(
            project_root, {.value = state.open_id});
        status = opened ? "Scene opened" : "Scene open rejected";
        if (opened) {
            state.edited_name = session.scene()->document.name;
            state.selected_transition = -1;
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.open_id.empty(),
                         "Choose an existing scene first.");

    if (!session.has_scene()) {
        draw_scene_errors(session);
        ImGui::End();
        return;
    }
    if (session.has_recovery()) {
        ImGui::TextColored({1.0F, 0.75F, 0.25F, 1.0F},
                           "A newer valid scene autosave is available.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Recover scene")) {
            if (session.accept_recovery()) {
                state.edited_name = session.scene()->document.name;
                status = "Scene autosave recovered";
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss scene recovery"))
            session.decline_recovery();
    }

    const auto scene_id = session.scene()->document.id;
    ImGui::SeparatorText("Scene document");
    ImGui::Text("%s (%s)%s", session.scene()->document.name.c_str(),
                scene_id.value.c_str(), session.dirty() ? " · dirty" : "");
    if (state.edited_name.empty())
        state.edited_name = session.scene()->document.name;
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::InputText("Name", &state.edited_name,
                         ImGuiInputTextFlags_EnterReturnsTrue))
        status = session.set_name(state.edited_name)
            ? "Scene name changed" : "Scene name rejected";
    if (ImGui::Button("Save scene"))
        status = session.save() ? "Scene saved" : "Scene save failed";
    ImGui::SameLine();
    if (ImGui::Button("Validate campaign")) {
        if (!session.dirty() || session.save()) {
            const auto planned = fabric::project::plan_scene_package(
                project_root, scene_id);
            package_errors = planned.errors;
            status = planned.ok() ? "Campaign package valid"
                                  : "Campaign validation failed";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Publish campaign")) {
        if (!session.dirty() || session.save()) {
            if (const auto parent = choose_folder(window, status)) {
                const auto destination = *parent /
                    (scene_id.value + ".scene-package");
                const auto published = fabric::project::publish_scene_package(
                    project_root, scene_id, destination);
                package_errors = published.errors;
                status = published.ok()
                    ? "Campaign published: " + destination.string()
                    : "Campaign publication failed";
            }
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.can_undo());
    if (ImGui::Button("Undo scene")) static_cast<void>(session.undo());
    ImGui::EndDisabled();
    draw_disabled_reason(!session.can_undo(),
                         "No scene changes are available to undo.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.can_redo());
    if (ImGui::Button("Redo scene")) static_cast<void>(session.redo());
    ImGui::EndDisabled();
    draw_disabled_reason(!session.can_redo(),
                         "No undone scene changes are available to redo.");

    ImGui::SeparatorText("Mounted maps");
    draw_resource_picker("Maps:", maps_directory, ".map.json", state.map_id);
    ImGui::SetNextItemWidth(170.0F);
    ImGui::InputText("Mount id", &state.mount_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.map_id.empty() || state.mount_id.empty());
    if (ImGui::Button(state.selected_map < 0 ? "Add mounted map"
                                             : "Apply mounted map")) {
        const fabric::project::SceneMapReference edited{
            {{.value = state.map_id}, "map"}, state.mount_id};
        const auto applied = state.selected_map < 0
            ? session.add_map(edited)
            : session.set_map(static_cast<std::size_t>(state.selected_map),
                              edited);
        status = applied ? "Scene map updated" : "Map mount rejected";
        if (applied) {
            state.map_id.clear();
            state.mount_id.clear();
            state.selected_map = -1;
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.map_id.empty() || state.mount_id.empty(),
                         "Choose a map and enter a mount layer id.");
    for (std::size_t index = 0; index < session.scene()->maps.size(); ++index) {
        const auto map = session.scene()->maps[index];
        ImGui::PushID(static_cast<int>(index));
        const bool entry = session.scene()->entry_map &&
            session.scene()->entry_map->id == map.map.id;
        if (ImGui::RadioButton("##entry", entry))
            static_cast<void>(session.set_entry_map(map.map.id));
        ImGui::SameLine();
        ImGui::Text("%s → %s", map.map.id.value.c_str(),
                    map.layer_id.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit map mount")) {
            state.selected_map = static_cast<int>(index);
            state.map_id = map.map.id.value;
            state.mount_id = map.layer_id;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove map")) {
            status = session.remove_map(index) ? "Map removed from scene"
                                               : "Map removal rejected";
            state.selected_map = -1;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (session.scene()->entry_map) {
        ImGui::TextDisabled("Entry map: %s",
                            session.scene()->entry_map->id.value.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear entry map"))
            static_cast<void>(session.set_entry_map(std::nullopt));
    }

    ImGui::SeparatorText("Transitions");
    ImGui::SetNextItemWidth(150.0F);
    ImGui::InputText("Transition id", &state.transition_id);
    draw_resource_picker("Target scenes:", scenes_directory, ".scene.json",
                         state.target_scene_id);
    ImGui::SetNextItemWidth(160.0F);
    ImGui::InputText("Entry point", &state.entry_point);
    ImGui::SetNextItemWidth(220.0F);
    if (map_event_ids.empty()) {
        ImGui::TextDisabled("Event: no mounted map events");
        state.event_id.clear();
    } else {
        std::vector<const char*> labels;
        labels.reserve(map_event_ids.size() + 1U);
        labels.push_back("(none)");
        for (const auto& id : map_event_ids) labels.push_back(id.c_str());
        int selected_event = 0;
        for (std::size_t index = 0; index < map_event_ids.size(); ++index)
            if (map_event_ids[index] == state.event_id) selected_event = static_cast<int>(index + 1U);
        if (ImGui::Combo("Event", &selected_event, labels.data(), static_cast<int>(labels.size())))
            state.event_id = selected_event == 0 ? std::string{} : map_event_ids[static_cast<std::size_t>(selected_event - 1)];
    }
    if (session.scene()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear event")) state.event_id.clear();
    }
    const auto transition_from_state = [&] {
        return fabric::project::SceneTransition{
            state.transition_id,
            {{.value = state.target_scene_id}, "scene"},
            state.entry_point,
            state.event_id.empty()
                ? std::nullopt
                : std::optional<fabric::core::ResourceId>{
                      {.value = state.event_id}}};
    };
    ImGui::BeginDisabled(state.transition_id.empty() ||
                         state.target_scene_id.empty() ||
                         state.entry_point.empty());
    if (state.selected_transition < 0) {
        if (ImGui::Button("Add transition")) {
            const auto added = session.add_transition(transition_from_state());
            status = added ? "Transition added" : "Transition rejected";
        }
    } else if (ImGui::Button("Apply transition")) {
        const auto applied = session.set_transition(
            static_cast<std::size_t>(state.selected_transition),
            transition_from_state());
        status = applied ? "Transition changed" : "Transition rejected";
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.transition_id.empty() ||
                             state.target_scene_id.empty() ||
                             state.entry_point.empty(),
                         "Enter a transition id, target scene and entry point.");
    ImGui::SameLine();
    if (ImGui::SmallButton("New transition")) {
        state.selected_transition = -1;
        state.transition_id.clear();
        state.target_scene_id.clear();
        state.entry_point.clear();
        state.event_id.clear();
    }
    for (std::size_t index = 0; index < session.scene()->transitions.size();
         ++index) {
        const auto transition = session.scene()->transitions[index];
        ImGui::PushID(static_cast<int>(index));
        const auto label = transition.id + " → " +
            transition.target_scene.id.value + ":" + transition.entry_point;
        if (ImGui::Selectable(label.c_str(),
                              state.selected_transition ==
                                  static_cast<int>(index))) {
            state.selected_transition = static_cast<int>(index);
            state.transition_id = transition.id;
            state.target_scene_id = transition.target_scene.id.value;
            state.entry_point = transition.entry_point;
            state.event_id = transition.event_id
                ? transition.event_id->value : "";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove transition")) {
            status = session.remove_transition(index)
                ? "Transition removed" : "Transition removal rejected";
            state.selected_transition = -1;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    draw_package_errors(package_errors);
    draw_scene_errors(session);
    ImGui::End();
}

std::string collision_shape_text(const fabric::project::CollisionShape& shape) {
    std::string result;
    switch (shape.kind) {
    case fabric::project::CollisionShapeKind::circle: result = "circle"; break;
    case fabric::project::CollisionShapeKind::capsule: result = "capsule"; break;
    case fabric::project::CollisionShapeKind::polygon: result = "polygon"; break;
    case fabric::project::CollisionShapeKind::chain: result = "chain"; break;
    }
    result += " @ " + std::to_string(shape.center.x) + "," +
              std::to_string(shape.center.y);
    if (shape.kind == fabric::project::CollisionShapeKind::circle ||
        shape.kind == fabric::project::CollisionShapeKind::capsule)
        result += " r=" + std::to_string(shape.radius);
    if (shape.kind == fabric::project::CollisionShapeKind::capsule)
        result += " l=" + std::to_string(shape.length);
    result += shape.sensor ? " [sensor]" : " [solid]";
    return result;
}

bool layer_visible(const fabric::project::MapDocument& map, const std::string& layer_id) {
    const auto layer = std::find_if(map.layers.begin(), map.layers.end(),
                                    [&](const auto& candidate) {
                                        return candidate.id == layer_id;
                                    });
    return layer == map.layers.end() || layer->visible;
}

struct TransformEditorState {
    std::string instance_id;
    fabric::core::Transform value{};
};

struct MechanicEditorState {
    std::string open_id;
    std::string new_id;
    std::string new_name;
    std::string selected_node;
    std::string new_node_id;
    int new_node_kind{};
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    fabric::editor::RotatingPlatformPresetRequest platform;
    int platform_activation{};
    int platform_direction{};
    std::string platform_visual_entity;
    fabric::physics::MechanicPreviewCharacterConfig preview_character;
    float preview_character_speed{3.0F};
};

void draw_mechanic_value_editor(
    fabric::editor::MechanicSession& session,
    const fabric::project::MechanicNodeDefinition& node,
    const fabric::project::MechanicNodeProperty& property,
    std::string& status) {
    ImGui::PushID(property.id.c_str());
    auto value = property.value;
    bool changed = false;
    std::visit([&](auto& item) {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, bool>) {
            changed = ImGui::Checkbox(property.id.c_str(), &item);
        } else if constexpr (std::is_same_v<Value, std::int64_t>) {
            auto edited = static_cast<int>(item);
            if (ImGui::InputInt(property.id.c_str(), &edited) &&
                ImGui::IsItemDeactivatedAfterEdit()) {
                item = edited;
                changed = true;
            }
        } else if constexpr (std::is_same_v<Value, float>) {
            changed = ImGui::DragFloat(property.id.c_str(), &item, 0.1F) &&
                      ImGui::IsItemDeactivatedAfterEdit();
        } else if constexpr (std::is_same_v<Value, std::string>) {
            changed = ImGui::InputText(property.id.c_str(), &item) &&
                      ImGui::IsItemDeactivatedAfterEdit();
        } else if constexpr (std::is_same_v<Value, fabric::core::Vec2>) {
            changed = ImGui::DragFloat2(property.id.c_str(), &item.x, 0.1F) &&
                      ImGui::IsItemDeactivatedAfterEdit();
        } else {
            changed = ImGui::InputText(property.id.c_str(), &item.id.value) &&
                      ImGui::IsItemDeactivatedAfterEdit();
        }
    }, value);
    if (changed) {
        status = session.set_node_property(
            {.value = node.id}, property.id, std::move(value))
            ? "Mechanic property changed"
            : "Mechanic property rejected";
    }
    ImGui::PopID();
}

void draw_mechanic_editor(fabric::editor::MechanicSession& session,
                          const fabric::editor::MapSession& map_session,
                          MechanicEditorState& state,
                          std::string& status) {
    ImGui::Begin("Mechanics");
    if (!map_session.map()) {
        ImGui::TextDisabled("Open a map before editing mechanics.");
        ImGui::End();
        return;
    }

    std::filesystem::path directory;
    if (map_session.manifest()) {
        directory = map_session.project_root() /
            map_session.manifest()->directories.assets / "mechanics";
        std::error_code error;
        if (std::filesystem::exists(directory, error)) {
            ImGui::TextDisabled("Project mechanics:");
            for (std::filesystem::directory_iterator iterator{directory, error}, end;
                 !error && iterator != end; iterator.increment(error)) {
                if (!iterator->is_regular_file(error)) continue;
                auto filename = iterator->path().filename().string();
                constexpr std::string_view suffix = ".mechanic.json";
                if (!filename.ends_with(suffix)) continue;
                filename.resize(filename.size() - suffix.size());
                ImGui::SameLine();
                if (ImGui::SmallButton(filename.c_str())) state.open_id = filename;
            }
        }
    }

    draw_resource_picker("Mechanics:", directory, ".mechanic.json",
                         state.open_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.open_id.empty());
    if (ImGui::Button("Open")) {
        status = session.open(map_session.project_root(), *map_session.map(),
                              {.value = state.open_id})
            ? "Mechanic opened" : "Mechanic could not be opened";
        state.selected_node.clear();
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.open_id.empty(),
                         "Choose an existing mechanic graph first.");
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputText("New id", &state.new_id);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0F);
    ImGui::InputText("New name", &state.new_name);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.new_id.empty() || state.new_name.empty());
    if (ImGui::Button("Create")) {
        fabric::project::MechanicGraph graph;
        graph.document.id = {.value = state.new_id};
        graph.document.name = state.new_name;
        status = session.create(map_session.project_root(), *map_session.map(), graph)
            ? "Mechanic created" : "Mechanic creation rejected";
        if (session.has_graph()) {
            state.open_id = state.new_id;
            state.new_id.clear();
            state.new_name.clear();
            state.selected_node.clear();
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.new_id.empty() || state.new_name.empty(),
                         "Enter both a mechanic id and a mechanic name.");

    ImGui::SeparatorText("Rotating platform preset");
    ImGui::SetNextItemWidth(150.0F);
    ImGui::InputText("Platform id", &state.platform.id.value);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0F);
    ImGui::InputText("Platform name", &state.platform.name);
    ImGui::Combo("Activation", &state.platform_activation,
                 "Presence sensor\0Map event\0");
    if (state.platform_activation == 1)
        ImGui::InputText("Activation event", &state.platform.event_id.value);
    if (state.platform_activation == 1 && !map_session.map()->events.empty()) {
        ImGui::TextDisabled("Map events:");
        for (const auto& event : map_session.map()->events) {
            ImGui::SameLine();
            if (ImGui::SmallButton(event.id.value.c_str()))
                state.platform.event_id = event.id;
        }
    }
    ImGui::InputText("Visual entity (optional)", &state.platform_visual_entity);
    if (map_session.manifest()) {
        const auto directory = map_session.project_root() /
            map_session.manifest()->directories.entities;
        std::error_code error;
        if (std::filesystem::exists(directory, error)) {
            ImGui::TextDisabled("Asset Studio entities:");
            for (std::filesystem::directory_iterator iterator{directory, error}, end;
                 !error && iterator != end; iterator.increment(error)) {
                if (!iterator->is_regular_file(error)) continue;
                auto filename = iterator->path().filename().string();
                constexpr std::string_view suffix = ".entity.json";
                if (!filename.ends_with(suffix)) continue;
                filename.resize(filename.size() - suffix.size());
                ImGui::SameLine();
                if (ImGui::SmallButton(filename.c_str()))
                    state.platform_visual_entity = filename;
            }
        }
    }
    ImGui::DragFloat2("Platform position", &state.platform.position.x, 0.1F);
    ImGui::DragFloat2("Platform size", &state.platform.size.x, 0.1F, 0.01F, 256.0F);
    ImGui::DragFloat("Speed (deg/s)",
                     &state.platform.speed_degrees_per_second, 1.0F, 0.0F, 3600.0F);
    ImGui::Combo("Direction", &state.platform_direction,
                 "Counter-clockwise (+1)\0Clockwise (-1)\0");
    ImGui::DragFloat("Acceleration (deg/s2)",
                     &state.platform.acceleration_degrees_per_second_squared,
                     1.0F, 0.0F, 7200.0F);
    ImGui::DragFloat("Maximum torque", &state.platform.maximum_torque,
                     1.0F, 0.0F, 100000.0F);
    ImGui::Checkbox("Angular limits", &state.platform.limit_enabled);
    if (state.platform.limit_enabled) {
        ImGui::DragFloat("Minimum angle", &state.platform.minimum_angle_degrees,
                         1.0F, -178.0F, 178.0F);
        ImGui::DragFloat("Maximum angle", &state.platform.maximum_angle_degrees,
                         1.0F, -178.0F, 178.0F);
    }
    if (state.platform_activation == 0) {
        ImGui::DragFloat2("Sensor center", &state.platform.sensor_center.x, 0.1F);
        ImGui::DragFloat2("Sensor size", &state.platform.sensor_size.x,
                          0.1F, 0.01F, 256.0F);
    }
    if (ImGui::Button("Create rotating platform")) {
        state.platform.activation = state.platform_activation == 0
            ? fabric::editor::RotatingPlatformActivation::sensor
            : fabric::editor::RotatingPlatformActivation::event;
        state.platform.direction = state.platform_direction == 0 ? 1 : -1;
        state.platform.visual_entity = state.platform_visual_entity.empty()
            ? std::nullopt
            : std::optional<fabric::project::ResourceReference>{
                  {{.value = state.platform_visual_entity}, "entity"}};
        const auto built = fabric::editor::build_rotating_platform_preset(
            *map_session.manifest(), *map_session.map(), state.platform);
        if (!built.ok()) {
            status = built.errors.empty() ? "Platform preset rejected"
                                          : built.errors.front().message;
        } else if (session.create(map_session.project_root(), *map_session.map(),
                                  *built.graph)) {
            state.open_id = state.platform.id.value;
            state.selected_node = "platform";
            status = "Rotating platform created from Studio preset";
        } else {
            status = session.errors().empty()
                ? "Platform could not replace the current dirty graph"
                : session.errors().front().message;
        }
    }

    if (!session.has_graph()) {
        for (const auto& error : session.errors())
            ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s: %s",
                               error.field.c_str(), error.message.c_str());
        ImGui::End();
        return;
    }
    const auto& graph = *session.graph();
    if (session.has_recovery()) {
        ImGui::TextColored({1.0F, 0.75F, 0.25F, 1.0F},
                           "A newer valid mechanic autosave is available.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Recover graph"))
            status = session.accept_recovery() ? "Mechanic autosave recovered"
                                                : "Mechanic recovery failed";
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss graph recovery")) {
            session.decline_recovery();
            status = "Mechanic recovery dismissed";
        }
    }
    ImGui::SeparatorText(graph.document.name.c_str());
    ImGui::TextColored(session.dirty() ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
                                       : ImVec4{0.45F, 0.9F, 0.55F, 1.0F},
                       session.dirty() ? "dirty" : "saved");
    ImGui::SameLine();
    if (ImGui::Button("Save graph"))
        status = session.save() ? "Mechanic saved" : "Mechanic save failed";
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.can_undo());
    if (ImGui::Button("Undo graph")) static_cast<void>(session.undo());
    ImGui::EndDisabled();
    draw_disabled_reason(!session.can_undo(),
                         "No mechanic changes are available to undo.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.can_redo());
    if (ImGui::Button("Redo graph")) static_cast<void>(session.redo());
    ImGui::EndDisabled();
    draw_disabled_reason(!session.can_redo(),
                         "No undone mechanic changes are available to redo.");

    ImGui::Columns(2, "mechanic-columns", true);
    ImGui::SeparatorText("Graph");
    for (const auto& node : graph.nodes) {
        const auto label = node.id + " [" + node.type + "]";
        if (ImGui::Selectable(label.c_str(), state.selected_node == node.id))
            state.selected_node = node.id;
    }
    ImGui::Combo("Node type", &state.new_node_kind,
                 "body\0pivot\0joint\0motor\0sensor\0constraint\0event\0");
    ImGui::InputText("Node id", &state.new_node_id);
    ImGui::BeginDisabled(state.new_node_id.empty());
    if (ImGui::Button("Add node")) {
        status = session.add_node(
            static_cast<fabric::project::MechanicNodeKind>(state.new_node_kind),
            state.new_node_id) ? "Mechanic node added" : "Mechanic node rejected";
        if (status == "Mechanic node added") {
            state.selected_node = state.new_node_id;
            state.new_node_id.clear();
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(state.new_node_id.empty(),
                         "Enter a unique node id before adding a node.");

    ImGui::SeparatorText("Connections");
    for (std::size_t index = 0; index < graph.connections.size(); ++index) {
        const auto& connection = graph.connections[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextWrapped("%s.%s -> %s.%s", connection.from_node.c_str(),
                           connection.from_port.c_str(), connection.to_node.c_str(),
                           connection.to_port.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Disconnect")) {
            status = session.disconnect(index) ? "Connection removed"
                                               : "Connection removal rejected";
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::InputText("From node", &state.from_node);
    ImGui::InputText("From port", &state.from_port);
    ImGui::InputText("To node", &state.to_node);
    ImGui::InputText("To port", &state.to_port);
    if (ImGui::Button("Connect ports")) {
        status = session.connect({state.from_node, state.from_port,
                                  state.to_node, state.to_port})
            ? "Ports connected" : "Connection rejected (types, direction or cycle)";
    }

    ImGui::NextColumn();
    ImGui::SeparatorText("Inspector");
    const auto selected = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [&](const auto& node) { return node.id == state.selected_node; });
    if (selected != graph.nodes.end()) {
        const auto selected_node = *selected;
        ImGui::Text("%s [%s]", selected_node.id.c_str(), selected_node.type.c_str());
        for (const auto& property : selected_node.properties)
            draw_mechanic_value_editor(session, selected_node, property, status);
        if (ImGui::Button("Remove selected node")) {
            status = session.remove_node({.value = selected_node.id})
                ? "Mechanic node removed" : "Mechanic node removal rejected";
            if (status == "Mechanic node removed") state.selected_node.clear();
        }
        ImGui::TextDisabled("Ports:");
        for (const auto& port : selected_node.ports)
            ImGui::BulletText("%s · %s · %s", port.id.c_str(),
                port.direction == fabric::project::MechanicPortDirection::input
                    ? "input" : "output",
                std::string{fabric::project::to_string(port.type)}.c_str());
    } else {
        ImGui::TextDisabled("Select a node to edit its properties.");
    }

    ImGui::SeparatorText("Simulation");
    const auto& simulation = session.simulation();
    ImGui::BeginDisabled(!simulation.valid());
    if (ImGui::Button(simulation.playing() ? "Pause" : "Play")) {
        if (simulation.playing()) session.pause(); else session.play();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(simulation.playing());
    if (ImGui::Button("Step 1/60"))
        status = session.step_once() ? "Simulation advanced one fixed step"
                                     : "Simulation step rejected";
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        status = session.reset_preview() ? "Simulation reset"
                                         : "Simulation reset failed";
    ImGui::EndDisabled();
    ImGui::Text("Fixed steps: %zu", simulation.step_count());
    ImGui::SeparatorText("Preview character");
    ImGui::DragFloat2("Character position", &state.preview_character.position.x,
                      0.1F);
    ImGui::DragFloat2("Character size", &state.preview_character.size.x,
                      0.05F, 0.05F, 16.0F);
    ImGui::DragFloat("Character friction", &state.preview_character.friction,
                     0.05F, 0.0F, 4.0F);
    if (ImGui::Button("Place / reset character"))
        status = session.place_preview_character(state.preview_character)
            ? "Preview character placed" : "Preview character rejected";
    ImGui::SameLine();
    if (ImGui::Button("Move left"))
        static_cast<void>(session.set_preview_character_velocity(
            {-state.preview_character_speed, 0.0F}));
    ImGui::SameLine();
    if (ImGui::Button("Stop character"))
        static_cast<void>(session.set_preview_character_velocity({0.0F, 0.0F}));
    ImGui::SameLine();
    if (ImGui::Button("Move right"))
        static_cast<void>(session.set_preview_character_velocity(
            {state.preview_character_speed, 0.0F}));
    ImGui::DragFloat("Character speed", &state.preview_character_speed,
                     0.1F, 0.0F, 20.0F);
    if (const auto character = simulation.preview_character_state())
        ImGui::BulletText("character  pos %.2f, %.2f  vel %.2f, %.2f",
                          character->position.x, character->position.y,
                          character->velocity.x, character->velocity.y);

    ImGui::SeparatorText("Mechanic debug overlay");
    for (const auto& signal : simulation.signal_states()) {
        bool manually_active = signal.manually_active;
        const auto label = signal.kind == fabric::physics::MechanicSignalKind::sensor
            ? "Inject sensor: " + signal.node_id
            : "Inject event: " + signal.event_id.value;
        if (ImGui::Checkbox(label.c_str(), &manually_active)) {
            const auto applied = signal.kind ==
                    fabric::physics::MechanicSignalKind::sensor
                ? session.set_preview_sensor_active(
                      {.value = signal.node_id}, manually_active)
                : session.set_preview_event_active(signal.event_id,
                                                   manually_active);
            status = applied ? "Preview activation signal changed"
                             : "Preview activation signal rejected";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("state: %s · physical overlaps: %zu",
                            signal.active ? "active" : "inactive",
                            signal.physical_overlap_count);
    }
    for (const auto& activation : simulation.activation_states())
        ImGui::BulletText("%s  %s  source %s", activation.node_id.c_str(),
                          activation.active ? "active" : "inactive",
                          activation.source_node_id
                              ? activation.source_node_id->c_str() : "always");
    for (const auto& event : simulation.debug_events())
        ImGui::TextDisabled("step %zu  %s  %s", event.step,
            event.node_id.c_str(),
            event.transition == fabric::physics::MechanicActivationTransition::begin
                ? "begin" : "end");
    for (const auto& body : simulation.body_states())
        ImGui::BulletText("%s  pos %.2f, %.2f  rot %.2f deg",
                          body.node_id.c_str(), body.position.x, body.position.y,
                          body.rotation_degrees);
    for (const auto& error : session.preview_errors())
        ImGui::TextColored({1.0F, 0.62F, 0.25F, 1.0F}, "%s: %s",
                           error.field.c_str(), error.message.c_str());
    ImGui::Columns(1);
    ImGui::End();
}

enum class CanvasGizmoMode { translate, rotate, scale };

struct CanvasGizmoState {
    bool active{};
    CanvasGizmoMode mode{CanvasGizmoMode::translate};
    std::string instance_id;
    ImVec2 start_mouse{};
    fabric::core::Transform start_transform{};
    fabric::core::Transform preview_transform{};
    std::vector<std::string> group_ids;
    fabric::core::Vec2 preview_delta{};
};

struct CollisionPointGizmoState {
    bool active{};
    int collision_index{-1};
    std::size_t point_index{};
    fabric::core::Vec2 preview_point{};
};

struct SelectionBoxState {
    bool active{};
    bool append{};
    ImVec2 start_mouse{};
    ImVec2 current_mouse{};
};

struct MapTexture {
    GLuint handle{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct MapPreviewRenderState {
    fabric::render::OpenGLVectorRenderer* renderer{};
    const fabric::render::MapPreviewResult* preview{};
    const std::filesystem::path* project_root{};
    const fabric::project::ProjectManifest* manifest{};
    std::unordered_map<std::string, MapTexture>* textures{};
    fabric::render::OpenGLVectorViewport viewport{};
};

void render_map_preview_callback(const ImDrawList*, const ImDrawCmd* command) {
    auto* state = static_cast<MapPreviewRenderState*>(command->UserCallbackData);
    if (state == nullptr || state->renderer == nullptr || state->preview == nullptr ||
        state->project_root == nullptr || state->manifest == nullptr ||
        state->textures == nullptr || state->preview->packets.empty()) return;
    const fabric::render::OpenGLTextureResolver resolver =
        [state](const fabric::core::ResourceId& id)
        -> std::optional<fabric::render::OpenGLTextureHandle> {
        const auto cached = state->textures->find(id.value);
        if (cached != state->textures->end()) {
            return fabric::render::OpenGLTextureHandle{
                cached->second.handle, cached->second.width, cached->second.height};
        }
        const auto loaded = fabric::project::load_texture_asset(
            *state->project_root, *state->manifest,
            fabric::project::texture_document_path(*state->manifest, id));
        if (!loaded.ok()) return std::nullopt;
        const auto decoded = fabric::render::load_png(
            *state->project_root / loaded.asset->source);
        if (!decoded.ok()) return std::nullopt;
        MapTexture texture{.width = decoded.image->width,
                           .height = decoded.image->height};
        glGenTextures(1, &texture.handle);
        glBindTexture(GL_TEXTURE_2D, texture.handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     static_cast<GLsizei>(texture.width),
                     static_cast<GLsizei>(texture.height), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, decoded.image->rgba8.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        const auto [inserted, _] = state->textures->emplace(id.value, texture);
        return fabric::render::OpenGLTextureHandle{
            inserted->second.handle, inserted->second.width, inserted->second.height};
    };
    static_cast<void>(state->renderer->draw(
        state->preview->packets, state->viewport, resolver));
}

void draw_transform_editor(fabric::editor::MapSession& session,
                           const std::vector<std::string>& selected_instances,
                           TransformEditorState& state,
                           std::string& status) {
    if (selected_instances.size() != 1U || !session.map()) return;
    const auto& map = *session.map();
    const auto found = std::find_if(map.instances.begin(), map.instances.end(),
                                    [&](const auto& instance) {
                                        return instance.id == selected_instances.front();
                                    });
    if (found == map.instances.end()) return;
    const auto instance_id = found->id;
    if (state.instance_id != instance_id) {
        state.instance_id = instance_id;
        state.value = found->transform;
    }
    ImGui::SeparatorText("Transform gizmo");
    ImGui::TextDisabled("Selected: %s", instance_id.c_str());
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Position", &state.value.position.x, 0.1F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Position transformed" : "Transform rejected (layer locked or invalid)";
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat("Rotation (degrees)", &state.value.rotation_degrees, 1.0F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Rotation transformed" : "Transform rejected (layer locked or invalid)";
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Scale", &state.value.scale.x, 0.01F, -32.0F, 32.0F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Scale transformed" : "Transform rejected (layer locked or invalid)";
    }
    ImGui::SetNextItemWidth(220.0F);
    if (ImGui::DragFloat2("Pivot", &state.value.pivot.x, 0.01F) &&
        ImGui::IsItemDeactivatedAfterEdit()) {
        status = session.set_instance_transform({.value = instance_id}, state.value)
            ? "Pivot transformed" : "Transform rejected (layer locked or invalid)";
    }
}

void draw_map_canvas(fabric::editor::MapSession& session,
                     std::vector<std::string>& selected_instances,
                     ImVec2& pan,
                     float& zoom,
                     CanvasGizmoState& gizmo,
                     int selected_collision_index,
                     CollisionPointGizmoState& point_gizmo,
                     int selected_trigger_index,
                     const std::string& active_layer_id,
                     SelectionBoxState& selection_box,
                     bool& placement_mode,
                     std::string& placement_id,
                     std::string& placement_resource_id,
                     int& placement_kind,
                     fabric::editor::MapSnapSettings& snapping,
                     MapPreviewRenderState& preview_render_state,
                     const fabric::physics::MechanicSimulation& mechanic_preview,
                     std::string& status) {
    if (!session.map()) return;
    const auto& map = *session.map();
    ImGui::SeparatorText("Canvas");
    ImGui::TextDisabled("Active layer: %s", active_layer_id.c_str());
    ImGui::Checkbox("Snap translation", &snapping.enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0F);
    ImGui::DragFloat("Grid size", &snapping.grid_size, 0.1F, 0.01F, 1024.0F);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0F);
    ImGui::DragFloat2("Origin", &snapping.origin.x, 0.1F);
    const ImVec2 canvas_size{ImGui::GetContentRegionAvail().x, 380.0F};
    auto frame_instances = [&](const bool selected_only) {
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        bool found = false;
        for (const auto& instance : map.instances) {
            if (!layer_visible(map, instance.layer_id)) continue;
            if (selected_only && std::find(selected_instances.begin(), selected_instances.end(),
                                           instance.id) == selected_instances.end()) continue;
            min_x = std::min(min_x, instance.transform.position.x);
            min_y = std::min(min_y, instance.transform.position.y);
            max_x = std::max(max_x, instance.transform.position.x);
            max_y = std::max(max_y, instance.transform.position.y);
            found = true;
        }
        if (!found) return false;
        const auto width = std::max(max_x - min_x, 1.0F);
        const auto height = std::max(max_y - min_y, 1.0F);
        zoom = std::clamp(std::min((canvas_size.x - 48.0F) / width,
                                   (canvas_size.y - 48.0F) / height), 0.1F, 32.0F);
        const auto center_x = (min_x + max_x) * 0.5F;
        const auto center_y = (min_y + max_y) * 0.5F;
        pan = {-center_x * zoom, center_y * zoom};
        return true;
    };
    ImGui::BeginDisabled(selected_instances.empty());
    if (ImGui::Button("Frame selection"))
        status = frame_instances(true) ? "Selection framed" : "No visible selected instance";
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Frame all"))
        status = frame_instances(false) ? "Map framed" : "No visible instance to frame";
    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map-canvas", canvas_size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 canvas_center{canvas_pos.x + canvas_size.x * 0.5F,
                               canvas_pos.y + canvas_size.y * 0.5F};
    auto world_to_screen = [&](const fabric::core::Vec2 point) {
        return ImVec2{canvas_center.x + pan.x + point.x * zoom,
                      canvas_center.y + pan.y - point.y * zoom};
    };
    auto screen_to_world = [&](const ImVec2 point) {
        return fabric::core::Vec2{(point.x - canvas_center.x - pan.x) / zoom,
                                  -(point.y - canvas_center.y - pan.y) / zoom};
    };
    auto transform_for = [&](const fabric::project::MapInstance& instance) {
        if (!gizmo.active) return instance.transform;
        if (gizmo.mode == CanvasGizmoMode::translate &&
            std::find(gizmo.group_ids.begin(), gizmo.group_ids.end(), instance.id) !=
                gizmo.group_ids.end()) {
            auto result = instance.transform;
            result.position.x += gizmo.preview_delta.x;
            result.position.y += gizmo.preview_delta.y;
            return result;
        }
        return gizmo.instance_id == instance.id ? gizmo.preview_transform : instance.transform;
    };
    auto collision_point_for = [&](const std::size_t collision_index,
                                   const std::size_t point_index,
                                   const fabric::core::Vec2 point) {
        return point_gizmo.active &&
                       point_gizmo.collision_index == static_cast<int>(collision_index) &&
                       point_gizmo.point_index == point_index
                   ? point_gizmo.preview_point : point;
    };

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_pos,
                        {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                        IM_COL32(22, 25, 31, 255));
    const float pixels_per_grid = zoom;
    float grid_step = 1.0F;
    while (grid_step * pixels_per_grid < 14.0F) grid_step *= 2.0F;
    while (grid_step * pixels_per_grid > 70.0F) grid_step *= 0.5F;
    const auto top_left = screen_to_world(canvas_pos);
    const auto bottom_right = screen_to_world(
        {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y});
    const auto first_x = static_cast<int>(std::floor(top_left.x / grid_step)) - 1;
    const auto last_x = static_cast<int>(std::ceil(bottom_right.x / grid_step)) + 1;
    const auto first_y = static_cast<int>(std::floor(bottom_right.y / grid_step)) - 1;
    const auto last_y = static_cast<int>(std::ceil(top_left.y / grid_step)) + 1;
    for (int x = first_x; x <= last_x; ++x) {
        const auto line = world_to_screen({static_cast<float>(x) * grid_step, 0.0F});
        draw->AddLine({line.x, canvas_pos.y},
                      {line.x, canvas_pos.y + canvas_size.y},
                      x == 0 ? IM_COL32(105, 115, 130, 220) : IM_COL32(48, 54, 64, 180));
    }
    for (int y = first_y; y <= last_y; ++y) {
        const auto line = world_to_screen({0.0F, static_cast<float>(y) * grid_step});
        draw->AddLine({canvas_pos.x, line.y},
                      {canvas_pos.x + canvas_size.x, line.y},
                      y == 0 ? IM_COL32(105, 115, 130, 220) : IM_COL32(48, 54, 64, 180));
    }
    const auto framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
    preview_render_state.viewport = {
        .width = std::max(1, static_cast<std::int32_t>(
            canvas_size.x * framebuffer_scale.x)),
        .height = std::max(1, static_cast<std::int32_t>(
            canvas_size.y * framebuffer_scale.y)),
        .world_bounds = {{top_left.x, bottom_right.y},
                         {bottom_right.x - top_left.x, top_left.y - bottom_right.y}},
        .x = std::max(0, static_cast<std::int32_t>(
            canvas_pos.x * framebuffer_scale.x)),
        .y = std::max(0, static_cast<std::int32_t>(
            (ImGui::GetIO().DisplaySize.y - canvas_pos.y - canvas_size.y) *
            framebuffer_scale.y)),
    };
    draw->AddCallback(render_map_preview_callback, &preview_render_state);
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    auto draw_mechanic_box = [&](const fabric::core::Vec2 position,
                                 const fabric::core::Vec2 size,
                                 const float rotation_degrees,
                                 const ImU32 fill, const ImU32 outline) {
        constexpr float degrees_to_radians = 0.01745329251994329577F;
        const auto angle = rotation_degrees * degrees_to_radians;
        const auto cosine = std::cos(angle);
        const auto sine = std::sin(angle);
        const fabric::core::Vec2 local[4] = {
            {-size.x * 0.5F, -size.y * 0.5F},
            {size.x * 0.5F, -size.y * 0.5F},
            {size.x * 0.5F, size.y * 0.5F},
            {-size.x * 0.5F, size.y * 0.5F}};
        ImVec2 points[4];
        for (std::size_t index = 0; index < 4; ++index)
            points[index] = world_to_screen({
                position.x + local[index].x * cosine - local[index].y * sine,
                position.y + local[index].x * sine + local[index].y * cosine});
        draw->AddQuadFilled(points[0], points[1], points[2], points[3], fill);
        draw->AddQuad(points[0], points[1], points[2], points[3], outline, 2.0F);
    };
    if (mechanic_preview.valid()) {
        for (const auto& body : mechanic_preview.body_states())
            draw_mechanic_box(body.position, body.size, body.rotation_degrees,
                              IM_COL32(75, 165, 180, 42),
                              IM_COL32(90, 220, 235, 235));
        for (const auto& sensor : mechanic_preview.sensor_states())
            draw_mechanic_box(sensor.position, sensor.size,
                              sensor.rotation_degrees,
                              sensor.active ? IM_COL32(105, 235, 135, 58)
                                            : IM_COL32(240, 190, 80, 35),
                              sensor.active ? IM_COL32(105, 235, 135, 245)
                                            : IM_COL32(240, 190, 80, 220));
        if (const auto character = mechanic_preview.preview_character_state())
            draw_mechanic_box(character->position, character->size,
                              character->rotation_degrees,
                              IM_COL32(225, 105, 190, 72),
                              IM_COL32(255, 145, 220, 245));
    }
    for (std::size_t collision_index = 0; collision_index < map.collisions.size();
         ++collision_index) {
        const auto& collision = map.collisions[collision_index];
        if (!layer_visible(map, collision.layer_id)) continue;
        const auto center = world_to_screen(collision.center);
        const auto color = collision.sensor ? IM_COL32(240, 190, 80, 210)
                                            : IM_COL32(220, 90, 90, 210);
        if (collision.kind == fabric::project::CollisionShapeKind::circle) {
            draw->AddCircle(center, collision.radius * zoom, color, 32, 2.0F);
        } else if (collision.kind == fabric::project::CollisionShapeKind::capsule) {
            const float half_length = collision.length * zoom * 0.5F;
            draw->AddLine({center.x - half_length, center.y},
                          {center.x + half_length, center.y}, color, 2.0F);
            draw->AddCircle({center.x - half_length, center.y}, collision.radius * zoom,
                            color, 24, 2.0F);
            draw->AddCircle({center.x + half_length, center.y}, collision.radius * zoom,
                            color, 24, 2.0F);
        } else if (collision.points.size() > 1U) {
            for (std::size_t point = 1; point < collision.points.size(); ++point)
                draw->AddLine(world_to_screen(collision_point_for(
                                  collision_index, point - 1, collision.points[point - 1])),
                              world_to_screen(collision_point_for(
                                  collision_index, point, collision.points[point])), color, 2.0F);
            if (collision.kind == fabric::project::CollisionShapeKind::polygon)
                draw->AddLine(world_to_screen(collision_point_for(
                                  collision_index, collision.points.size() - 1,
                                  collision.points.back())),
                              world_to_screen(collision_point_for(
                                  collision_index, 0, collision.points.front())), color, 2.0F);
        }
    }
    if (selected_collision_index >= 0 &&
        static_cast<std::size_t>(selected_collision_index) < map.collisions.size()) {
        const auto& collision = map.collisions[static_cast<std::size_t>(selected_collision_index)];
        if (collision.kind == fabric::project::CollisionShapeKind::polygon ||
            collision.kind == fabric::project::CollisionShapeKind::chain) {
            for (std::size_t point = 0; point < collision.points.size(); ++point) {
                const auto position = world_to_screen(collision_point_for(
                    static_cast<std::size_t>(selected_collision_index), point,
                    collision.points[point]));
                const auto active = point_gizmo.active &&
                    point_gizmo.collision_index == selected_collision_index &&
                    point_gizmo.point_index == point;
                draw->AddCircleFilled(position, active ? 7.0F : 5.0F,
                                      IM_COL32(250, 210, 90, 255));
            }
        }
    }
    for (std::size_t trigger_index = 0; trigger_index < map.triggers.size(); ++trigger_index) {
        const auto& trigger = map.triggers[trigger_index];
        if (trigger.collision_index >= map.collisions.size()) continue;
        const auto& collision = map.collisions[trigger.collision_index];
        if (!layer_visible(map, collision.layer_id)) continue;
        fabric::core::Vec2 anchor = collision.center;
        if (!collision.points.empty()) {
            anchor = {};
            for (const auto point : collision.points) {
                anchor.x += point.x;
                anchor.y += point.y;
            }
            const auto count = static_cast<float>(collision.points.size());
            anchor.x /= count;
            anchor.y /= count;
        }
        const auto label_position = world_to_screen(anchor);
        const auto color = static_cast<int>(trigger_index) == selected_trigger_index
            ? IM_COL32(255, 240, 100, 255) : IM_COL32(160, 220, 255, 230);
        draw->AddText({label_position.x + 8.0F, label_position.y + 8.0F}, color,
                      trigger.event_id.value.c_str());
    }
    for (const auto& instance : map.instances) {
        if (!layer_visible(map, instance.layer_id)) continue;
        const auto transform = transform_for(instance);
        const auto point = world_to_screen(transform.position);
        const bool selected = std::find(selected_instances.begin(), selected_instances.end(),
                                        instance.id) != selected_instances.end();
        draw->AddCircleFilled(point, selected ? 8.0F : 6.0F,
                              selected ? IM_COL32(90, 190, 255, 255)
                                       : IM_COL32(150, 205, 165, 255));
        draw->AddText({point.x + 9.0F, point.y - 7.0F}, IM_COL32(220, 225, 235, 230),
                      instance.id.c_str());
    }
    if (selected_instances.size() == 1U) {
        const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                           [&](const auto& instance) {
                                               return instance.id == selected_instances.front();
                                           });
        if (selected != map.instances.end() && layer_visible(map, selected->layer_id)) {
            const auto transform = transform_for(*selected);
            const auto center = world_to_screen(transform.position);
            const auto scale_handle = ImVec2{center.x + 36.0F, center.y};
            const auto rotate_handle = ImVec2{center.x, center.y - 36.0F};
            draw->AddLine({center.x - 18.0F, center.y}, {center.x + 48.0F, center.y},
                          IM_COL32(90, 190, 255, 180), 1.0F);
            draw->AddCircleFilled(scale_handle, 6.0F, IM_COL32(100, 220, 140, 240));
            draw->AddCircle(rotate_handle, 6.0F, IM_COL32(240, 180, 80, 240), 16, 2.0F);
            draw->AddCircle(center, 11.0F, IM_COL32(90, 190, 255, 240), 24, 2.0F);
        }
    } else if (selected_instances.size() > 1U) {
        ImVec2 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        ImVec2 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        bool found = false;
        for (const auto& instance : map.instances) {
            if (std::find(selected_instances.begin(), selected_instances.end(), instance.id) ==
                    selected_instances.end() || !layer_visible(map, instance.layer_id))
                continue;
            const auto point = world_to_screen(transform_for(instance).position);
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            found = true;
        }
        if (found) {
            minimum.x -= 12.0F;
            minimum.y -= 12.0F;
            maximum.x += 12.0F;
            maximum.y += 12.0F;
            draw->AddRect(minimum, maximum, IM_COL32(90, 190, 255, 230), 2.0F, 0, 2.0F);
        }
    }
    draw->AddRect(canvas_pos,
                  {canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y},
                  IM_COL32(130, 140, 155, 255));
    if (selection_box.active) {
        const ImVec2 minimum{std::min(selection_box.start_mouse.x,
                                      selection_box.current_mouse.x),
                             std::min(selection_box.start_mouse.y,
                                      selection_box.current_mouse.y)};
        const ImVec2 maximum{std::max(selection_box.start_mouse.x,
                                      selection_box.current_mouse.x),
                             std::max(selection_box.start_mouse.y,
                                      selection_box.current_mouse.y)};
        draw->AddRectFilled(minimum, maximum, IM_COL32(80, 160, 240, 35));
        draw->AddRect(minimum, maximum, IM_COL32(100, 190, 255, 220));
    }

    if (hovered) {
        const auto& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0F) {
            const auto before = screen_to_world(io.MousePos);
            zoom = std::clamp(zoom * std::pow(1.15F, io.MouseWheel), 0.1F, 32.0F);
            const auto after = world_to_screen(before);
            pan.x += io.MousePos.x - after.x;
            pan.y += io.MousePos.y - after.y;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan.x += io.MouseDelta.x;
            pan.y += io.MouseDelta.y;
        }
        if (!io.WantTextInput && !placement_mode && !gizmo.active &&
            !point_gizmo.active && !selection_box.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                status = frame_instances(true) ? "Selection framed"
                                               : "No visible selected instance to frame";
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
                status = frame_instances(false) ? "Map framed"
                                                : "No visible instance to frame";
        }
        if (!io.WantTextInput && !placement_mode && !gizmo.active &&
            !point_gizmo.active && !selection_box.active && !selected_instances.empty()) {
            std::vector<fabric::core::ResourceId> ids;
            for (const auto& id : selected_instances) ids.push_back({.value = id});
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                const auto removed = session.remove_instances(ids);
                status = removed ? "Selected instances deleted"
                                 : "Delete rejected (selection locked or invalid)";
                if (removed) selected_instances.clear();
                return;
            }
            fabric::core::Vec2 nudge{};
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) nudge.x = -1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) nudge.x = 1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) nudge.y = 1.0F;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) nudge.y = -1.0F;
            if (nudge.x != 0.0F || nudge.y != 0.0F) {
                const auto moved = session.translate_instances(ids, nudge, snapping);
                status = moved ? "Selected instances nudged"
                                : "Nudge rejected (selection locked or invalid)";
                return;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) &&
                selected_instances.size() == 1U) {
                const auto duplicated = session.duplicate_instance(
                    ids.front(), {1.0F, 1.0F}, snapping);
                status = duplicated ? "Selected instance duplicated"
                                    : "Duplication rejected (locked or invalid)";
                return;
            }
        }
        if (!placement_mode && !gizmo.active && !point_gizmo.active && io.KeyCtrl &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_instances.size() == 1U) {
            const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                               [&](const auto& instance) {
                                                   return instance.id == selected_instances.front();
                                               });
            if (selected != map.instances.end()) {
                const auto target = screen_to_world(io.MousePos);
                const fabric::core::Vec2 offset{
                    target.x - selected->transform.position.x,
                    target.y - selected->transform.position.y};
                const auto duplicated = session.duplicate_instance(
                    {.value = selected->id}, offset, snapping);
                status = duplicated ? "Instance duplicated at cursor"
                                    : "Duplication rejected (locked or invalid)";
                return;
            }
        }
        if (placement_mode && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            fabric::project::MapInstance instance;
            instance.id = placement_id;
            instance.layer_id = active_layer_id;
            instance.transform.position = screen_to_world(io.MousePos);
            if (placement_kind == 0)
                instance.entity = fabric::project::ResourceReference{
                    {.value = placement_resource_id}, "entity"};
            else
                instance.prefab = fabric::project::ResourceReference{
                    {.value = placement_resource_id}, "prefab"};
            const auto placed = session.place_instance(std::move(instance), snapping);
            status = placed ? "Instance placed" :
                             "Placement rejected (id, resource, layer or lock)";
            if (placed) {
                placement_mode = false;
                placement_id.clear();
                placement_resource_id.clear();
            }
            return;
        }
        if (!gizmo.active && !point_gizmo.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && selected_collision_index >= 0) {
            const auto collision_index = static_cast<std::size_t>(selected_collision_index);
            if (collision_index < map.collisions.size()) {
                const auto& collision = map.collisions[collision_index];
                if (collision.kind == fabric::project::CollisionShapeKind::polygon ||
                    collision.kind == fabric::project::CollisionShapeKind::chain) {
                    for (std::size_t point = 0; point < collision.points.size(); ++point) {
                        const auto screen = world_to_screen(collision.points[point]);
                        const auto dx = io.MousePos.x - screen.x;
                        const auto dy = io.MousePos.y - screen.y;
                        if (std::sqrt(dx * dx + dy * dy) <= 10.0F) {
                            point_gizmo.active = true;
                            point_gizmo.collision_index = selected_collision_index;
                            point_gizmo.point_index = point;
                            point_gizmo.preview_point = collision.points[point];
                            break;
                        }
                    }
                }
            }
        }
        if (point_gizmo.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                point_gizmo.preview_point = screen_to_world(io.MousePos);
            } else {
                const auto index = static_cast<std::size_t>(point_gizmo.collision_index);
                if (index < map.collisions.size() &&
                    point_gizmo.point_index < map.collisions[index].points.size()) {
                    auto shape = map.collisions[index];
                    shape.points[point_gizmo.point_index] = point_gizmo.preview_point;
                    const auto committed = session.set_collision_shape(index, std::move(shape));
                    status = committed ? "Collision point committed"
                                       : "Collision point rejected (layer locked or invalid)";
                }
                point_gizmo.active = false;
                point_gizmo.collision_index = -1;
            }
        }
        if (!gizmo.active && !point_gizmo.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !selected_instances.empty()) {
            const auto selected = std::find_if(map.instances.begin(), map.instances.end(),
                                               [&](const auto& instance) {
                                                   return instance.id == selected_instances.front();
                                               });
            if (selected_instances.size() == 1U && selected != map.instances.end()) {
                const auto transform = transform_for(*selected);
                const auto center = world_to_screen(transform.position);
                const auto scale_handle = ImVec2{center.x + 36.0F, center.y};
                const auto rotate_handle = ImVec2{center.x, center.y - 36.0F};
                const auto distance = [](const ImVec2 a, const ImVec2 b) {
                    const auto dx = a.x - b.x;
                    const auto dy = a.y - b.y;
                    return std::sqrt(dx * dx + dy * dy);
                };
                const auto handle_distance = distance(io.MousePos, scale_handle);
                const auto rotate_distance = distance(io.MousePos, rotate_handle);
                const auto center_distance = distance(io.MousePos, center);
                if (handle_distance <= 10.0F || rotate_distance <= 10.0F ||
                    center_distance <= 12.0F) {
                    gizmo.active = true;
                    gizmo.instance_id = selected->id;
                    gizmo.start_mouse = io.MousePos;
                    gizmo.start_transform = selected->transform;
                    gizmo.preview_transform = selected->transform;
                    gizmo.mode = handle_distance <= 10.0F
                        ? CanvasGizmoMode::scale
                        : (rotate_distance <= 10.0F ? CanvasGizmoMode::rotate
                                                   : CanvasGizmoMode::translate);
                }
            } else if (selected_instances.size() > 1U) {
                const auto hit = std::find_if(map.instances.begin(), map.instances.end(),
                                              [&](const auto& instance) {
                                                  if (std::find(selected_instances.begin(),
                                                                selected_instances.end(),
                                                                instance.id) == selected_instances.end())
                                                      return false;
                                                  const auto point = world_to_screen(
                                                      instance.transform.position);
                                                  const auto dx = io.MousePos.x - point.x;
                                                  const auto dy = io.MousePos.y - point.y;
                                                  return std::sqrt(dx * dx + dy * dy) <= 12.0F;
                                              });
                if (hit != map.instances.end()) {
                    gizmo.active = true;
                    gizmo.mode = CanvasGizmoMode::translate;
                    gizmo.instance_id = hit->id;
                    gizmo.start_mouse = io.MousePos;
                    gizmo.preview_delta = {};
                    gizmo.group_ids = selected_instances;
                }
            }
        }
        if (gizmo.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (gizmo.mode == CanvasGizmoMode::translate) {
                    const auto start = screen_to_world(gizmo.start_mouse);
                    const auto current = screen_to_world(io.MousePos);
                    gizmo.preview_delta = {current.x - start.x, current.y - start.y};
                    if (gizmo.group_ids.size() == 1U)
                        gizmo.preview_transform.position = {
                            gizmo.start_transform.position.x + gizmo.preview_delta.x,
                            gizmo.start_transform.position.y + gizmo.preview_delta.y};
                } else if (gizmo.mode == CanvasGizmoMode::rotate) {
                    const auto center = world_to_screen(gizmo.start_transform.position);
                    const auto start_angle = std::atan2(gizmo.start_mouse.y - center.y,
                                                        gizmo.start_mouse.x - center.x);
                    const auto current_angle = std::atan2(io.MousePos.y - center.y,
                                                          io.MousePos.x - center.x);
                    constexpr float radians_to_degrees = 57.29577951308232F;
                    gizmo.preview_transform.rotation_degrees =
                        gizmo.start_transform.rotation_degrees +
                        (current_angle - start_angle) * radians_to_degrees;
                } else {
                    const auto delta = (io.MousePos.x - gizmo.start_mouse.x) / 48.0F;
                    const auto factor = std::max(0.05F, 1.0F + delta);
                    gizmo.preview_transform.scale = {
                        gizmo.start_transform.scale.x * factor,
                        gizmo.start_transform.scale.y * factor};
                }
            } else {
                bool committed = false;
                if (gizmo.group_ids.size() > 1U) {
                    std::vector<fabric::core::ResourceId> ids;
                    for (const auto& id : gizmo.group_ids) ids.push_back({.value = id});
                    committed = session.translate_instances(ids, gizmo.preview_delta, snapping);
                } else {
                    committed = session.set_instance_transform(
                        {.value = gizmo.instance_id}, gizmo.preview_transform,
                        gizmo.mode == CanvasGizmoMode::translate
                            ? snapping : fabric::editor::MapSnapSettings{.enabled = false});
                }
                status = committed ? "Canvas transform committed"
                                   : "Canvas transform rejected (layer locked or invalid)";
                gizmo.active = false;
                gizmo.instance_id.clear();
                gizmo.group_ids.clear();
                gizmo.preview_delta = {};
            }
        }
        if (!gizmo.active && !point_gizmo.active && !placement_mode &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyCtrl) {
            selection_box.active = true;
            selection_box.append = io.KeyShift;
            selection_box.start_mouse = io.MousePos;
            selection_box.current_mouse = io.MousePos;
        }
        if (selection_box.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                selection_box.current_mouse = io.MousePos;
            } else {
                const auto start = screen_to_world(selection_box.start_mouse);
                const auto end = screen_to_world(selection_box.current_mouse);
                const auto min_x = std::min(start.x, end.x);
                const auto max_x = std::max(start.x, end.x);
                const auto min_y = std::min(start.y, end.y);
                const auto max_y = std::max(start.y, end.y);
                const auto width = std::abs(selection_box.current_mouse.x -
                                            selection_box.start_mouse.x);
                const auto height = std::abs(selection_box.current_mouse.y -
                                             selection_box.start_mouse.y);
                if (width >= 5.0F || height >= 5.0F) {
                    if (!selection_box.append) selected_instances.clear();
                    for (const auto& instance : map.instances) {
                        if (!layer_visible(map, instance.layer_id)) continue;
                        const auto& position = instance.transform.position;
                        if (position.x >= min_x && position.x <= max_x &&
                            position.y >= min_y && position.y <= max_y &&
                            std::find(selected_instances.begin(), selected_instances.end(),
                                      instance.id) == selected_instances.end())
                            selected_instances.push_back(instance.id);
                    }
                    status = "Rectangle selection changed";
                } else {
                    const auto world = screen_to_world(selection_box.start_mouse);
                    auto hit = map.instances.end();
                    float best_distance = 12.0F / zoom;
                    for (auto candidate = map.instances.begin(); candidate != map.instances.end();
                         ++candidate) {
                        if (!layer_visible(map, candidate->layer_id)) continue;
                        const auto dx = candidate->transform.position.x - world.x;
                        const auto dy = candidate->transform.position.y - world.y;
                        const auto distance = std::sqrt(dx * dx + dy * dy);
                        if (distance <= best_distance) {
                            best_distance = distance;
                            hit = candidate;
                        }
                    }
                    if (!selection_box.append) selected_instances.clear();
                    if (hit != map.instances.end()) {
                        const auto existing = std::find(selected_instances.begin(),
                                                        selected_instances.end(), hit->id);
                        if (selection_box.append && existing != selected_instances.end())
                            selected_instances.erase(existing);
                        else if (existing == selected_instances.end())
                            selected_instances.push_back(hit->id);
                        status = "Canvas selection changed";
                    }
                }
                selection_box.active = false;
            }
        }
        if (!gizmo.active && !selection_box.active &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto world = screen_to_world(io.MousePos);
            auto hit = map.instances.end();
            float best_distance = 12.0F / zoom;
            for (auto candidate = map.instances.begin(); candidate != map.instances.end();
                 ++candidate) {
                if (!layer_visible(map, candidate->layer_id)) continue;
                const auto dx = candidate->transform.position.x - world.x;
                const auto dy = candidate->transform.position.y - world.y;
                const auto distance = std::sqrt(dx * dx + dy * dy);
                if (distance <= best_distance) {
                    best_distance = distance;
                    hit = candidate;
                }
            }
            if (!io.KeyShift) selected_instances.clear();
            if (hit != map.instances.end()) {
                const auto selected = std::find(selected_instances.begin(),
                                                selected_instances.end(), hit->id);
                if (io.KeyShift && selected != selected_instances.end())
                    selected_instances.erase(selected);
                else if (selected == selected_instances.end())
                    selected_instances.push_back(hit->id);
                status = "Canvas selection changed";
            }
        }
    }
    ImGui::TextDisabled("Zoom %.2fx · pan %.1f, %.1f · clic: sélectionner · molette: zoom · bouton milieu: déplacer",
                       zoom, pan.x, pan.y);
}

int run(const std::filesystem::path& project_root,
        const fabric::core::ResourceId& map_id,
        const std::optional<CloseE2eMode> e2e_mode = std::nullopt,
        const bool scene_e2e = false,
        const bool transformation_e2e = false) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    if (NFD_Init() != NFD_OKAY) {
        std::cerr << "native file dialog initialization failed: "
                  << (NFD_GetError() == nullptr ? "unknown error"
                                                 : NFD_GetError()) << '\n';
        SDL_Quit();
        return 1;
    }
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    const auto window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
        (e2e_mode || scene_e2e || transformation_e2e ? SDL_WINDOW_HIDDEN : 0U);
    auto* window = SDL_CreateWindow("Vertex Loom Map Studio",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1200, 760,
        window_flags);
    if (window == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        NFD_Quit();
        SDL_Quit();
        return 1;
    }
    auto context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        NFD_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window, context);
#if defined(__APPLE__)
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 130");
#endif

    fabric::editor::MapSession session;
    fabric::editor::MechanicSession mechanic_session;
    fabric::editor::SceneSession scene_session;
    fabric::editor::SessionTransitionGuard transition_guard;
    MechanicEditorState mechanic_editor;
    SceneEditorState scene_editor;
    fabric::render::OpenGLVectorRenderer map_renderer;
    std::unordered_map<std::string, MapTexture> map_textures;
    if (!map_renderer.initialize()) {
        std::cerr << "map OpenGL vector renderer initialization failed: "
                  << map_renderer.initialization_error() << '\n';
    }
    std::string status;
    std::string new_map_id;
    std::string new_map_name;
    std::string open_map_id;
    std::vector<fabric::project::Error> package_errors;
    const auto save_dirty_documents = [&] {
        return (!mechanic_session.dirty() || mechanic_session.save()) &&
            (!session.dirty() || session.save()) &&
            (!scene_session.dirty() || scene_session.save());
    };
    const auto prepare_package = [&] {
        if (!save_dirty_documents()) {
            status = "A document save failed; package action cancelled";
            return false;
        }
        return true;
    };
    if (!project_root.empty()) {
        if (!session.open(project_root, map_id)) status = "Map could not be opened";
        else status = "Map opened";
    }
    bool e2e_failed = false;
    bool e2e_event_injected = false;
    bool e2e_modal_handled = false;
    std::filesystem::path e2e_primary_path;
    std::filesystem::path e2e_autosave_path;
    std::optional<std::string> e2e_primary_contents;
    std::optional<std::string> e2e_autosave_contents;
    const auto fail_e2e = [&](const std::string_view message) {
        std::cerr << "Map Studio close E2E: " << message << '\n';
        e2e_failed = true;
    };
    bool scene_e2e_complete = false;
    if (scene_e2e) {
        if (!session.has_map()) {
            fail_e2e("scene fixture map could not be opened");
        } else {
            const auto actor_position = session.map()->instances.empty()
                ? fabric::core::Vec2{}
                : session.map()->instances.front().transform.position;
            const bool trigger_authored =
                session.add_layer({"collision-e2e", "Collision E2E",
                    fabric::project::MapLayerKind::collision,
                    true, false, 0.0F}) &&
                session.add_layer({"triggers-e2e", "Triggers E2E",
                    fabric::project::MapLayerKind::triggers,
                    true, false, 0.0F}) &&
                session.add_collision_shape({
                    fabric::project::CollisionShapeKind::circle,
                    "collision-e2e", true, actor_position, 2.0F, 0.0F, {}}) &&
                session.declare_event(
                    {{.value = "scene-e2e-entered"}, {}}) &&
                session.add_trigger({
                    "scene-e2e-zone", "triggers-e2e", 0U,
                    {.value = "scene-e2e-entered"},
                    {{"source", std::string{"scene-studio"}}}}) &&
                session.save();
            const fabric::project::SceneDocument scene{
                .document = {.schema_version = 1, .type = "scene",
                             .id = {.value = "scene-studio-e2e"},
                             .name = "Scene Studio E2E"}};
            const auto destination = project_root.parent_path() /
                "scene-studio-e2e.scene-package";
            const bool authored = trigger_authored &&
                scene_session.create(project_root, scene) &&
                scene_session.add_map({{map_id, "map"}, "world"}) &&
                scene_session.set_entry_map(map_id) &&
                scene_session.add_transition(
                    {"loop", {{.value = "scene-studio-e2e"}, "scene"},
                     "start", std::nullopt}) &&
                scene_session.save();
            const auto published = authored
                ? fabric::project::publish_scene_package(
                      project_root, scene.document.id, destination)
                : fabric::project::ScenePackagePublishResult{};
            fabric::editor::SceneSession reloaded;
            fabric::editor::MapSession reloaded_map;
            if (!authored || !published.ok() ||
                !reloaded.open(project_root, scene.document.id) ||
                !reloaded_map.open(project_root, map_id) ||
                reloaded_map.map()->triggers.size() != 1U ||
                reloaded_map.map()->triggers.front().properties.size() != 1U ||
                reloaded.scene()->maps.size() != 1U ||
                reloaded.scene()->transitions.size() != 1U ||
                !std::filesystem::is_regular_file(
                    destination /
                    fabric::project::scene_package_manifest_filename)) {
                fail_e2e("scene authoring, reload or publication failed");
            } else {
                scene_e2e_complete = true;
                status = "Scene E2E authored and published";
            }
        }
    }
    bool transformation_e2e_complete = false;
    if (transformation_e2e) {
        if (!session.has_map()) {
            fail_e2e("transformation fixture map could not be opened");
        } else {
            fabric::project::EntityTransformation transformation;
            transformation.document.id = {.value = "map-transformation-e2e"};
            transformation.document.name = "Map Transformation E2E";
            transformation.source_entity = {
                {.value = "rotating-platform-entity"}, "entity"};
            transformation.destination_entity = {
                {.value = "textile-head-entity"}, "entity"};
            fabric::editor::TransformationSession transformation_session;
            const bool authored = transformation_session.create(
                    project_root, transformation) &&
                transformation_session.save();
            fabric::runtime::PreviewRuntime preview_runtime;
            const bool loaded = authored && preview_runtime.load({
                .project_root = project_root,
                .map_id = map_id,
                .mode = fabric::runtime::RuntimeMode::smoke_test});
            const bool transformed = loaded && preview_runtime.transform_instance(
                "rotating-platform-instance", transformation.document.id);
            const auto destination = transformed
                ? preview_runtime.instance_entity_id("rotating-platform-instance")
                : std::nullopt;
            const auto source_instance = std::ranges::find(
                session.map()->instances, std::string{"rotating-platform-instance"},
                &fabric::project::MapInstance::id);
            transformation_e2e_complete = destination &&
                destination->value == "textile-head-entity" &&
                !preview_runtime.packet_order().empty() &&
                source_instance != session.map()->instances.end() &&
                source_instance->prefab &&
                source_instance->prefab->id.value == "rotating-platform-prefab" &&
                !session.dirty();
            if (!transformation_e2e_complete)
                fail_e2e("isolated transformation preview did not preserve the map");
        }
    }
    if (e2e_mode && *e2e_mode != CloseE2eMode::clean) {
        if (!session.has_map() || !session.manifest()) {
            fail_e2e("fixture map could not be opened");
        } else if (!session.declare_event({
                       {.value = "close-e2e-dirty"}, {}})) {
            fail_e2e("fixture could not be made dirty");
        } else {
            const auto autosave_start =
                fabric::editor::AutosaveScheduler::Clock::now();
            static_cast<void>(session.update_autosave(autosave_start));
            const auto autosave_status = session.update_autosave(
                autosave_start + std::chrono::seconds{31});
            if (autosave_status != fabric::editor::AutosaveStatus::saved)
                fail_e2e("autosave was not written");
            const auto document_path = fabric::project::map_document_path(
                *session.manifest(), map_id);
            e2e_primary_path = project_root / document_path;
            e2e_autosave_path = project_root /
                fabric::project::autosave_document_path(document_path);
            e2e_primary_contents = read_binary_file(e2e_primary_path);
            e2e_autosave_contents = read_binary_file(e2e_autosave_path);
            if (!e2e_primary_contents || !e2e_autosave_contents)
                fail_e2e("primary or autosave could not be read");
        }
    }
    std::string event_id;
    std::string selected_event_id;
    int event_editor_index = -1;
    std::vector<fabric::project::MapProperty> event_payload_editor;
    std::string event_property_id;
    std::string event_property_value;
    int event_property_kind = 2;
    std::string trigger_id;
    std::string trigger_event_id;
    int trigger_collision_index = 0;
    int selected_collision_index = -1;
    int collision_editor_index = -1;
    fabric::project::CollisionShape collision_editor;
    std::string new_collision_layer;
    int new_collision_kind = 0;
    bool new_collision_sensor = true;
    fabric::core::Vec2 new_collision_center{};
    float new_collision_radius = 1.0F;
    float new_collision_length = 2.0F;
    int selected_trigger_index = -1;
    int trigger_editor_index = -1;
    int trigger_editor_collision_index = 0;
    fabric::project::TriggerDefinition trigger_editor;
    std::string trigger_property_id;
    std::string trigger_property_value;
    int trigger_property_kind = 2;
    std::vector<std::string> selected_instances;
    std::string active_layer_id;
    std::string new_layer_id;
    std::string new_layer_name;
    int new_layer_kind = 1;
    bool placement_mode = false;
    std::string placement_id;
    std::string placement_resource_id;
    int placement_kind = 0;
    std::string selected_prefab;
    std::string new_prefab_id;
    std::string new_prefab_entity;
    std::string new_prefab_mechanic;
    std::string override_id;
    std::string override_value;
    int override_kind = 2;
    std::string mechanic_override_parameter;
    std::string mechanic_override_value;
    std::string instance_property_id;
    std::string instance_property_value;
    int instance_property_kind = 2;
    std::string transformation_preview_id;
    std::string transformation_preview_result;
    ImVec2 canvas_pan{0.0F, 0.0F};
    float canvas_zoom = 1.0F;
    CanvasGizmoState canvas_gizmo;
    CollisionPointGizmoState collision_point_gizmo;
    SelectionBoxState selection_box;
    fabric::editor::MapSnapSettings canvas_snapping;
    TransformEditorState transform_editor;
    float preview_time = 0.0F;
    bool preview_playing = true;
    fabric::render::MapPreviewResult map_preview;
    MapPreviewRenderState preview_render_state{
        .renderer = &map_renderer,
        .textures = &map_textures,
    };
    bool running = true;
    while (running) {
        if (e2e_mode && !e2e_failed && !e2e_event_injected) {
            SDL_Event close_event{};
            if (*e2e_mode == CloseE2eMode::window) {
                close_event.type = SDL_WINDOWEVENT;
                close_event.window.event = SDL_WINDOWEVENT_CLOSE;
                close_event.window.windowID = SDL_GetWindowID(window);
            } else {
                close_event.type = SDL_QUIT;
            }
            if (SDL_PushEvent(&close_event) < 0)
                fail_e2e("close event could not be injected");
            e2e_event_injected = true;
        }
        if (e2e_mode && e2e_failed) running = false;
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                transition_guard.request(
                    fabric::editor::SessionAction::quit,
                    session.dirty() || mechanic_session.dirty() ||
                        scene_session.dirty());
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (session.update_autosave() == fabric::editor::AutosaveStatus::failed)
            status = "Map autosave failed";
        if (mechanic_session.update_autosave() ==
            fabric::editor::AutosaveStatus::failed)
            status = "Mechanic autosave failed";
        if (scene_session.update_autosave() ==
            fabric::editor::AutosaveStatus::failed)
            status = "Scene autosave failed";
        if (mechanic_session.simulation().playing())
            static_cast<void>(mechanic_session.update_preview(ImGui::GetIO().DeltaTime));
        ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Map Studio", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse);
        if (!session.has_map()) {
            ImGui::TextUnformatted("Map Studio");
            ImGui::TextDisabled("Select a map or create one without restarting the studio.");
            const auto manifest = fabric::project::load_manifest(project_root);
            if (manifest.ok()) {
                const auto maps_directory = project_root / manifest.manifest->directories.maps;
                draw_resource_picker("Open map:", maps_directory, ".map.json", open_map_id);
                ImGui::SameLine();
                ImGui::BeginDisabled(open_map_id.empty());
                if (ImGui::Button("Open selected")) {
                    status = session.open(project_root, {.value = open_map_id})
                        ? "Map opened" : "Map could not be opened";
                }
                ImGui::EndDisabled();
                draw_disabled_reason(open_map_id.empty(),
                                     "Choose an existing map first.");
                ImGui::InputText("New map id", &new_map_id);
                draw_field_errors(session.errors(), "id",
                                  "Use a unique non-empty resource id.");
                ImGui::InputText("New map name", &new_map_name);
                draw_field_errors(session.errors(), "name",
                                  "Enter a visible non-empty map name.");
                ImGui::BeginDisabled(new_map_id.empty() || new_map_name.empty());
                if (ImGui::Button("Create map")) {
                    const fabric::project::MapDocument map{
                        .document = {.schema_version = 1, .type = "map",
                                     .id = {.value = new_map_id}, .name = new_map_name}};
                    status = session.create(project_root, map)
                        ? "Map created" : "Map creation failed";
                }
                ImGui::EndDisabled();
                draw_disabled_reason(new_map_id.empty() || new_map_name.empty(),
                                     "Enter both a map id and a map name.");
            }
            draw_errors(session);
        } else {
            if (session.has_recovery()) {
                ImGui::TextColored({1.0F, 0.75F, 0.25F, 1.0F},
                                   "A newer valid map autosave is available.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Recover")) {
                    status = session.accept_recovery()
                        ? "Map autosave recovered; save to publish it"
                        : "Map recovery failed";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Dismiss recovery")) {
                    session.decline_recovery();
                    status = "Map recovery dismissed";
                }
            }
            const auto& map = *session.map();
            if (active_layer_id.empty() && !map.layers.empty())
                active_layer_id = map.layers.front().id;
            ImGui::Text("Map: %s (%s)", map.document.name.c_str(),
                        map.document.id.value.c_str());
            ImGui::SameLine();
            ImGui::TextColored(session.dirty() ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
                                               : ImVec4{0.45F, 0.9F, 0.55F, 1.0F},
                               session.dirty() ? "dirty" : "saved");
            if (ImGui::Button("Save")) status = session.save() ? "Map saved" : "Save failed";
            ImGui::SameLine();
            if (ImGui::Button("Preview")) {
                preview_time = 0.0F;
                preview_playing = true;
                status = "Map preview restarted";
            }
            ImGui::SameLine();
            if (ImGui::Button("Validate")) {
                if (prepare_package()) {
                    const auto validation = fabric::project::plan_map_package(
                        session.project_root(), map.document.id);
                    package_errors = validation.errors;
                    status = validation.ok() ? "Map package validated"
                                             : "Map validation failed";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Publish")) {
                if (prepare_package()) {
                    const auto parent = choose_folder(window, status);
                    if (parent) {
                        const auto destination = *parent /
                            (map.document.id.value + ".map-package");
                        const auto published = fabric::project::publish_map_package(
                            session.project_root(), map.document.id, destination);
                        package_errors = published.errors;
                        status = published.ok()
                            ? "Map package published: " + destination.string()
                            : "Map package publication failed";
                    }
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.can_undo());
            if (ImGui::Button("Undo")) static_cast<void>(session.undo());
            ImGui::EndDisabled();
            draw_disabled_reason(!session.can_undo(),
                                 "No map changes are available to undo.");
            ImGui::SameLine();
            ImGui::BeginDisabled(!session.can_redo());
            if (ImGui::Button("Redo")) static_cast<void>(session.redo());
            ImGui::EndDisabled();
            draw_disabled_reason(!session.can_redo(),
                                 "No undone map changes are available to redo.");
            ImGui::Separator();
            ImGui::Columns(2, "map-studio-columns", true);
            ImGui::Text("Layers (%zu)", map.layers.size());
            ImGui::SetNextItemWidth(120.0F);
            ImGui::InputText("Layer id", &new_layer_id);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0F);
            ImGui::InputText("Layer name", &new_layer_name);
            ImGui::SetNextItemWidth(150.0F);
            ImGui::Combo("Layer kind", &new_layer_kind,
                         "visual\0instances\0collision\0triggers\0");
            ImGui::SameLine();
            ImGui::BeginDisabled(new_layer_id.empty() || new_layer_name.empty());
            if (ImGui::Button("Add layer")) {
                const auto added = session.add_layer({
                    new_layer_id, new_layer_name,
                    std::array{fabric::project::MapLayerKind::visual,
                               fabric::project::MapLayerKind::instances,
                               fabric::project::MapLayerKind::collision,
                               fabric::project::MapLayerKind::triggers}[static_cast<std::size_t>(new_layer_kind)],
                    true, false, 0.0F});
                status = added ? "Layer added" : "Layer creation rejected";
                if (added) {
                    active_layer_id = new_layer_id;
                    new_layer_id.clear();
                    new_layer_name.clear();
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(new_layer_id.empty() || new_layer_name.empty(),
                                 "Enter both a layer id and a layer name.");
            bool layer_changed = false;
            for (std::size_t layer_index = 0; layer_index < map.layers.size(); ++layer_index) {
                const auto layer = map.layers[layer_index];
                ImGui::PushID(layer.id.c_str());
                bool visible = layer.visible;
                if (ImGui::Checkbox("##visible", &visible) &&
                    session.set_layer_visibility({.value = layer.id}, visible)) {
                    status = "Layer visibility changed";
                    layer_changed = true;
                }
                ImGui::SameLine();
                bool locked = layer.locked;
                if (ImGui::Checkbox("##locked", &locked) &&
                    session.set_layer_locked({.value = layer.id}, locked)) {
                    status = "Layer lock changed";
                    layer_changed = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(layer.name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(active_layer_id == layer.id ? "Active" : "Use"))
                    active_layer_id = layer.id;
                ImGui::SameLine();
                float depth = layer.depth;
                ImGui::SetNextItemWidth(90.0F);
                if (ImGui::DragFloat("##depth", &depth, 0.1F) &&
                    ImGui::IsItemDeactivatedAfterEdit() &&
                    session.set_layer_depth({.value = layer.id}, depth)) {
                    status = "Layer depth changed";
                    layer_changed = true;
                }
                ImGui::PopID();
                if (layer_changed) break;
            }
            if (layer_changed) ImGui::TextDisabled("Layer edit recorded in undo history");
            ImGui::SeparatorText("Content");
            ImGui::Text("Instances: %zu", map.instances.size());
            for (const auto& instance : map.instances) {
                bool selected = std::find(selected_instances.begin(), selected_instances.end(),
                                          instance.id) != selected_instances.end();
                if (ImGui::Checkbox((instance.id + "##selected").c_str(), &selected)) {
                    if (selected) selected_instances.push_back(instance.id);
                    else selected_instances.erase(std::remove(selected_instances.begin(),
                                                               selected_instances.end(), instance.id),
                                                  selected_instances.end());
                }
            }
            ImGui::BeginDisabled(selected_instances.empty());
            if (ImGui::Button("Nudge selected +1")) {
                std::vector<fabric::core::ResourceId> ids;
                for (const auto& id : selected_instances) ids.push_back({.value = id});
                status = session.translate_instances(ids, {1.0F, 0.0F})
                    ? "Selected instances moved" : "Selection contains a locked instance";
            }
            if (selected_instances.size() == 1U) {
                ImGui::SameLine();
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                if (ImGui::Button("Duplicate selected")) {
                    status = session.duplicate_instance(selected_id)
                        ? "Instance duplicated" : "Instance duplication rejected";
                }
                ImGui::SameLine();
                if (ImGui::Button("Move selected to front")) {
                    status = session.reorder_instance(selected_id, 0U)
                        ? "Instance reordered" : "Instance reorder rejected";
                }
            }
            ImGui::EndDisabled();
            draw_disabled_reason(selected_instances.empty(),
                                 "Select at least one instance first.");
            ImGui::BeginDisabled(selected_instances.empty() || active_layer_id.empty());
            if (ImGui::Button("Move selected to active layer")) {
                std::vector<fabric::core::ResourceId> ids;
                for (const auto& id : selected_instances) ids.push_back({.value = id});
                status = session.set_instances_layer(ids, {.value = active_layer_id})
                    ? "Instances moved to active layer"
                    : "Layer move rejected (locked or invalid)";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(selected_instances.empty() || active_layer_id.empty(),
                                 "Select an instance and an active layer first.");
            ImGui::SeparatorText("Placement");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("New instance id", &placement_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::Combo("Resource kind", &placement_kind, "entity\0prefab\0");
            if (placement_kind == 0 && session.manifest()) {
                const auto directory = session.project_root() /
                    session.manifest()->directories.entities;
                draw_resource_picker("Entity resources:", directory, ".entity.json",
                                     placement_resource_id);
            } else {
                ImGui::InputText("Prefab resource id", &placement_resource_id);
            }
            ImGui::BeginDisabled(placement_id.empty() || placement_resource_id.empty() ||
                                 active_layer_id.empty());
            if (ImGui::Button(placement_mode ? "Cancel placement" : "Place in canvas"))
                placement_mode = !placement_mode;
            ImGui::EndDisabled();
            draw_disabled_reason(placement_id.empty() || placement_resource_id.empty() ||
                                     active_layer_id.empty(),
                                 "Enter an instance id, choose a resource and select an active layer.");
            ImGui::Checkbox("Play visual animation", &preview_playing);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0F);
            ImGui::SliderFloat("Preview time", &preview_time, 0.0F, 10.0F, "%.2f s");
            if (preview_playing) preview_time += ImGui::GetIO().DeltaTime;
            map_preview = fabric::render::resolve_map_preview(
                session.project_root(), *session.manifest(), map, preview_time);
            preview_render_state.preview = &map_preview;
            preview_render_state.project_root = &session.project_root();
            preview_render_state.manifest = &*session.manifest();
            draw_map_canvas(session, selected_instances, canvas_pan, canvas_zoom, canvas_gizmo,
                            selected_collision_index, collision_point_gizmo,
                            selected_trigger_index, active_layer_id, selection_box,
                            placement_mode,
                            placement_id, placement_resource_id, placement_kind,
                            canvas_snapping, preview_render_state,
                            mechanic_session.simulation(), status);
            for (const auto& error : map_preview.errors)
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F}, "%s", error.c_str());
            draw_transform_editor(session, selected_instances, transform_editor, status);
            ImGui::Text("Collisions: %zu", map.collisions.size());
            ImGui::SetNextItemWidth(150.0F);
            ImGui::Combo("New collision kind", &new_collision_kind,
                         "circle\0capsule\0polygon\0chain\0");
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Collision layer", &new_collision_layer);
            ImGui::TextDisabled("Collision layers:");
            for (const auto& layer : map.layers) {
                if (layer.kind != fabric::project::MapLayerKind::collision)
                    continue;
                ImGui::SameLine();
                if (ImGui::SmallButton(layer.id.c_str()))
                    new_collision_layer = layer.id;
            }
            ImGui::Checkbox("New collision sensor", &new_collision_sensor);
            if (new_collision_kind == 3) {
                new_collision_sensor = false;
                ImGui::SameLine();
                ImGui::TextDisabled("chains are solid and cannot be triggers");
            }
            ImGui::SetNextItemWidth(220.0F);
            ImGui::DragFloat2("New collision center",
                              &new_collision_center.x, 0.1F);
            if (new_collision_kind <= 1) {
                ImGui::SetNextItemWidth(180.0F);
                ImGui::DragFloat("New collision radius", &new_collision_radius,
                                 0.1F, 0.01F, 4096.0F);
            }
            if (new_collision_kind == 1) {
                ImGui::SetNextItemWidth(180.0F);
                ImGui::DragFloat("New capsule length", &new_collision_length,
                                 0.1F, 0.0F, 4096.0F);
            }
            ImGui::BeginDisabled(new_collision_layer.empty() ||
                                 new_collision_radius <= 0.0F);
            if (ImGui::Button("Add collision")) {
                fabric::project::CollisionShape shape{
                    .kind = static_cast<fabric::project::CollisionShapeKind>(
                        new_collision_kind),
                    .layer_id = new_collision_layer,
                    .sensor = new_collision_sensor,
                    .center = new_collision_center,
                    .radius = new_collision_radius,
                    .length = new_collision_length};
                if (shape.kind ==
                    fabric::project::CollisionShapeKind::polygon)
                    shape.points = {
                        {new_collision_center.x - 1.0F,
                         new_collision_center.y - 1.0F},
                        {new_collision_center.x + 1.0F,
                         new_collision_center.y - 1.0F},
                        {new_collision_center.x,
                         new_collision_center.y + 1.0F}};
                else if (shape.kind ==
                         fabric::project::CollisionShapeKind::chain)
                    shape.points = {
                        {new_collision_center.x - 1.0F,
                         new_collision_center.y},
                        {new_collision_center.x + 1.0F,
                         new_collision_center.y}};
                const auto added = session.add_collision_shape(
                    std::move(shape));
                status = added ? "Collision added"
                               : "Collision creation rejected";
                if (added)
                    selected_collision_index = static_cast<int>(
                        session.map()->collisions.size() - 1U);
            }
            ImGui::EndDisabled();
            draw_disabled_reason(new_collision_layer.empty() ||
                                     new_collision_radius <= 0.0F,
                                 "Choose a collision layer and enter a positive radius.");
            for (std::size_t collision_index = 0; collision_index < map.collisions.size();
                 ++collision_index) {
                const auto& collision = map.collisions[collision_index];
                ImGui::PushID(static_cast<int>(collision_index));
                const auto label = "[" + std::to_string(collision_index) + "] " +
                                   collision_shape_text(collision) + " / layer " +
                                   collision.layer_id;
                if (ImGui::Selectable(label.c_str(),
                                      selected_collision_index ==
                                          static_cast<int>(collision_index))) {
                    selected_collision_index = static_cast<int>(collision_index);
                }
                ImGui::PopID();
            }
            if (selected_collision_index >= 0 &&
                static_cast<std::size_t>(selected_collision_index) < map.collisions.size()) {
                if (collision_editor_index != selected_collision_index) {
                    collision_editor_index = selected_collision_index;
                    collision_editor = map.collisions[static_cast<std::size_t>(
                        selected_collision_index)];
                }
                ImGui::SeparatorText("Selected collision");
                ImGui::Text("Layer: %s", collision_editor.layer_id.c_str());
                ImGui::Text("Kind: %s", collision_shape_text(collision_editor).c_str());
                ImGui::Checkbox("Sensor", &collision_editor.sensor);
                ImGui::SetNextItemWidth(220.0F);
                ImGui::DragFloat2("Center", &collision_editor.center.x, 0.1F);
                if (collision_editor.kind == fabric::project::CollisionShapeKind::circle ||
                    collision_editor.kind == fabric::project::CollisionShapeKind::capsule) {
                    ImGui::SetNextItemWidth(220.0F);
                    ImGui::DragFloat("Radius", &collision_editor.radius, 0.1F, 0.0F,
                                     4096.0F);
                }
                if (collision_editor.kind == fabric::project::CollisionShapeKind::capsule) {
                    ImGui::SetNextItemWidth(220.0F);
                    ImGui::DragFloat("Length", &collision_editor.length, 0.1F, 0.0F,
                                     4096.0F);
                }
                if (collision_editor.kind == fabric::project::CollisionShapeKind::polygon ||
                    collision_editor.kind == fabric::project::CollisionShapeKind::chain) {
                    ImGui::Text("Points: %zu", collision_editor.points.size());
                    for (std::size_t point_index = 0;
                         point_index < collision_editor.points.size(); ++point_index) {
                        ImGui::PushID(static_cast<int>(point_index));
                        ImGui::SetNextItemWidth(220.0F);
                        ImGui::DragFloat2("Point", &collision_editor.points[point_index].x,
                                          0.1F);
                        ImGui::PopID();
                    }
                    const auto minimum_points = collision_editor.kind ==
                        fabric::project::CollisionShapeKind::polygon ? 3U : 2U;
                    ImGui::BeginDisabled(collision_editor.points.size() <= minimum_points);
                    if (ImGui::Button("Remove last point")) collision_editor.points.pop_back();
                    ImGui::EndDisabled();
                    draw_disabled_reason(collision_editor.points.size() <= minimum_points,
                                         "The shape must keep its minimum number of points.");
                    ImGui::SameLine();
                    if (ImGui::Button("Add point")) collision_editor.points.push_back({});
                }
                if (ImGui::Button("Apply collision")) {
                    const auto applied = session.set_collision_shape(
                        static_cast<std::size_t>(selected_collision_index), collision_editor);
                    status = applied ? "Collision updated" :
                                       "Collision update rejected (layer locked or invalid)";
                }
            }
            ImGui::Text("Triggers: %zu", map.triggers.size());
            ImGui::Text("Events: %zu", map.events.size());
            ImGui::NextColumn();
            ImGui::SeparatorText("Events");
            ImGui::SetNextItemWidth(250.0F);
            ImGui::InputText("New event id", &event_id);
            ImGui::SameLine();
            ImGui::BeginDisabled(event_id.empty());
            if (ImGui::Button("Declare")) {
                if (session.declare_event({{.value = event_id}, {}})) {
                    status = "Event declared";
                    event_id.clear();
                } else status = "Event declaration rejected";
            }
            ImGui::EndDisabled();
            draw_disabled_reason(event_id.empty(),
                                 "Enter an event id before declaring it.");
            for (const auto& event_definition : map.events) {
                const auto selected = selected_event_id == event_definition.id.value;
                if (ImGui::Selectable(event_definition.id.value.c_str(), selected))
                    selected_event_id = event_definition.id.value;
            }
            if (!selected_event_id.empty()) {
                const auto event_definition = std::find_if(
                    map.events.begin(), map.events.end(), [&](const auto& event) {
                        return event.id.value == selected_event_id;
                    });
                if (event_definition != map.events.end()) {
                    if (event_editor_index != static_cast<int>(
                            std::distance(map.events.begin(), event_definition))) {
                        event_editor_index = static_cast<int>(
                            std::distance(map.events.begin(), event_definition));
                        event_payload_editor = event_definition->payload;
                    }
                    ImGui::SeparatorText("Selected event payload");
                    for (std::size_t property_index = 0;
                         property_index < event_payload_editor.size(); ++property_index) {
                        ImGui::PushID(static_cast<int>(property_index));
                        ImGui::BulletText("%s = %s", event_payload_editor[property_index].id.c_str(),
                                          property_value_text(
                                              event_payload_editor[property_index].value).c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove"))
                            event_payload_editor.erase(event_payload_editor.begin() +
                                                       static_cast<std::ptrdiff_t>(property_index));
                        ImGui::PopID();
                        if (property_index >= event_payload_editor.size()) break;
                    }
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::InputText("Payload property id", &event_property_id);
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::Combo("Payload type", &event_property_kind,
                                 "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                    ImGui::SetNextItemWidth(180.0F);
                    ImGui::InputText("Payload value", &event_property_value);
                    ImGui::BeginDisabled(event_property_id.empty() ||
                                         event_property_value.empty());
                    if (ImGui::Button("Apply payload property")) {
                        const auto value = parse_override_value(event_property_kind,
                                                                 event_property_value);
                        if (value) {
                            const auto existing = std::find_if(
                                event_payload_editor.begin(), event_payload_editor.end(),
                                [&](const auto& property) {
                                    return property.id == event_property_id;
                                });
                            if (existing != event_payload_editor.end()) existing->value = *value;
                            else event_payload_editor.push_back({event_property_id, *value});
                            const auto applied = session.set_event_payload(
                                {.value = selected_event_id}, event_payload_editor);
                            status = applied ? "Event payload updated" :
                                               "Event payload rejected";
                            if (applied) {
                                event_property_id.clear();
                                event_property_value.clear();
                            }
                        } else status = "Event payload value rejected";
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::SeparatorText("Triggers");
            for (std::size_t trigger_index = 0; trigger_index < map.triggers.size();
                 ++trigger_index) {
                const auto& trigger = map.triggers[trigger_index];
                ImGui::PushID(static_cast<int>(trigger_index));
                const auto label = trigger.id + " -> " + trigger.event_id.value +
                                   " (collision " + std::to_string(trigger.collision_index) + ")";
                if (ImGui::Selectable(label.c_str(), selected_trigger_index ==
                                      static_cast<int>(trigger_index)))
                    selected_trigger_index = static_cast<int>(trigger_index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    status = session.remove_trigger({.value = trigger.id})
                        ? "Trigger removed" : "Trigger removal rejected";
                }
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("New trigger id", &trigger_id);
            ImGui::SetNextItemWidth(180.0F);
            if (map.events.empty()) {
                ImGui::TextDisabled("Trigger event: declare an event first");
                trigger_event_id.clear();
            } else {
                std::vector<const char*> event_labels;
                event_labels.reserve(map.events.size());
                for (const auto& event : map.events)
                    event_labels.push_back(event.id.value.c_str());
                int selected_event = 0;
                for (std::size_t index = 0; index < map.events.size(); ++index)
                    if (map.events[index].id.value == trigger_event_id)
                        selected_event = static_cast<int>(index);
                if (ImGui::Combo("Trigger event", &selected_event,
                                event_labels.data(), static_cast<int>(event_labels.size())))
                    trigger_event_id = map.events[static_cast<std::size_t>(selected_event)].id.value;
            }
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputInt("Collision index", &trigger_collision_index);
            const auto valid_trigger_collision = [&](const int index) {
                if (index < 0 || static_cast<std::size_t>(index) >=
                                     map.collisions.size())
                    return false;
                const auto& collision =
                    map.collisions[static_cast<std::size_t>(index)];
                return collision.sensor && collision.kind !=
                    fabric::project::CollisionShapeKind::chain;
            };
            if (!valid_trigger_collision(trigger_collision_index))
                ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                   "Choose a closed sensor collision");
            ImGui::BeginDisabled(trigger_id.empty() || trigger_event_id.empty() ||
                                 !valid_trigger_collision(
                                     trigger_collision_index));
            if (ImGui::Button("Add trigger")) {
                const auto added = session.add_trigger(
                    {trigger_id, "triggers", static_cast<std::size_t>(trigger_collision_index),
                     {.value = trigger_event_id}, {}});
                status = added ? "Trigger added" : "Trigger rejected";
                if (added) {
                    trigger_id.clear();
                    trigger_event_id.clear();
                    trigger_collision_index = 0;
                }
            }
            ImGui::EndDisabled();
            if (selected_trigger_index >= 0 &&
                static_cast<std::size_t>(selected_trigger_index) < map.triggers.size()) {
                if (trigger_editor_index != selected_trigger_index) {
                    trigger_editor_index = selected_trigger_index;
                    trigger_editor = map.triggers[static_cast<std::size_t>(selected_trigger_index)];
                    trigger_editor_collision_index =
                        static_cast<int>(trigger_editor.collision_index);
                }
                ImGui::SeparatorText("Selected trigger");
                ImGui::Text("Id: %s", trigger_editor.id.c_str());
                ImGui::SetNextItemWidth(220.0F);
                if (map.events.empty()) {
                    ImGui::TextDisabled("Event: no declared events");
                } else {
                    std::vector<const char*> event_labels;
                    event_labels.reserve(map.events.size());
                    for (const auto& event : map.events)
                        event_labels.push_back(event.id.value.c_str());
                    int selected_event = 0;
                    for (std::size_t index = 0; index < map.events.size(); ++index)
                        if (map.events[index].id == trigger_editor.event_id)
                            selected_event = static_cast<int>(index);
                    if (ImGui::Combo("Event", &selected_event, event_labels.data(),
                                    static_cast<int>(event_labels.size())))
                        trigger_editor.event_id = map.events[static_cast<std::size_t>(selected_event)].id;
                }
                ImGui::SetNextItemWidth(220.0F);
                ImGui::InputInt("Collision index", &trigger_editor_collision_index);
                const auto event_definition = std::find_if(
                    map.events.begin(), map.events.end(), [&](const auto& event) {
                        return event.id == trigger_editor.event_id;
                    });
                if (event_definition != map.events.end()) {
                    ImGui::Text("Event payload:");
                    for (const auto& property : event_definition->payload)
                        ImGui::BulletText("%s = %s", property.id.c_str(),
                                          property_value_text(property.value).c_str());
                }
                ImGui::Text("Trigger overrides:");
                for (std::size_t property_index = 0;
                     property_index < trigger_editor.properties.size();
                     ++property_index) {
                    const auto property = trigger_editor.properties[property_index];
                    ImGui::PushID(static_cast<int>(property_index));
                    ImGui::BulletText("%s = %s", property.id.c_str(),
                                      property_value_text(property.value).c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove override")) {
                        trigger_editor.properties.erase(
                            trigger_editor.properties.begin() +
                            static_cast<std::ptrdiff_t>(property_index));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Trigger property id", &trigger_property_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Trigger property type", &trigger_property_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Trigger property value",
                                 &trigger_property_value);
                ImGui::BeginDisabled(trigger_property_id.empty() ||
                                     trigger_property_value.empty());
                if (ImGui::Button("Set trigger override")) {
                    const auto value = parse_override_value(
                        trigger_property_kind, trigger_property_value);
                    if (value) {
                        const auto existing = std::ranges::find(
                            trigger_editor.properties, trigger_property_id,
                            &fabric::project::MapProperty::id);
                        if (existing == trigger_editor.properties.end())
                            trigger_editor.properties.push_back(
                                {trigger_property_id, *value});
                        else
                            existing->value = *value;
                        trigger_property_id.clear();
                        trigger_property_value.clear();
                    } else {
                        status = "Trigger property value rejected";
                    }
                }
                ImGui::EndDisabled();
                if (!valid_trigger_collision(trigger_editor_collision_index))
                    ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                                       "Trigger collision must be a closed sensor");
                ImGui::BeginDisabled(
                                     !valid_trigger_collision(
                                         trigger_editor_collision_index) ||
                                     trigger_editor.event_id.value.empty());
                if (ImGui::Button("Apply trigger")) {
                    trigger_editor.collision_index = static_cast<std::size_t>(
                        trigger_editor_collision_index);
                    const auto applied = session.set_trigger(
                        static_cast<std::size_t>(selected_trigger_index), trigger_editor);
                    status = applied ? "Trigger updated" :
                                       "Trigger update rejected (event, collision or layer)";
                }
                ImGui::EndDisabled();
            }
            if (selected_instances.size() == 1U) {
                const fabric::core::ResourceId selected_id{selected_instances.front()};
                ImGui::SeparatorText("Selected instance properties");
                const auto instance = std::ranges::find(
                    map.instances, selected_id.value,
                    &fabric::project::MapInstance::id);
                std::optional<fabric::core::ResourceId> selected_entity;
                if (instance != map.instances.end()) {
                    if (instance->entity) selected_entity = instance->entity->id;
                    else if (instance->prefab) {
                        const auto prefab = std::ranges::find(
                            map.prefabs, instance->prefab->id.value,
                            &fabric::project::PrefabDefinition::id);
                        if (prefab != map.prefabs.end())
                            selected_entity = prefab->entity.id;
                    }
                }
                const auto transformations = load_transformations(
                    session.project_root(), *session.manifest());
                const auto selected_transformation = std::ranges::find_if(
                    transformations, [&](const auto& value) {
                        return value.document.id.value == transformation_preview_id;
                    });
                const char* transformation_label =
                    selected_transformation == transformations.end()
                    ? "Choose a compatible transformation..."
                    : selected_transformation->document.name.c_str();
                if (ImGui::BeginCombo("Transformation preview",
                                      transformation_label)) {
                    for (const auto& value : transformations) {
                        if (!selected_entity ||
                            value.source_entity.id != *selected_entity) continue;
                        if (ImGui::Selectable(value.document.name.c_str(),
                                value.document.id.value ==
                                    transformation_preview_id))
                            transformation_preview_id = value.document.id.value;
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s -> %s",
                            value.source_entity.id.value.c_str(),
                            value.destination_entity.id.value.c_str());
                    }
                    ImGui::EndCombo();
                }
                const bool can_preview = !session.dirty() && selected_entity &&
                    selected_transformation != transformations.end() &&
                    selected_transformation->source_entity.id == *selected_entity;
                ImGui::BeginDisabled(!can_preview);
                if (ImGui::Button("Run atomic transformation preview")) {
                    fabric::runtime::PreviewRuntime preview_runtime;
                    const bool loaded = preview_runtime.load({
                        .project_root = session.project_root(),
                        .map_id = map.document.id,
                        .mode = fabric::runtime::RuntimeMode::smoke_test});
                    const bool transformed = loaded &&
                        preview_runtime.transform_instance(
                            selected_id.value,
                            selected_transformation->document.id);
                    const auto result_entity = transformed
                        ? preview_runtime.instance_entity_id(selected_id.value)
                        : std::nullopt;
                    transformation_preview_result = transformed && result_entity
                        ? "Preview: " + selected_entity->value + " -> " +
                            result_entity->value + ", " +
                            std::to_string(preview_runtime.packet_order().size()) +
                            " draw packet(s). The map was not modified."
                        : "Transformation preview failed; the source instance was kept.";
                }
                ImGui::EndDisabled();
                if (session.dirty())
                    ImGui::TextDisabled(
                        "Save the map before running the isolated runtime preview.");
                if (!transformation_preview_result.empty())
                    ImGui::TextWrapped("%s", transformation_preview_result.c_str());
                for (const auto& property : session.effective_instance_properties(selected_id))
                    ImGui::BulletText("%s = %s", property.id.c_str(),
                                      property_value_text(property.value).c_str());
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Instance property id", &instance_property_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Instance type", &instance_property_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Instance value", &instance_property_value);
                ImGui::BeginDisabled(instance_property_id.empty() || instance_property_value.empty());
                if (ImGui::Button("Apply instance property")) {
                    const auto value = parse_override_value(instance_property_kind,
                                                             instance_property_value);
                    auto property = value
                        ? std::optional<fabric::project::MapProperty>{
                              {instance_property_id, *value}}
                        : std::nullopt;
                    if (property && property->id == "animation") {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&property->value))
                            reference->expected_type = "animation";
                        else property.reset();
                    }
                    const auto applied = property && session.set_instance_property(
                        selected_id, std::move(*property));
                    status = applied ? "Instance property applied" : "Instance property rejected";
                    if (applied) {
                        instance_property_id.clear();
                        instance_property_value.clear();
                    }
                }
                ImGui::EndDisabled();
            }
            ImGui::SeparatorText("Prefabs");
            ImGui::SetNextItemWidth(160.0F);
            ImGui::InputText("New prefab id", &new_prefab_id);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Prefab entity", &new_prefab_entity);
            ImGui::SetNextItemWidth(180.0F);
            ImGui::InputText("Prefab mechanic (optional)",
                             &new_prefab_mechanic);
            if (session.manifest()) {
                draw_resource_picker(
                    "Asset Studio entities:", session.project_root() /
                        session.manifest()->directories.entities,
                    ".entity.json", new_prefab_entity);
                draw_resource_picker(
                    "Mechanic graphs:", session.project_root() /
                        session.manifest()->directories.assets / "mechanics",
                    ".mechanic.json", new_prefab_mechanic);
            }
            ImGui::BeginDisabled(new_prefab_id.empty() ||
                                 new_prefab_entity.empty());
            if (ImGui::Button("Create prefab")) {
                fabric::project::PrefabDefinition prefab{
                    .id = new_prefab_id,
                    .entity = {{.value = new_prefab_entity}, "entity"}};
                if (!new_prefab_mechanic.empty())
                    prefab.mechanic = fabric::project::ResourceReference{
                        {.value = new_prefab_mechanic}, "mechanic"};
                const auto created = session.add_prefab(std::move(prefab));
                status = created ? "Prefab created"
                                 : "Prefab creation rejected";
                if (created) {
                    selected_prefab = new_prefab_id;
                    new_prefab_id.clear();
                }
            }
            ImGui::EndDisabled();
            for (const auto& prefab : map.prefabs) {
                const auto selected = selected_prefab == prefab.id;
                if (ImGui::Selectable(prefab.id.c_str(), selected)) selected_prefab = prefab.id;
            }
            if (!selected_prefab.empty()) {
                ImGui::Text("Selected prefab: %s", selected_prefab.c_str());
                const auto prefab = std::ranges::find(
                    map.prefabs, selected_prefab,
                    &fabric::project::PrefabDefinition::id);
                if (prefab != map.prefabs.end() && prefab->mechanic) {
                    ImGui::Text("Mechanic: %s",
                                prefab->mechanic->id.value.c_str());
                    const auto preview_instance = std::ranges::find_if(
                        map.instances, [&](const auto& instance) {
                            return instance.prefab &&
                                   instance.prefab->id.value == selected_prefab;
                        });
                    if (ImGui::Button(preview_instance == map.instances.end()
                            ? "Preview prefab mechanic"
                            : "Preview mechanic instance")) {
                        const auto opened = preview_instance == map.instances.end()
                            ? mechanic_session.open_prefab(
                                  session.project_root(), map,
                                  {.value = selected_prefab})
                            : mechanic_session.open_prefab_instance(
                                  session.project_root(), map,
                                  {.value = preview_instance->id});
                        status = opened ? "Prefab mechanic preview opened"
                                        : "Prefab mechanic preview rejected";
                        if (opened)
                            mechanic_editor.open_id =
                                prefab->mechanic->id.value;
                    }
                    const auto graph = session.prefab_mechanic_graph(
                        {.value = selected_prefab});
                    if (graph) {
                        ImGui::TextDisabled("Mechanic parameters:");
                        for (const auto& parameter : graph->parameters) {
                            const auto selected = mechanic_override_parameter ==
                                                  parameter.id;
                            const auto label = parameter.name + " [" +
                                std::string{fabric::project::to_string(
                                    parameter.type)} + "]##" + parameter.id;
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                mechanic_override_parameter = parameter.id;
                                const auto stored = std::ranges::find(
                                    prefab->mechanic_overrides, parameter.id,
                                    &fabric::project::MechanicParameterOverride::parameter_id);
                                mechanic_override_value = property_value_text(
                                    stored == prefab->mechanic_overrides.end()
                                        ? parameter.default_value : stored->value);
                            }
                        }
                        const auto parameter = std::ranges::find(
                            graph->parameters, mechanic_override_parameter,
                            &fabric::project::MechanicParameterDefinition::id);
                        if (parameter != graph->parameters.end()) {
                            ImGui::SetNextItemWidth(180.0F);
                            ImGui::InputText("Mechanic value",
                                             &mechanic_override_value);
                            if (ImGui::Button("Apply mechanic override")) {
                                const auto value = parse_mechanic_override_value(
                                    *parameter, mechanic_override_value);
                                const auto applied = value &&
                                    session.set_prefab_mechanic_override(
                                        {.value = selected_prefab},
                                        {parameter->id, *value});
                                status = applied
                                    ? "Mechanic override applied"
                                    : "Mechanic override rejected by parameter type";
                                if (applied && mechanic_session.previewing_prefab(
                                                   {.value = selected_prefab})) {
                                    const auto refreshed_map = *session.map();
                                    const auto refreshed_instance = std::ranges::find_if(
                                        refreshed_map.instances,
                                        [&](const auto& instance) {
                                            return instance.prefab &&
                                                instance.prefab->id.value ==
                                                    selected_prefab;
                                        });
                                    if (refreshed_instance ==
                                        refreshed_map.instances.end())
                                        static_cast<void>(mechanic_session.open_prefab(
                                            session.project_root(), refreshed_map,
                                            {.value = selected_prefab}));
                                    else
                                        static_cast<void>(
                                            mechanic_session.open_prefab_instance(
                                                session.project_root(), refreshed_map,
                                                {.value = refreshed_instance->id}));
                                }
                            }
                        }
                    }
                }
                ImGui::SeparatorText("Visual/property overrides");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Property id", &override_id);
                ImGui::SetNextItemWidth(180.0F);
                ImGui::Combo("Type", &override_kind,
                             "bool\0integer\0real\0text\0Vec2 (x,y)\0resource\0");
                ImGui::SetNextItemWidth(180.0F);
                ImGui::InputText("Value", &override_value);
                ImGui::BeginDisabled(override_id.empty() || override_value.empty());
                if (ImGui::Button("Apply override")) {
                    const auto value = parse_override_value(override_kind, override_value);
                    auto property = value
                        ? std::optional<fabric::project::MapProperty>{
                              {override_id, *value}}
                        : std::nullopt;
                    if (property && property->id == "animation") {
                        if (auto* reference = std::get_if<
                                fabric::project::ResourceReference>(&property->value))
                            reference->expected_type = "animation";
                        else property.reset();
                    }
                    const auto applied = property && session.set_prefab_override(
                        {.value = selected_prefab}, std::move(*property));
                    status = applied ? "Prefab override applied" : "Prefab override rejected";
                    if (applied) {
                        override_id.clear();
                        override_value.clear();
                    }
                }
                ImGui::EndDisabled();
            }
            ImGui::Columns(1);
            if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
            draw_package_errors(package_errors);
            draw_errors(session);
        }
        ImGui::End();
        draw_mechanic_editor(mechanic_session, session, mechanic_editor, status);
        draw_scene_editor(scene_session, project_root, window, scene_editor,
                          status, package_errors);

        if (const auto ready = transition_guard.take_ready();
            ready == fabric::editor::SessionAction::quit) {
            running = false;
        }
        if (transition_guard.confirmation_required())
            ImGui::OpenPopup("Unsaved Map Studio documents");
        if (ImGui::BeginPopupModal("Unsaved Map Studio documents", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save changes before closing Map Studio?");
            if (session.dirty())
                ImGui::BulletText("Map: %s",
                                  session.map()
                                      ? session.map()->document.name.c_str()
                                      : "current map");
            if (mechanic_session.dirty())
                ImGui::BulletText("Mechanic: %s",
                                  mechanic_session.graph()
                                      ? mechanic_session.graph()->document.name.c_str()
                                      : "current mechanic");
            if (scene_session.dirty())
                ImGui::BulletText("Scene: %s",
                                  scene_session.scene()
                                      ? scene_session.scene()->document.name.c_str()
                                      : "current scene");
            if (e2e_mode && !e2e_modal_handled) {
                if (!transition_guard.confirmation_required() ||
                    !session.dirty() ||
                    read_binary_file(e2e_primary_path) != e2e_primary_contents ||
                    read_binary_file(e2e_autosave_path) != e2e_autosave_contents) {
                    fail_e2e("close request changed the active document or autosave");
                } else if (*e2e_mode == CloseE2eMode::save) {
                    const bool saved = session.save();
                    if (!saved || session.dirty() ||
                        transition_guard.resolve(
                            fabric::editor::UnsavedDecision::save, true) !=
                            fabric::editor::SessionAction::quit)
                        fail_e2e("Save did not finish the close request");
                    running = false;
                } else if (*e2e_mode != CloseE2eMode::save_failure) {
                    static_cast<void>(transition_guard.resolve(
                        fabric::editor::UnsavedDecision::cancel));
                    if (transition_guard.pending() || !session.dirty())
                        fail_e2e("Cancel did not preserve the dirty session");
                    transition_guard.request(
                        fabric::editor::SessionAction::quit, session.dirty());
                    if (transition_guard.resolve(
                            fabric::editor::UnsavedDecision::discard) !=
                        fabric::editor::SessionAction::quit)
                        fail_e2e("Discard did not finish the close request");
                    running = false;
                } else {
                    const auto backup = std::filesystem::path{
                        e2e_primary_path.string() + ".e2e-backup"};
                    std::error_code filesystem_error;
                    std::filesystem::rename(
                        e2e_primary_path, backup, filesystem_error);
                    const bool primary_backed_up = !filesystem_error;
                    if (primary_backed_up)
                        std::filesystem::create_directory(
                            e2e_primary_path, filesystem_error);
                    if (filesystem_error) {
                        fail_e2e("save failure could not be prepared");
                    } else {
                        const bool saved = session.save();
                        static_cast<void>(transition_guard.resolve(
                            fabric::editor::UnsavedDecision::save, saved));
                        if (saved || !transition_guard.confirmation_required() ||
                            !transition_guard.pending() || !session.dirty() ||
                            read_binary_file(backup) != e2e_primary_contents ||
                            read_binary_file(e2e_autosave_path) !=
                                e2e_autosave_contents)
                            fail_e2e("Save failure did not preserve recovery state");
                    }
                    if (primary_backed_up) {
                        filesystem_error.clear();
                        std::filesystem::remove(
                            e2e_primary_path, filesystem_error);
                        if (!filesystem_error)
                            std::filesystem::rename(
                                backup, e2e_primary_path, filesystem_error);
                    }
                    if (!primary_backed_up || filesystem_error ||
                        read_binary_file(e2e_primary_path) !=
                            e2e_primary_contents)
                        fail_e2e("primary document could not be restored");
                    if (!e2e_failed &&
                        transition_guard.resolve(
                            fabric::editor::UnsavedDecision::discard) !=
                            fabric::editor::SessionAction::quit)
                        fail_e2e("failed Save did not keep the close decision open");
                    running = false;
                }
                e2e_modal_handled = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Retry save and continue", {170.0F, 0.0F})) {
                if (save_dirty_documents()) {
                    ImGui::CloseCurrentPopup();
                    if (transition_guard.resolve(
                        fabric::editor::UnsavedDecision::save) ==
                        fabric::editor::SessionAction::quit) {
                        running = false;
                    }
                } else {
                    static_cast<void>(transition_guard.resolve(
                        fabric::editor::UnsavedDecision::save, false));
                    status = "Save failed; retry or discard the changes";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", {100.0F, 0.0F})) {
                ImGui::CloseCurrentPopup();
                if (transition_guard.resolve(
                        fabric::editor::UnsavedDecision::discard) ==
                    fabric::editor::SessionAction::quit) {
                    running = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {100.0F, 0.0F})) {
                static_cast<void>(transition_guard.resolve(
                    fabric::editor::UnsavedDecision::cancel));
                ImGui::CloseCurrentPopup();
                status = "Close cancelled; unsaved changes kept";
            }
            ImGui::EndPopup();
        }
        ImGui::Render();
        glViewport(0, 0, static_cast<GLsizei>(ImGui::GetIO().DisplaySize.x),
                   static_cast<GLsizei>(ImGui::GetIO().DisplaySize.y));
        glClearColor(0.04F, 0.05F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (scene_e2e || transformation_e2e) running = false;
    }

    for (const auto& [_, texture] : map_textures) {
        if (texture.handle != 0U) glDeleteTextures(1, &texture.handle);
    }
    map_renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    NFD_Quit();
    SDL_Quit();
    return e2e_failed || (scene_e2e && !scene_e2e_complete) ||
            (transformation_e2e && !transformation_e2e_complete)
        ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool e2e = argc == 5 && std::string_view{argv[1]} == "--e2e-close";
    const bool scene_e2e = argc == 4 &&
        std::string_view{argv[1]} == "--e2e-scene";
    const bool transformation_e2e = argc == 4 &&
        std::string_view{argv[1]} == "--e2e-transformation";
    const auto e2e_mode = e2e ? close_e2e_mode(argv[2]) : std::nullopt;
    if ((argc != 1 && argc != 3 && !e2e && !scene_e2e &&
         !transformation_e2e) ||
        (e2e && !e2e_mode)) {
        std::cerr << "usage: map_studio [project-directory map-id]\n"
                     "       map_studio --e2e-close "
                     "<clean|window|shortcut|save|save-failure> project-directory map-id\n"
                     "       map_studio --e2e-scene project-directory map-id\n"
                     "       map_studio --e2e-transformation "
                     "project-directory map-id\n";
        return 64;
    }
    const std::filesystem::path project = e2e ? argv[3]
        : scene_e2e ? argv[2]
        : transformation_e2e ? argv[2]
        : argc == 3 ? argv[1] : std::filesystem::path{};
    const fabric::core::ResourceId map_id{
        e2e ? argv[4]
        : scene_e2e || transformation_e2e ? argv[3]
        : argc == 3 ? argv[2] : ""};
    return run(project, map_id, e2e_mode, scene_e2e, transformation_e2e);
}
