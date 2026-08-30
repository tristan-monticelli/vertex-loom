#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/audio.hpp"
#include "fabric/project/behavior_graph.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/input.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/textured_path.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/project/visual_composition.hpp"
#include "fabric/render/raster_image.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::editor {

struct CreateVectorArtworkPrompt;
struct CreateMaterialPrompt;
struct CreateEntityPrompt;
struct CreateAnimationPrompt;
struct CreateInputPrompt;
struct VisualPresetRequest;

enum class StudioResourceKind {
    texture,
    vector,
    material,
    entity,
    animation,
    input,
    behavior,
    transformation,
    textured_path,
    visual_composition,
    visual_component,
    map,
    scene,
    mechanic,
    replay,
    audio,
};

struct ResourceDuplicationDependency {
    StudioResourceKind kind{StudioResourceKind::texture};
    core::ResourceId source_id;
    core::ResourceId destination_id;
    std::string destination_name;
};

struct ResourceDuplicationOptions {
    std::vector<ResourceDuplicationDependency> dependencies;
};

struct StudioResource {
    StudioResourceKind kind{StudioResourceKind::texture};
    core::ResourceId id;
    std::string name;
    std::filesystem::path document_path;
    bool native{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::string format;

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
    [[nodiscard]] bool create_input(const CreateInputPrompt& prompt);
    [[nodiscard]] bool set_selected_audio_event(
        std::size_t event_index, project::AudioEvent event);
    [[nodiscard]] bool create_visual_preset(
        const VisualPresetRequest& request);
    [[nodiscard]] bool create_visual_composition(
        const core::ResourceId& id, std::string name, core::Vec2 size);
    [[nodiscard]] bool create_visual_component(
        const core::ResourceId& id, std::string name,
        const core::ResourceId& composition_id, core::Rect bounds);
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
    [[nodiscard]] bool set_runtime_settings(
        std::optional<project::RuntimeSettings> settings,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_texture_view(
        std::optional<project::RasterView> view,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool reset_selected_texture_view(
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_material(
        project::MaterialDefinition material,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_vector_node(
        std::size_t node_index, project::VectorNode node,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool add_selected_vector_node(
        project::VectorNode node,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool duplicate_selected_vector_node(
        std::size_t node_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool move_selected_vector_node(
        std::size_t node_index, std::size_t destination_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool remove_selected_vector_node(
        std::size_t node_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_entity_node(
        std::size_t node_index, project::EntityNode node,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_entity_behavior(
        std::optional<project::ResourceReference> behavior,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_entity_definition(
        project::EntityDefinition entity,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_visual_composition(
        project::VisualComposition composition,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_textured_path(
        project::TexturedPath path,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_visual_component(
        project::VisualComponent component,
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
    [[nodiscard]] bool move_selected_entity_node(
        std::size_t node_index, std::size_t destination_index,
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
    [[nodiscard]] bool set_selected_animation_preview_entity(
        std::optional<project::ResourceReference> entity,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool insert_selected_animation_key(
        project::PropertyBinding binding, float time,
        project::AnimationValue value, project::AnimationInterpolation interpolation,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now(),
        project::AnimationComposition composition =
            project::AnimationComposition::replace,
        project::AnimationEasing easing = project::AnimationEasing::linear,
        std::optional<project::AnimationValue> in_tangent = {},
        std::optional<project::AnimationValue> out_tangent = {});
    [[nodiscard]] bool set_selected_animation_key(
        project::PropertyBinding binding, float time,
        project::AnimationValue value, project::AnimationInterpolation interpolation,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now(),
        project::AnimationComposition composition =
            project::AnimationComposition::replace,
        project::AnimationEasing easing = project::AnimationEasing::linear,
        std::optional<project::AnimationValue> in_tangent = {},
        std::optional<project::AnimationValue> out_tangent = {});
    [[nodiscard]] bool set_selected_animation_segment(
        project::PropertyBinding binding, float start_time,
        project::AnimationValue start_value, float end_time,
        project::AnimationValue end_value,
        project::AnimationInterpolation interpolation,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now(),
        project::AnimationComposition composition =
            project::AnimationComposition::replace,
        project::AnimationEasing easing = project::AnimationEasing::linear);
    [[nodiscard]] bool remove_selected_animation_key(
        project::PropertyBinding binding, std::size_t key_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool replace_selected_animation_binding(
        project::PropertyBinding from, project::PropertyBinding to,
        AutosaveScheduler::Clock::time_point now = AutosaveScheduler::Clock::now());
    [[nodiscard]] bool move_selected_animation_key(
        project::PropertyBinding binding, std::size_t key_index, float time,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool insert_selected_animation_marker(
        std::string id, float time,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool remove_selected_animation_marker(
        std::string_view id,
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
    [[nodiscard]] bool duplicate_resource(StudioResourceKind kind,
                                          const core::ResourceId& id,
                                          const core::ResourceId& copy_id,
                                          std::string copy_name,
                                          ResourceDuplicationOptions options = {});
    [[nodiscard]] bool rename_resource(StudioResourceKind kind,
                                       const core::ResourceId& id,
                                       std::string name);
    [[nodiscard]] std::optional<std::vector<StudioResource>>
    incoming_references(StudioResourceKind kind, const core::ResourceId& id);
    [[nodiscard]] bool replace_incoming_references(
        StudioResourceKind kind, const core::ResourceId& id,
        const core::ResourceId& replacement_id);
    [[nodiscard]] std::optional<std::vector<StudioResource>>
    behavior_consumers(std::string_view semantic_action);
    [[nodiscard]] bool trash_resource(StudioResourceKind kind,
                                      const core::ResourceId& id,
                                      bool confirmed);
    [[nodiscard]] bool restore_trashed_resource();
    [[nodiscard]] bool can_restore_trashed_resource() const noexcept;

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
    [[nodiscard]] const std::optional<project::InputDocument>&
    selected_input() const noexcept;
    [[nodiscard]] const std::optional<project::TexturedPath>&
    selected_textured_path() const noexcept;
    [[nodiscard]] const std::optional<project::VisualComposition>&
    selected_visual_composition() const noexcept;
    [[nodiscard]] const std::optional<project::VisualComponent>&
    selected_visual_component() const noexcept;
    [[nodiscard]] bool set_selected_input_action_id(
        std::size_t action_index, std::string id,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_selected_input_binding(
        std::size_t action_index, std::size_t binding_index,
        project::InputBinding binding,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool add_selected_input_binding(
        std::size_t action_index, project::InputBinding binding,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool add_selected_input_action(
        project::InputActionDefinition action,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool remove_selected_input_action(
        std::size_t action_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool remove_selected_input_binding(
        std::size_t action_index, std::size_t binding_index,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] const std::vector<StudioResource>& resources() const noexcept;
    [[nodiscard]] StudioResource* selected_resource() noexcept;
    [[nodiscard]] const StudioResource* selected_resource() const noexcept;

private:
    [[nodiscard]] bool save_before_document_transition();
    [[nodiscard]] bool prepare_manifest_edit(
        AutosaveScheduler::Clock::time_point now);
    [[nodiscard]] bool replace_selected_vector_nodes(
        std::vector<project::VectorNode> nodes,
        AutosaveScheduler::Clock::time_point now);

    enum class DirtyDocument {
        none,
        manifest,
        texture,
        vector,
        material,
        entity,
        animation,
        input,
        textured_path,
        visual_composition,
        visual_component,
    };

    [[nodiscard]] bool prepare_dirty_document_edit(
        DirtyDocument expected, project::ErrorCode code,
        std::string field, std::string message,
        AutosaveScheduler::Clock::time_point now);

    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<ImportedTexture> imported_texture_;
    std::optional<ImportedVector> imported_vector_;
    std::optional<project::VectorAsset> created_vector_;
    std::optional<project::MaterialDefinition> selected_material_;
    std::optional<project::EntityDefinition> selected_entity_;
    std::optional<project::AnimationClip> selected_animation_;
    std::optional<project::InputDocument> selected_input_;
    std::optional<project::TexturedPath> selected_textured_path_;
    std::optional<project::VisualComposition> selected_visual_composition_;
    std::optional<project::VisualComponent> selected_visual_component_;
    std::vector<StudioResource> resources_;
    std::optional<std::size_t> selected_resource_index_;
    std::optional<project::ProjectManifest> recovery_manifest_;
    std::optional<project::TextureAsset> recovery_texture_;
    std::optional<project::VectorAsset> recovery_vector_;
    std::optional<project::MaterialDefinition> recovery_material_;
    std::optional<project::EntityDefinition> recovery_entity_;
    std::optional<project::AnimationClip> recovery_animation_;
    std::optional<project::TexturedPath> recovery_textured_path_;
    std::optional<project::VisualComposition> recovery_visual_composition_;
    std::optional<project::VisualComponent> recovery_visual_component_;
    std::filesystem::path selected_vector_document_path_;
    std::filesystem::path selected_texture_document_path_;
    std::filesystem::path selected_material_document_path_;
    std::filesystem::path selected_entity_document_path_;
    std::filesystem::path selected_animation_document_path_;
    std::filesystem::path selected_input_document_path_;
    std::filesystem::path selected_textured_path_document_path_;
    std::filesystem::path selected_visual_composition_document_path_;
    std::filesystem::path selected_visual_component_document_path_;
    DirtyDocument dirty_document_{DirtyDocument::none};
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;
    struct TrashedResource {
        StudioResource resource;
        std::filesystem::path original_path;
        std::filesystem::path trash_path;
    };
    std::optional<TrashedResource> trashed_resource_;

    [[nodiscard]] bool prepare_animation_edit(
        AutosaveScheduler::Clock::time_point now);
    [[nodiscard]] bool sync_animation_preview_entity();
    [[nodiscard]] bool prepare_texture_edit(
        AutosaveScheduler::Clock::time_point now);
    [[nodiscard]] bool prepare_input_edit(
        AutosaveScheduler::Clock::time_point now);

};

} // namespace fabric::editor
