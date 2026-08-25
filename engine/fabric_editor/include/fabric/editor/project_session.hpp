#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/render/raster_image.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fabric::editor {

struct CreateVectorArtworkPrompt;

enum class AutosaveStatus {
    not_due,
    saved,
    failed,
};

enum class StudioResourceKind {
    texture,
    vector,
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
    [[nodiscard]] const std::vector<StudioResource>& resources() const noexcept;
    [[nodiscard]] StudioResource* selected_resource() noexcept;
    [[nodiscard]] const StudioResource* selected_resource() const noexcept;

private:
    enum class DirtyDocument {
        none,
        manifest,
        vector,
    };

    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<ImportedTexture> imported_texture_;
    std::optional<ImportedVector> imported_vector_;
    std::optional<project::VectorAsset> created_vector_;
    std::vector<StudioResource> resources_;
    std::optional<std::size_t> selected_resource_index_;
    std::optional<project::ProjectManifest> recovery_manifest_;
    std::optional<project::VectorAsset> recovery_vector_;
    std::filesystem::path selected_vector_document_path_;
    DirtyDocument dirty_document_{DirtyDocument::none};
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;

};

} // namespace fabric::editor
