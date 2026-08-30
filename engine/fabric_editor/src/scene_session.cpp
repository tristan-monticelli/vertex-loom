#include "fabric/editor/scene_session.hpp"

#include "fabric/project/document_storage.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace fabric::editor {
namespace {

class SceneSnapshotCommand final : public Command {
public:
    SceneSnapshotCommand(project::SceneDocument& target,
                         project::SceneDocument before,
                         project::SceneDocument after)
        : target_(target), before_(std::move(before)),
          after_(std::move(after)) {}
    bool execute() override { target_ = after_; return true; }
    bool undo() override { target_ = before_; return true; }

private:
    project::SceneDocument& target_;
    project::SceneDocument before_;
    project::SceneDocument after_;
};

project::ValidationReport validate_serialized(
    const project::ProjectManifest& manifest,
    const std::string_view contents) {
    const auto parsed = project::parse_scene(manifest, contents);
    return {.errors = parsed.errors};
}

} // namespace

bool SceneSession::create(const std::filesystem::path& project_root,
                          const project::SceneDocument& scene) {
    auto loaded = project::load_manifest(project_root);
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    const auto validation = project::validate_scene(*loaded.manifest, scene);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    const auto path = project::scene_document_path(
        *loaded.manifest, scene.document.id);
    std::error_code filesystem_error;
    if (std::filesystem::exists(project_root / path, filesystem_error) ||
        filesystem_error) {
        errors_ = {{project::ErrorCode::asset_already_exists, "document",
                    "scene document already exists or cannot be inspected"}};
        return false;
    }
    if (dirty() && !save()) return false;
    project_root_ = project_root;
    manifest_ = std::move(*loaded.manifest);
    scene_ = scene;
    document_path_ = path;
    recovery_scene_.reset();
    commands_.clear();
    autosave_.reset();
    errors_.clear();
    return save();
}

bool SceneSession::open(const std::filesystem::path& project_root,
                        const core::ResourceId& scene_id) {
    auto loaded = project::load_manifest(project_root);
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    const auto path = project::scene_document_path(*loaded.manifest, scene_id);
    auto loaded_scene = project::load_scene(
        project_root, *loaded.manifest, path);
    if (!loaded_scene.ok()) {
        errors_ = std::move(loaded_scene.errors);
        return false;
    }
    if (dirty() && !save()) return false;
    project_root_ = project_root;
    manifest_ = std::move(*loaded.manifest);
    scene_ = std::move(*loaded_scene.asset);
    document_path_ = path;
    recovery_scene_.reset();
    commands_.clear();
    autosave_.reset();
    errors_.clear();
    const auto recovery = project::inspect_recovery(
        project_root_, document_path_, [this](const std::string_view contents) {
            return validate_serialized(*manifest_, contents);
        });
    if (recovery.candidate) {
        auto parsed = project::parse_scene(
            *manifest_, recovery.candidate->contents);
        if (parsed.ok()) recovery_scene_ = std::move(*parsed.asset);
    } else if (!recovery.errors.empty()) {
        errors_ = recovery.errors;
    }
    return true;
}

bool SceneSession::save() {
    if (!scene_ || !manifest_) return false;
    const auto result = project::publish_scene(
        project_root_, *manifest_, *scene_);
    if (!result.ok()) { errors_ = result.errors; return false; }
    commands_.mark_clean();
    autosave_.reset();
    errors_.clear();
    return true;
}

AutosaveStatus SceneSession::update_autosave(
    const AutosaveScheduler::Clock::time_point now) {
    if (!scene_ || !manifest_ || document_path_.empty()) {
        autosave_.reset();
        return AutosaveStatus::not_due;
    }
    if (!commands_.dirty()) {
        if (!autosave_.pending()) return AutosaveStatus::not_due;
    } else if (!autosave_.pending()) {
        autosave_.mark_changed(now);
    }
    if (!autosave_.due(now)) return AutosaveStatus::not_due;
    const auto report = project::save_autosave_atomic(
        project_root_, document_path_, project::serialize_scene(*scene_),
        [this](const std::string_view contents) {
            return validate_serialized(*manifest_, contents);
        });
    if (!report.ok()) {
        errors_ = report.errors;
        return AutosaveStatus::failed;
    }
    autosave_.mark_saved();
    errors_.clear();
    return AutosaveStatus::saved;
}

bool SceneSession::accept_recovery(
    const AutosaveScheduler::Clock::time_point now) {
    if (!recovery_scene_) return false;
    scene_ = std::move(recovery_scene_);
    recovery_scene_.reset();
    commands_.clear();
    commands_.mark_dirty();
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

void SceneSession::decline_recovery() noexcept {
    recovery_scene_.reset();
    errors_.clear();
}

bool SceneSession::commit(project::SceneDocument next) {
    if (!scene_ || !manifest_ || next == *scene_) return false;
    const auto validation = project::validate_scene(*manifest_, next);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    auto before = *scene_;
    const bool committed = commands_.execute(
        std::make_unique<SceneSnapshotCommand>(
            *scene_, std::move(before), std::move(next)));
    if (committed) errors_.clear();
    return committed;
}

bool SceneSession::set_name(std::string name) {
    if (!scene_) return false;
    auto next = *scene_;
    next.document.name = std::move(name);
    return commit(std::move(next));
}

bool SceneSession::add_map(project::SceneMapReference map) {
    if (!scene_) return false;
    auto next = *scene_;
    next.maps.push_back(std::move(map));
    return commit(std::move(next));
}

bool SceneSession::set_map(const std::size_t index,
                           project::SceneMapReference map) {
    if (!scene_ || index >= scene_->maps.size()) return false;
    auto next = *scene_;
    const auto previous_id = next.maps[index].map.id;
    next.maps[index] = std::move(map);
    if (next.entry_map && next.entry_map->id == previous_id)
        next.entry_map = next.maps[index].map;
    return commit(std::move(next));
}

bool SceneSession::remove_map(const std::size_t index) {
    if (!scene_ || index >= scene_->maps.size()) return false;
    auto next = *scene_;
    const auto removed_id = next.maps[index].map.id;
    next.maps.erase(next.maps.begin() + static_cast<std::ptrdiff_t>(index));
    if (next.entry_map && next.entry_map->id == removed_id)
        next.entry_map.reset();
    return commit(std::move(next));
}

bool SceneSession::set_entry_map(
    const std::optional<core::ResourceId> map_id) {
    if (!scene_) return false;
    auto next = *scene_;
    next.entry_map = map_id
        ? std::optional<project::ResourceReference>{
              project::ResourceReference{*map_id, "map"}}
        : std::nullopt;
    return commit(std::move(next));
}

bool SceneSession::add_transition(project::SceneTransition transition) {
    if (!scene_) return false;
    auto next = *scene_;
    next.transitions.push_back(std::move(transition));
    return commit(std::move(next));
}

bool SceneSession::set_transition(
    const std::size_t index, project::SceneTransition transition) {
    if (!scene_ || index >= scene_->transitions.size()) return false;
    auto next = *scene_;
    next.transitions[index] = std::move(transition);
    return commit(std::move(next));
}

bool SceneSession::remove_transition(const std::size_t index) {
    if (!scene_ || index >= scene_->transitions.size()) return false;
    auto next = *scene_;
    next.transitions.erase(
        next.transitions.begin() + static_cast<std::ptrdiff_t>(index));
    return commit(std::move(next));
}

bool SceneSession::undo() { return commands_.undo(); }
bool SceneSession::redo() { return commands_.redo(); }

} // namespace fabric::editor
