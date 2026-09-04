#include "scene_workspace.hpp"

#include "editor_widgets.hpp"
#include "resource_picker.hpp"
#include "fabric/project/document_storage.hpp"
#include "fabric/project/map_package.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <ranges>

namespace fabric::map_studio {

using editor_ui::draw_disabled_reason;
using editor_ui::draw_field_errors;
using editor_ui::draw_resource_name_field;
using editor_ui::draw_validation_errors;
using editor_ui::focus_first_field_error;

void draw_scene_workspace(
    editor::SceneSession& session,
    const std::filesystem::path& project_root,
    SDL_Window* window,
    SceneWorkspaceState& state,
    std::string& status,
    std::vector<project::Error>& package_errors,
    editor::ProjectSession& resource_catalog,
    const SceneFolderPicker& choose_folder) {
    ImGui::SeparatorText("Scenes");
    if (project_root.empty()) {
        ImGui::TextDisabled("Open a project map to author its scenes.");
        return;
    }
    const auto loaded_manifest = project::load_manifest(project_root);
    if (!loaded_manifest.ok()) {
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                           "Project manifest unavailable");
        return;
    }
    const auto maps_directory =
        project_root / loaded_manifest.manifest->directories.maps;
    const auto scenes_directory =
        project_root / loaded_manifest.manifest->directories.scenes;
    std::vector<std::string> map_event_ids;
    if (session.scene()) {
        for (const auto& mounted : session.scene()->maps) {
            const auto map = project::load_map(
                project_root, *loaded_manifest.manifest,
                project::map_document_path(*loaded_manifest.manifest,
                                           mounted.map.id));
            if (!map.ok()) continue;
            for (const auto& event : map.asset->events) {
                if (std::ranges::find(map_event_ids, event.id.value) ==
                    map_event_ids.end()) {
                    map_event_ids.push_back(event.id.value);
                }
            }
        }
    }

    ImGui::SeparatorText("Create or open");
    ImGui::SetNextItemWidth(150.0F);
    ImGui::InputText("Scene id", &state.new_id);
    focus_first_field_error(session.errors(), "id", "scene-create");
    ImGui::SameLine();
    draw_resource_name_field("Scene name", state.new_name, 180.0F);
    focus_first_field_error(session.errors(), "name", "scene-create");
    ImGui::SameLine();
    ImGui::BeginDisabled(state.new_id.empty() || state.new_name.empty());
    if (ImGui::Button("Create scene")) {
        const project::SceneDocument scene{
            .document = {.schema_version = 1,
                         .type = "scene",
                         .id = {.value = state.new_id},
                         .name = state.new_name}};
        const auto created = session.create(project_root, scene);
        status = created ? "Scene created" : "Scene creation rejected";
        if (created) {
            static_cast<void>(resource_catalog.refresh_resources());
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
                         state.open_id, &resource_catalog);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.open_id.empty());
    if (ImGui::Button("Open scene")) {
        const auto opened = session.open(project_root, {.value = state.open_id});
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
        draw_validation_errors(session.errors());
        return;
    }
    if (session.has_recovery()) {
        ImGui::TextColored({1.0F, 0.75F, 0.25F, 1.0F},
                           "A newer valid scene autosave is available.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Recover scene") &&
            session.accept_recovery()) {
            state.edited_name = session.scene()->document.name;
            status = "Scene autosave recovered";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss scene recovery")) {
            session.decline_recovery();
        }
    }

    const auto scene_id = session.scene()->document.id;
    ImGui::SeparatorText("Scene document");
    ImGui::Text("%s (%s)%s", session.scene()->document.name.c_str(),
                scene_id.value.c_str(), session.dirty() ? " · dirty" : "");
    if (state.edited_name.empty()) {
        state.edited_name = session.scene()->document.name;
    }
    if (draw_resource_name_field("Name", state.edited_name, 260.0F,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        status = session.set_name(state.edited_name)
            ? "Scene name changed" : "Scene name rejected";
    }
    if (ImGui::Button("Save scene")) {
        status = session.save() ? "Scene saved" : "Scene save failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate campaign") &&
        (!session.dirty() || session.save())) {
        const auto planned = project::plan_scene_package(project_root, scene_id);
        package_errors = planned.errors;
        status = planned.ok() ? "Campaign package valid"
                              : "Campaign validation failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("Publish campaign") &&
        (!session.dirty() || session.save())) {
        if (const auto parent = choose_folder(window, status)) {
            const auto destination =
                *parent / (scene_id.value + ".scene-package");
            const auto published = project::publish_scene_package(
                project_root, scene_id, destination);
            package_errors = published.errors;
            status = published.ok()
                ? "Campaign published: " + destination.string()
                : "Campaign publication failed";
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
    draw_resource_picker("Maps:", maps_directory, ".map.json", state.map_id,
                         &resource_catalog);
    ImGui::SetNextItemWidth(170.0F);
    ImGui::InputText("Mount id", &state.mount_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(state.map_id.empty() || state.mount_id.empty());
    if (ImGui::Button(state.selected_map < 0 ? "Add mounted map"
                                             : "Apply mounted map")) {
        const project::SceneMapReference edited{
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
        if (ImGui::RadioButton("##entry", entry)) {
            static_cast<void>(session.set_entry_map(map.map.id));
        }
        ImGui::SameLine();
        ImGui::Text("%s → %s", map.map.id.value.c_str(), map.layer_id.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit map mount")) {
            state.selected_map = static_cast<int>(index);
            state.map_id = map.map.id.value;
            state.mount_id = map.layer_id;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove map")) {
            state.remove_map_request = static_cast<int>(index);
            ImGui::OpenPopup("Remove mounted map?");
        }
        ImGui::PopID();
    }
    if (ImGui::BeginPopupModal("Remove mounted map?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto valid = state.remove_map_request >= 0 &&
            static_cast<std::size_t>(state.remove_map_request) <
                session.scene()->maps.size();
        if (valid) {
            const auto& pending = session.scene()->maps[
                static_cast<std::size_t>(state.remove_map_request)];
            ImGui::Text("Remove map '%s' from this scene?",
                        pending.map.id.value.c_str());
            ImGui::TextDisabled("The mount and its layer mapping will be "
                                "removed; the map resource stays intact.");
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Remove mount")) {
            status = session.remove_map(
                static_cast<std::size_t>(state.remove_map_request))
                ? "Map removed from scene" : "Map removal rejected";
            state.selected_map = -1;
            state.remove_map_request = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Select a mounted map before removing its mount.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.remove_map_request = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (session.scene()->entry_map) {
        ImGui::TextDisabled("Entry map: %s",
                            session.scene()->entry_map->id.value.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear entry map")) {
            static_cast<void>(session.set_entry_map(std::nullopt));
        }
    }

    ImGui::SeparatorText("Transitions");
    ImGui::SetNextItemWidth(150.0F);
    ImGui::InputText("Transition id", &state.transition_id);
    draw_resource_picker("Target scenes:", scenes_directory, ".scene.json",
                         state.target_scene_id, &resource_catalog);
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
        for (std::size_t index = 0; index < map_event_ids.size(); ++index) {
            if (map_event_ids[index] == state.event_id) {
                selected_event = static_cast<int>(index + 1U);
            }
        }
        if (ImGui::Combo("Event", &selected_event, labels.data(),
                         static_cast<int>(labels.size()))) {
            state.event_id = selected_event == 0
                ? std::string{}
                : map_event_ids[static_cast<std::size_t>(selected_event - 1)];
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear event")) state.event_id.clear();

    const auto transition_from_state = [&] {
        return project::SceneTransition{
            state.transition_id,
            {{.value = state.target_scene_id}, "scene"},
            state.entry_point,
            state.event_id.empty()
                ? std::nullopt
                : std::optional<core::ResourceId>{{.value = state.event_id}}};
    };
    const bool transition_incomplete = state.transition_id.empty() ||
        state.target_scene_id.empty() || state.entry_point.empty();
    ImGui::BeginDisabled(transition_incomplete);
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
    draw_disabled_reason(
        transition_incomplete,
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
            state.remove_transition_request = static_cast<int>(index);
            ImGui::OpenPopup("Remove transition?");
        }
        ImGui::PopID();
    }
    if (ImGui::BeginPopupModal("Remove transition?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto valid = state.remove_transition_request >= 0 &&
            static_cast<std::size_t>(state.remove_transition_request) <
                session.scene()->transitions.size();
        if (valid) {
            const auto& pending = session.scene()->transitions[
                static_cast<std::size_t>(state.remove_transition_request)];
            ImGui::Text("Remove transition '%s'?", pending.id.c_str());
            ImGui::TextDisabled("The scene link will be removed; the target "
                                "scene and event remain intact.");
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Remove transition")) {
            status = session.remove_transition(
                static_cast<std::size_t>(state.remove_transition_request))
                ? "Transition removed" : "Transition removal rejected";
            state.selected_transition = -1;
            state.remove_transition_request = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        draw_disabled_reason(!valid,
                             "Select a transition before removing it.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.remove_transition_request = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    draw_validation_errors(package_errors, "Package");
    draw_validation_errors(session.errors());
}

} // namespace fabric::map_studio
