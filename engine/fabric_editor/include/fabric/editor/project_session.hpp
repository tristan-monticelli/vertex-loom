#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/render/raster_image.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::editor {

struct CreateVectorArtworkPrompt;
struct CreateMaterialPrompt;
struct CreateEntityPrompt;
struct CreateAnimationPrompt;

enum class StudioResourceKind {
    texture,
    vector,
    material,
    entity,
    animation,
};

struct StudioResource {
    StudioResourceKind kind{StudioResourceKind::texture};
    core::ResourceId id;
    std::string name;
    std::filesystem::path document_path;
    bool native{};

    friend bool operator==(const StudioResource&, const StudioResource&) = default;
};

struct ImportedTexture {
    project::TextureAsset asset;
    render::RasterImage image;
};

struct ImportedVector {
    project::VectorAsset asset;
    render::RasterImage preview;
};

class ProjectSession {
public:
    ProjectSession() = default;
    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;
    ProjectSession(ProjectSession&&) = delete;
    ProjectSession& operator=(ProjectSession&&) = delete;

    [[nodiscard]] bool create(const std::filesystem::path& project_root,
                              const project::ProjectManifest& manifest);
    [[nodiscard]] bool open(const std::filesystem::path& project_root);
    [[nodiscard]] bool import_png(const std::filesystem::path& source,
                                  const core::ResourceId& id,
                                  const std::string& name);
    [[nodiscard]] bool import_svg(const std::filesystem::path& source,
                                  const core::ResourceId& id,
                                  const std::string& name);
    [[nodiscard]] bool create_vector_artwork(
        const CreateVectorArtworkPrompt& prompt);
    [[nodiscard]] bool create_material(const CreateMaterialPrompt& prompt);
    [[nodiscard]] bool create_entity(const CreateEntityPrompt& prompt);
    [[nodiscard]] bool create_animation(const CreateAnimationPrompt& prompt);
    [[nodiscard]] bool convert_selected_linked_svg_to_native(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_project_name(
        std::string name,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_pixels_per_unit(
        double pixels_per_unit,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_vector_node(
        std::size_t node_index, project::VectorNode node,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_entity_node(
        std::size_t node_index, project::EntityNode node,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool add_selected_entity_node(
        project::EntityNode node,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool duplicate_selected_entity_node(
        std::size_t node_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool remove_selected_entity_node(
        std::size_t node_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_animation_duration(
        float duration,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_animation_loop(
        bool loop,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool insert_selected_animation_key(
        project::PropertyBinding binding, float time,
        project::AnimationValue value, project::AnimationInterpolation interpolation,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool remove_selected_animation_key(
        project::PropertyBinding binding, std::size_t key_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool move_selected_animation_key(
        project::PropertyBinding binding, std::size_t key_index, float time,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool undo(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool redo(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool save();
    [[nodiscard]] AutosaveStatus update_autosave(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool accept_recovery(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    void decline_recovery() noexcept;
    [[nodiscard]] bool refresh_resources();
    [[nodiscard]] bool select_resource(StudioResourceKind kind,
                                       const core::ResourceId& id);

    [[nodiscard]] bool has_project() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] bool has_recovery() const noexcept;
    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;
    [[nodiscard]] const std::optional<project::ProjectManifest>& manifest() const noexcept;
    [[nodiscard]] const std::vector<project::Error>& errors() const noexcept;
    [[nodiscard]] const std::optional<ImportedTexture>& imported_texture() const noexcept;
    [[nodiscard]] const std::optional<ImportedVector>& imported_vector() const noexcept;
    [[nodiscard]] const std::optional<project::VectorAsset>&
    created_vector() const noexcept;
    [[nodiscard]] const std::optional<project::MaterialDefinition>&
    selected_material() const noexcept;
    [[nodiscard]] const std::optional<project::EntityDefinition>&
    selected_entity() const noexcept;
    [[nodiscard]] const std::optional<project::AnimationClip>&
    selected_animation() const noexcept;
    [[nodiscard]] const std::vector<StudioResource>& resources() const noexcept;
    [[nodiscard]] StudioResource* selected_resource() noexcept;
    [[nodiscard]] const StudioResource* selected_resource() const noexcept;

private:
    enum class DirtyDocument {
        none,
        manifest,
        vector,
        entity,
        animation,
    };

    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<ImportedTexture> imported_texture_;
    std::optional<ImportedVector> imported_vector_;
    std::optional<project::VectorAsset> created_vector_;
    std::optional<project::MaterialDefinition> selected_material_;
    std::optional<project::EntityDefinition> selected_entity_;
    std::optional<project::AnimationClip> selected_animation_;
    std::vector<StudioResource> resources_;
    std::optional<std::size_t> selected_resource_index_;
    std::optional<project::ProjectManifest> recovery_manifest_;
    std::optional<project::VectorAsset> recovery_vector_;
    std::optional<project::EntityDefinition> recovery_entity_;
    std::optional<project::AnimationClip> recovery_animation_;
    std::filesystem::path selected_vector_document_path_;
    std::filesystem::path selected_entity_document_path_;
    std::filesystem::path selected_animation_document_path_;
    DirtyDocument dirty_document_{DirtyDocument::none};
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;

    [[nodiscard]] bool prepare_animation_edit(
        AutosaveScheduler::Clock::time_point now);

};

} // namespace fabric::editor
