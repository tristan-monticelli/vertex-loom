#include "fabric/runtime/scene_session.hpp"

#include "fabric/project/manifest.hpp"

#include <algorithm>

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
    std::optional<project::SceneDocument>& scene,
    std::optional<project::MapDocument>& map,
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
    if (!loaded_scene.asset->entry_map) {
        errors.push_back("scene has no entry map");
        return false;
    }
    const auto loaded_map = project::load_map(
        project_root_, *manifest_, project::map_document_path(
            *manifest_, loaded_scene.asset->entry_map->id));
    if (!loaded_map.ok()) {
        append_errors(errors, loaded_map.errors);
        return false;
    }
    scene = *loaded_scene.asset;
    map = *loaded_map.asset;
    return true;
}

bool SceneRuntimeSession::load(const std::filesystem::path& project_root,
                               const core::ResourceId& scene_id) {
    project_root_ = project_root;
    manifest_.reset();
    scene_.reset();
    map_.reset();
    errors_.clear();
    const auto loaded_project = project::load_project(project_root_);
    if (!loaded_project.ok()) {
        append_errors(errors_, loaded_project.errors);
        return false;
    }
    manifest_ = *loaded_project.manifest;
    return load_scene(scene_id);
}

bool SceneRuntimeSession::load_scene(const core::ResourceId& scene_id) {
    std::optional<project::SceneDocument> staged_scene;
    std::optional<project::MapDocument> staged_map;
    std::vector<std::string> staged_errors;
    if (!stage_scene(scene_id, staged_scene, staged_map, staged_errors)) {
        errors_ = std::move(staged_errors);
        return false;
    }
    scene_ = std::move(staged_scene);
    map_ = std::move(staged_map);
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
    return load_scene(transition->target_scene.id);
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
    return load_scene(transition->target_scene.id);
}

} // namespace fabric::runtime
