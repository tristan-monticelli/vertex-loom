#pragma once

#include "fabric/editor/autosave_scheduler.hpp"
#include "fabric/editor/command_stack.hpp"
#include "fabric/project/manifest.hpp"
#include "fabric/project/sprite_sheet.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/render/raster_image.hpp"
#include "fabric/render/sprite_atlas.hpp"

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

struct ImportedTexture {
    project::TextureAsset asset;
    render::RasterImage image;
};

struct ImportedVector {
    project::VectorAsset asset;
    render::RasterImage preview;
};

struct ImportedSpriteSheet {
    project::SpriteSheetDefinition asset;
    render::RasterImage atlas;
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
    [[nodiscard]] bool import_aseprite(const std::filesystem::path& source,
                                       const core::ResourceId& id,
                                       const std::string& name);
    [[nodiscard]] bool import_png_sprite_grid(
        const std::filesystem::path& source,
        const core::ResourceId& id,
        const std::string& name,
        const render::SpriteGrid& grid);
    [[nodiscard]] bool import_png_sprite_regions(
        const std::filesystem::path& source,
        const core::ResourceId& id,
        const std::string& name,
        const std::vector<render::SpriteRegion>& regions);
    [[nodiscard]] bool regenerate_sprite_sheet(const core::ResourceId& id);
    [[nodiscard]] bool set_project_name(
        std::string name,
        AutosaveScheduler::Clock::time_point now =
            AutosaveScheduler::Clock::now());
    [[nodiscard]] bool set_pixels_per_unit(
        double pixels_per_unit,
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
    [[nodiscard]] const std::optional<ImportedSpriteSheet>&
    imported_sprite_sheet() const noexcept;

private:
    std::filesystem::path project_root_;
    std::optional<project::ProjectManifest> manifest_;
    std::optional<ImportedTexture> imported_texture_;
    std::optional<ImportedVector> imported_vector_;
    std::optional<project::VectorAsset> created_vector_;
    std::optional<ImportedSpriteSheet> imported_sprite_sheet_;
    std::optional<project::ProjectManifest> recovery_manifest_;
    CommandStack commands_;
    AutosaveScheduler autosave_;
    std::vector<project::Error> errors_;

    [[nodiscard]] bool publish_sprite_frames(
        const std::filesystem::path& source,
        const core::ResourceId& id,
        const std::string& name,
        project::SpriteSourceKind source_kind,
        std::vector<render::SpriteSourceFrame> frames,
        std::vector<project::SpriteTagDefinition> tags,
        std::vector<project::SpriteSliceDefinition> slices);
};

} // namespace fabric::editor
