#include "publish_workspace.hpp"

#include "../shared/editor_widgets.hpp"

#include <imgui.h>

#include <system_error>

namespace fabric::map_studio {
namespace {

using editor_ui::draw_disabled_reason;
using editor_ui::draw_validation_errors;

std::string package_suffix(const PublishRoot root) {
    return root == PublishRoot::scene ? ".scene-package" : ".map-package";
}

std::optional<core::ResourceId> root_id(
    const PublishRoot root,
    const editor::MapSession& map_session,
    const editor::SceneSession& scene_session) {
    if (root == PublishRoot::scene && scene_session.scene())
        return scene_session.scene()->document.id;
    if (root == PublishRoot::map && map_session.map())
        return map_session.map()->document.id;
    return std::nullopt;
}

void update_plan(PublishWorkspaceState& state,
                 std::vector<project::Error>& errors,
                 const std::filesystem::path& project_root,
                 const core::ResourceId& id) {
    state.resources.clear();
    state.minimum_runtime_version.clear();
    state.plan_valid = false;
    if (state.root == PublishRoot::scene) {
        auto result = project::plan_scene_package(project_root, id);
        errors = std::move(result.errors);
        if (!result.ok()) return;
        state.resources = std::move(result.manifest->resources);
        state.minimum_runtime_version = result.manifest->minimum_runtime_version;
    } else {
        auto result = project::plan_map_package(project_root, id);
        errors = std::move(result.errors);
        if (!result.ok()) return;
        state.resources = std::move(result.manifest->resources);
        state.minimum_runtime_version = result.manifest->minimum_runtime_version;
    }
    state.plan_valid = true;
}

} // namespace

void draw_publish_workspace(
    editor::MapSession& map_session,
    editor::SceneSession& scene_session,
    SDL_Window* window,
    PublishWorkspaceState& state,
    std::string& status,
    std::vector<project::Error>& package_errors,
    const bool publication_enabled,
    const std::string_view publication_disabled_reason,
    const PublishPrepare& prepare_documents,
    const PublishFolderPicker& choose_folder,
    PublishWorkspaceProbe* probe) {
    ImGui::SeparatorText("Publish");
    ImGui::TextWrapped(
        "Validate the complete dependency closure, write a portable package, "
        "then load that exact folder with the runtime smoke path.");

    const bool has_map = map_session.map().has_value();
    const bool has_scene = scene_session.scene().has_value();
    ImGui::BeginDisabled(!has_map);
    if (ImGui::RadioButton("Current map", state.root == PublishRoot::map)) {
        state.root = PublishRoot::map;
        state.plan_valid = false;
    }
    ImGui::EndDisabled();
    draw_disabled_reason(!has_map, "Open a map before publishing it.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_scene);
    if (ImGui::RadioButton("Current scene", state.root == PublishRoot::scene)) {
        state.root = PublishRoot::scene;
        state.plan_valid = false;
    }
    ImGui::EndDisabled();
    draw_disabled_reason(!has_scene, "Open a scene before publishing a campaign.");

    if (state.root == PublishRoot::scene && !has_scene && has_map)
        state.root = PublishRoot::map;
    if (state.root == PublishRoot::map && !has_map && has_scene)
        state.root = PublishRoot::scene;

    const auto id = root_id(state.root, map_session, scene_session);
    if (!id) {
        ImGui::TextDisabled("Open a map or scene to prepare a package.");
        return;
    }
    ImGui::Text("Root: %s (%s)", id->value.c_str(),
                state.root == PublishRoot::scene ? "scene" : "map");

    if (ImGui::Button("Choose destination folder")) {
        if (const auto selected = choose_folder(window, status))
            state.destination_parent = *selected;
    }
    ImGui::SameLine();
    const auto destination_label = state.destination_parent.empty()
        ? std::string{"No destination selected"}
        : state.destination_parent.string();
    ImGui::TextDisabled("%s", destination_label.c_str());

    if (ImGui::Button("Validate package")) {
        if (prepare_documents()) {
            update_plan(state, package_errors, map_session.project_root(), *id);
            status = state.plan_valid
                ? "Package closure validated."
                : "Package validation failed.";
            if (probe && probe->enabled) probe->validate_clicked = true;
        }
    }
    if (probe && probe->enabled) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->validate_screen = {(minimum.x + maximum.x) * 0.5F,
                                  (minimum.y + maximum.y) * 0.5F};
        probe->validate_seen = true;
    }
    ImGui::SameLine();
    const bool publication_blocked = state.destination_parent.empty() ||
        !publication_enabled;
    ImGui::BeginDisabled(publication_blocked);
    if (ImGui::Button("Publish and verify runtime")) {
        if (probe && probe->enabled) probe->publish_clicked = true;
        state.runtime_verified = false;
        state.runtime_errors.clear();
        if (prepare_documents()) {
            update_plan(state, package_errors, map_session.project_root(), *id);
            const auto destination = state.destination_parent /
                (id->value + package_suffix(state.root));
            std::error_code error;
            if (std::filesystem::exists(destination, error)) {
                status = "Destination already exists; choose a new folder.";
            } else if (!state.plan_valid) {
                status = "Package validation failed.";
            } else {
                bool published = false;
                if (state.root == PublishRoot::scene) {
                    auto result = project::publish_scene_package(
                        map_session.project_root(), *id, destination);
                    package_errors = std::move(result.errors);
                    published = result.ok();
                } else {
                    auto result = project::publish_map_package(
                        map_session.project_root(), *id, destination);
                    package_errors = std::move(result.errors);
                    published = result.ok();
                }
                if (published) {
                    runtime::PreviewRuntime runtime;
                    const bool loaded = runtime.load({
                        .package_root = destination,
                        .mode = runtime::RuntimeMode::smoke_test,
                        .frame_limit = 1U,
                    });
                    const bool ran = loaded && runtime.run();
                    state.runtime_errors = runtime.errors();
                    state.runtime_stats = runtime.stats();
                    state.runtime_verified = ran;
                    state.published_destination = destination;
                    status = ran
                        ? "Package published and runtime smoke verified."
                        : "Package written but runtime smoke failed.";
                } else {
                    status = "Package publication failed.";
                }
            }
        }
    }
    ImGui::EndDisabled();
    draw_disabled_reason(publication_blocked,
        state.destination_parent.empty()
            ? "Choose a destination folder before publishing."
            : publication_disabled_reason);
    if (probe && probe->enabled) {
        const auto minimum = ImGui::GetItemRectMin();
        const auto maximum = ImGui::GetItemRectMax();
        probe->publish_screen = {(minimum.x + maximum.x) * 0.5F,
                                 (minimum.y + maximum.y) * 0.5F};
        probe->publish_seen = true;
        probe->runtime_verified = state.runtime_verified;
    }

    if (state.plan_valid) {
        ImGui::SeparatorText("Dependency closure");
        ImGui::Text("Minimum runtime: %s", state.minimum_runtime_version.c_str());
        ImGui::Text("Documents: %zu", state.resources.size());
        if (ImGui::BeginChild("publish-dependencies", {0.0F, 220.0F}, true)) {
            for (const auto& resource : state.resources) {
                ImGui::Text("%s · %s", resource.resource.expected_type.c_str(),
                            resource.resource.id.value.c_str());
                ImGui::TextDisabled("  %s",
                                    resource.document_path.generic_string().c_str());
                for (const auto& payload : resource.payload_paths)
                    ImGui::BulletText("%s", payload.generic_string().c_str());
            }
        }
        ImGui::EndChild();
        if (probe && probe->enabled && !state.resources.empty())
            probe->dependency_seen = true;
    }
    draw_validation_errors(package_errors, "Package");
    for (const auto& error : state.runtime_errors)
        ImGui::TextColored({0.95F, 0.42F, 0.38F, 1.0F},
                           "Runtime: %s", error.c_str());
    if (state.runtime_verified) {
        ImGui::SeparatorText("Published runtime result");
        ImGui::TextColored({0.45F, 0.9F, 0.55F, 1.0F},
                           "Verified: %s",
                           state.published_destination.string().c_str());
        ImGui::Text("Frames: %zu · draw calls: %zu · triangles: %zu",
                    state.runtime_stats.frames, state.runtime_stats.draw_calls,
                    state.runtime_stats.triangles);
    }
}

} // namespace fabric::map_studio
