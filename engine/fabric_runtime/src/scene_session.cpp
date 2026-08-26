#include "fabric/runtime/scene_session.hpp"

#include "fabric/project/manifest.hpp"

#include <algorithm>
#include <ranges>

namespace fabric::runtime {
namespace {

void append_errors(std::vector<std::string>& output,
                   const std::vector<project::Error>& errors) {
    for (const auto& error : errors)
        output.push_back(error.field + ": " + error.message);
}

} // namespace

bool SceneRuntimeSession::stage_scene(
    const core::ResourceId& scene_id,
    const std::optional<std::string>& entry_point_id,
    std::optional<project::SceneDocument>& scene,
    std::optional<project::MapDocument>& map,
    std::optional<project::SceneEntryPoint>& entry_point,
    std::vector<std::string>& errors) const {
    if (!manifest_ || !core::ResourceId::is_valid(scene_id.value)) {
        errors.push_back("a valid scene id is required");
        return false;
    }
    const auto loaded_scene = project::load_scene(
        project_root_, *manifest_, project::scene_document_path(*manifest_, scene_id));
    if (!loaded_scene.ok()) {
        append_errors(errors, loaded_scene.errors);
        return false;
    }
    const auto composition = project::compose_scene_maps(
        project_root_, *manifest_, *loaded_scene.asset);
    if (!composition.ok()) {
        append_errors(errors, composition.errors);
        return false;
    }
    if (entry_point_id) {
        const auto found = std::ranges::find(
            composition.entry_points, *entry_point_id,
            &project::SceneEntryPoint::id);
        if (found == composition.entry_points.end()) {
            errors.push_back("scene entry point not found: " + *entry_point_id);
            return false;
        }
        entry_point = *found;
    } else {
        entry_point.reset();
    }
    scene = *loaded_scene.asset;
    map = *composition.map;
    return true;
}

bool SceneRuntimeSession::load(const std::filesystem::path& project_root,
                               const core::ResourceId& scene_id) {
    project_root_ = project_root;
    manifest_.reset();
    scene_.reset();
    map_.reset();
    entry_point_.reset();
    errors_.clear();
    const auto loaded_project = project::load_project(project_root_);
    if (!loaded_project.ok()) {
        append_errors(errors_, loaded_project.errors);
        return false;
    }
    manifest_ = *loaded_project.manifest;
    return load_scene(scene_id);
}

bool SceneRuntimeSession::load_scene(
    const core::ResourceId& scene_id,
    std::optional<std::string> entry_point_id) {
    std::optional<project::SceneDocument> staged_scene;
    std::optional<project::MapDocument> staged_map;
    std::optional<project::SceneEntryPoint> staged_entry_point;
    std::vector<std::string> staged_errors;
    if (!stage_scene(scene_id, entry_point_id, staged_scene, staged_map,
                     staged_entry_point, staged_errors)) {
        errors_ = std::move(staged_errors);
        return false;
    }
    scene_ = std::move(staged_scene);
    map_ = std::move(staged_map);
    entry_point_ = std::move(staged_entry_point);
    errors_.clear();
    return true;
}

bool SceneRuntimeSession::transition(const std::string_view transition_id) {
    if (!scene_) {
        errors_ = {"no scene is loaded"};
        return false;
    }
    const auto transition = std::find_if(scene_->transitions.begin(), scene_->transitions.end(),
        [&](const auto& value) { return value.id == transition_id; });
    if (transition == scene_->transitions.end()) {
        errors_ = {"transition not found: " + std::string(transition_id)};
        return false;
    }
    return load_scene(transition->target_scene.id, transition->entry_point);
}

bool SceneRuntimeSession::transition_for_event(const core::ResourceId& event_id) {
    if (!scene_) {
        errors_ = {"no scene is loaded"};
        return false;
    }
    const auto transition = std::find_if(scene_->transitions.begin(), scene_->transitions.end(),
        [&](const auto& value) { return value.event_id && *value.event_id == event_id; });
    if (transition == scene_->transitions.end()) {
        errors_ = {"transition not found for event: " + event_id.value};
        return false;
    }
    return load_scene(transition->target_scene.id, transition->entry_point);
}

} // namespace fabric::runtime
