#include "fabric/editor/project_session.hpp"

#include "fabric/project/document_storage.hpp"
#include "fabric/render/aseprite.hpp"

#include <memory>
#include <limits>
#include <string_view>
#include <utility>

namespace fabric::editor {
namespace {

project::ValidationReport validate_serialized_manifest(
    const std::string_view contents) {
    auto parsed = project::parse_manifest(contents);
    return {.errors = std::move(parsed.errors)};
}

template <typename Value>
class SetValueCommand final : public Command {
public:
    SetValueCommand(Value& target, Value next)
        : target_(target), before_(target), after_(std::move(next)) {}

    bool execute() override {
        target_ = after_;
        return true;
    }

    bool undo() override {
        target_ = before_;
        return true;
    }

    bool merge_with(const Command& newer) override {
        const auto* value = dynamic_cast<const SetValueCommand*>(&newer);
        if (value == nullptr || &target_ != &value->target_) {
            return false;
        }
        after_ = value->after_;
        return true;
    }

private:
    Value& target_;
    Value before_;
    Value after_;
};

std::string tag_direction(
    const render::AsepriteLoopDirection direction) {
    switch (direction) {
    case render::AsepriteLoopDirection::forward: return "forward";
    case render::AsepriteLoopDirection::reverse: return "reverse";
    case render::AsepriteLoopDirection::ping_pong: return "pingPong";
    case render::AsepriteLoopDirection::ping_pong_reverse:
        return "pingPongReverse";
    }
    return "forward";
}

std::optional<render::AsepritePoint> frame_pivot(
    const render::AsepriteDocument& document, const std::uint32_t frame) {
    const render::AsepriteSliceKey* fallback = nullptr;
    for (const auto& slice : document.slices) {
        const render::AsepriteSliceKey* active = nullptr;
        for (const auto& key : slice.keys) {
            if (key.frame > frame) {
                break;
            }
            active = &key;
        }
        if (active == nullptr || !active->pivot.has_value()) {
            continue;
        }
        if (slice.name == "pivot") {
            return active->pivot;
        }
        if (fallback == nullptr) {
            fallback = active;
        }
    }
    return fallback == nullptr ? std::nullopt : fallback->pivot;
}

std::vector<render::SpriteSourceFrame> aseprite_frames(
    const render::AsepriteDocument& document) {
    std::vector<render::SpriteSourceFrame> frames;
    frames.reserve(document.frames.size());
    for (std::size_t index = 0; index < document.frames.size(); ++index) {
        frames.push_back(render::SpriteSourceFrame{
            .name = "frame-" + std::to_string(index),
            .image = document.frames[index].image,
            .duration_ms = document.frames[index].duration_ms,
            .pivot = frame_pivot(document, static_cast<std::uint32_t>(index)),
        });
    }
    return frames;
}

std::vector<project::SpriteTagDefinition> aseprite_tags(
    const render::AsepriteDocument& document) {
    std::vector<project::SpriteTagDefinition> tags;
    tags.reserve(document.tags.size());
    for (const auto& tag : document.tags) {
        tags.push_back({.name = tag.name,
                        .from_frame = tag.from_frame,
                        .to_frame = tag.to_frame,
                        .direction = tag_direction(tag.direction),
                        .repeat = tag.repeat});
    }
    return tags;
}

std::vector<project::SpriteSliceDefinition> aseprite_slices(
    const render::AsepriteDocument& document) {
    std::vector<project::SpriteSliceDefinition> slices;
    slices.reserve(document.slices.size());
    for (const auto& slice : document.slices) {
        project::SpriteSliceDefinition converted{.name = slice.name};
        converted.keys.reserve(slice.keys.size());
        for (const auto& key : slice.keys) {
            project::SpriteSliceKeyDefinition converted_key{
                .frame = key.frame,
                .bounds = {key.bounds.x, key.bounds.y, key.bounds.width,
                           key.bounds.height},
            };
            if (key.center.has_value()) {
                converted_key.center = project::SpriteSliceRect{
                    key.center->x, key.center->y, key.center->width,
                    key.center->height};
            }
            if (key.pivot.has_value()) {
                converted_key.pivot =
                    project::SpritePoint{key.pivot->x, key.pivot->y};
            }
            converted.keys.push_back(std::move(converted_key));
        }
        slices.push_back(std::move(converted));
    }
    return slices;
}

project::SpriteSheetDefinition sprite_definition(
    const project::ProjectManifest& manifest,
    const core::ResourceId& id,
    const std::string& name,
    const project::SpriteSourceKind source_kind,
    const render::SpriteAtlas& atlas,
    std::vector<project::SpriteTagDefinition> tags,
    std::vector<project::SpriteSliceDefinition> slices) {
    project::SpriteSheetDefinition definition{
        .document = {
            .schema_version = project::current_sprite_sheet_schema_version,
            .type = "spriteSheet",
            .id = id,
            .name = name,
        },
        .source_kind = source_kind,
        .source = project::sprite_sheet_source_path(manifest, id, source_kind),
        .atlas = project::sprite_sheet_atlas_path(manifest, id),
        .atlas_size = {atlas.image.width, atlas.image.height},
        .tags = std::move(tags),
        .slices = std::move(slices),
    };
    definition.frames.reserve(atlas.frames.size());
    for (const auto& frame : atlas.frames) {
        project::SpriteFrameDefinition converted{
            .name = frame.name,
            .atlas_bounds = {frame.atlas_bounds.x, frame.atlas_bounds.y,
                             frame.atlas_bounds.width,
                             frame.atlas_bounds.height},
            .source_bounds = {frame.source_bounds.x, frame.source_bounds.y,
                              frame.source_bounds.width,
                              frame.source_bounds.height},
            .source_size = {frame.source_width, frame.source_height},
            .duration_ms = frame.duration_ms,
        };
        if (frame.pivot.has_value()) {
            converted.pivot =
                project::SpritePoint{frame.pivot->x, frame.pivot->y};
        }
        if (frame.input_bounds.has_value()) {
            converted.input_bounds = project::SpriteRect{
                frame.input_bounds->x, frame.input_bounds->y,
                frame.input_bounds->width, frame.input_bounds->height};
        }
        definition.frames.push_back(std::move(converted));
    }
    return definition;
}

std::vector<render::SpriteRegion> png_regions(
    const project::SpriteSheetDefinition& definition) {
    std::vector<render::SpriteRegion> regions;
    regions.reserve(definition.frames.size());
    for (const auto& frame : definition.frames) {
        if (!frame.input_bounds.has_value()) {
            return {};
        }
        render::SpriteRegion region{
            .name = frame.name,
            .bounds = {frame.input_bounds->x, frame.input_bounds->y,
                       frame.input_bounds->width,
                       frame.input_bounds->height},
            .duration_ms = frame.duration_ms,
        };
        if (frame.pivot.has_value()) {
            region.pivot =
                render::AsepritePoint{frame.pivot->x, frame.pivot->y};
        }
        regions.push_back(std::move(region));
    }
    return regions;
}

} // namespace

bool ProjectSession::create(const std::filesystem::path& project_root,
                            const project::ProjectManifest& manifest) {
    auto created = project::create_project(project_root, manifest);
    if (!created.ok()) {
        errors_ = std::move(created.errors);
        return false;
    }

    project_root_ = project_root;
    manifest_ = std::move(created.manifest);
    imported_texture_.reset();
    imported_vector_.reset();
    imported_sprite_sheet_.reset();
    recovery_manifest_.reset();
    commands_.clear();
    autosave_.reset();
    errors_.clear();
    return true;
}

bool ProjectSession::open(const std::filesystem::path& project_root) {
    auto loaded = project::load_project(project_root);
    if (!loaded.ok()) {
        errors_ = std::move(loaded.errors);
        return false;
    }

    auto recovery = project::inspect_recovery(
        project_root, "project.json", validate_serialized_manifest);
    std::optional<project::ProjectManifest> recovery_manifest;
    if (recovery.candidate.has_value()) {
        auto parsed = project::parse_manifest(recovery.candidate->contents);
        if (parsed.ok()) {
            recovery_manifest = std::move(parsed.manifest);
        } else {
            recovery.errors = std::move(parsed.errors);
        }
    }

    project_root_ = project_root;
    manifest_ = std::move(loaded.manifest);
    imported_texture_.reset();
    imported_vector_.reset();
    imported_sprite_sheet_.reset();
    recovery_manifest_ = std::move(recovery_manifest);
    commands_.clear();
    autosave_.reset();
    errors_ = std::move(recovery.errors);
    return true;
}

bool ProjectSession::import_png(const std::filesystem::path& source,
                                const core::ResourceId& id,
                                const std::string& name) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before importing a texture"}};
        return false;
    }

    auto decoded = render::load_png(source);
    if (!decoded.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "source",
                    std::string(render::to_string(decoded.error->code)) +
                        ": " + decoded.error->message}};
        return false;
    }

    project::TextureAsset asset{
        .document = {
            .schema_version = project::current_texture_schema_version,
            .type = "texture",
            .id = id,
            .name = name,
        },
        .source = project::texture_source_path(*manifest_, id),
        .width = decoded.image->width,
        .height = decoded.image->height,
        .pixel_format = "rgba8",
    };
    auto published = project::publish_texture_asset(
        project_root_, *manifest_, asset, source);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }

    imported_texture_ = ImportedTexture{
        .asset = std::move(*published.asset),
        .image = std::move(*decoded.image),
    };
    errors_.clear();
    return true;
}

bool ProjectSession::import_svg(const std::filesystem::path& source,
                                const core::ResourceId& id,
                                const std::string& name) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before importing a vector"}};
        return false;
    }

    auto decoded = render::load_svg_preview(source);
    if (!decoded.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "source",
                    std::string(render::to_string(decoded.error->code)) +
                        ": " + decoded.error->message}};
        return false;
    }

    project::VectorAsset asset{
        .document = {
            .schema_version = project::current_vector_schema_version,
            .type = "vector",
            .id = id,
            .name = name,
        },
        .source_kind = project::VectorSourceKind::linked_svg,
        .source = project::vector_source_path(*manifest_, id),
    };
    auto published = project::publish_vector_asset(
        project_root_, *manifest_, asset, source);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }

    imported_vector_ = ImportedVector{
        .asset = std::move(*published.asset),
        .preview = std::move(*decoded.image),
    };
    errors_.clear();
    return true;
}

bool ProjectSession::publish_sprite_frames(
    const std::filesystem::path& source,
    const core::ResourceId& id,
    const std::string& name,
    const project::SpriteSourceKind source_kind,
    std::vector<render::SpriteSourceFrame> frames,
    std::vector<project::SpriteTagDefinition> tags,
    std::vector<project::SpriteSliceDefinition> slices) {
    auto packed = render::build_sprite_atlas(frames);
    if (!packed.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "atlas",
                    std::string(render::to_string(packed.error->code)) +
                        ": " + packed.error->message}};
        return false;
    }
    auto definition = sprite_definition(
        *manifest_, id, name, source_kind, *packed.atlas, std::move(tags),
        std::move(slices));
    auto published = project::publish_sprite_sheet(
        project_root_, *manifest_, definition, source, packed.atlas->png);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }
    imported_sprite_sheet_ = ImportedSpriteSheet{
        .asset = std::move(*published.asset),
        .atlas = std::move(packed.atlas->image),
    };
    errors_.clear();
    return true;
}

bool ProjectSession::import_aseprite(const std::filesystem::path& source,
                                     const core::ResourceId& id,
                                     const std::string& name) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before importing a sprite"}};
        return false;
    }
    auto decoded = render::load_aseprite(source);
    if (!decoded.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "source",
                    std::string(render::to_string(decoded.error->code)) +
                        " at byte " + std::to_string(decoded.error->offset) +
                        ": " + decoded.error->message}};
        return false;
    }
    auto frames = aseprite_frames(*decoded.document);
    auto tags = aseprite_tags(*decoded.document);
    auto slices = aseprite_slices(*decoded.document);
    return publish_sprite_frames(source, id, name,
                                 project::SpriteSourceKind::aseprite,
                                 std::move(frames), std::move(tags),
                                 std::move(slices));
}

bool ProjectSession::import_png_sprite_grid(
    const std::filesystem::path& source,
    const core::ResourceId& id,
    const std::string& name,
    const render::SpriteGrid& grid) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before importing a sprite"}};
        return false;
    }
    auto decoded = render::load_png(source);
    if (!decoded.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "source",
                    std::string(render::to_string(decoded.error->code)) +
                        ": " + decoded.error->message}};
        return false;
    }
    auto sliced = render::slice_sprite_grid(*decoded.image, grid);
    if (!sliced.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "frames",
                    std::string(render::to_string(sliced.error->code)) +
                        ": " + sliced.error->message}};
        return false;
    }
    return publish_sprite_frames(source, id, name,
                                 project::SpriteSourceKind::png,
                                 std::move(*sliced.frames), {}, {});
}

bool ProjectSession::import_png_sprite_regions(
    const std::filesystem::path& source,
    const core::ResourceId& id,
    const std::string& name,
    const std::vector<render::SpriteRegion>& regions) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before importing a sprite"}};
        return false;
    }
    auto decoded = render::load_png(source);
    if (!decoded.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "source",
                    std::string(render::to_string(decoded.error->code)) +
                        ": " + decoded.error->message}};
        return false;
    }
    auto sliced = render::slice_sprite_regions(*decoded.image, regions);
    if (!sliced.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "frames",
                    std::string(render::to_string(sliced.error->code)) +
                        ": " + sliced.error->message}};
        return false;
    }
    return publish_sprite_frames(source, id, name,
                                 project::SpriteSourceKind::png,
                                 std::move(*sliced.frames), {}, {});
}

bool ProjectSession::regenerate_sprite_sheet(const core::ResourceId& id) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before regenerating a sprite"}};
        return false;
    }
    auto current = project::load_sprite_sheet(
        project_root_, *manifest_,
        project::sprite_sheet_document_path(*manifest_, id));
    if (!current.ok()) {
        errors_ = std::move(current.errors);
        return false;
    }
    std::vector<render::SpriteSourceFrame> frames;
    std::vector<project::SpriteTagDefinition> tags;
    std::vector<project::SpriteSliceDefinition> slices;
    const auto source = project_root_ / current.asset->source;
    if (current.asset->source_kind == project::SpriteSourceKind::aseprite) {
        auto decoded = render::load_aseprite(source);
        if (!decoded.ok()) {
            errors_ = {{project::ErrorCode::invalid_asset, "source",
                        std::string(render::to_string(decoded.error->code)) +
                            " at byte " +
                            std::to_string(decoded.error->offset) + ": " +
                            decoded.error->message}};
            return false;
        }
        frames = aseprite_frames(*decoded.document);
        tags = aseprite_tags(*decoded.document);
        slices = aseprite_slices(*decoded.document);
    } else {
        auto decoded = render::load_png(source);
        if (!decoded.ok()) {
            errors_ = {{project::ErrorCode::invalid_asset, "source",
                        std::string(render::to_string(decoded.error->code)) +
                            ": " + decoded.error->message}};
            return false;
        }
        const auto regions = png_regions(*current.asset);
        auto sliced = render::slice_sprite_regions(*decoded.image, regions);
        if (!sliced.ok()) {
            errors_ = {{project::ErrorCode::invalid_asset, "frames",
                        std::string(render::to_string(sliced.error->code)) +
                            ": " + sliced.error->message}};
            return false;
        }
        frames = std::move(*sliced.frames);
        tags = current.asset->tags;
        slices = current.asset->slices;
    }
    auto packed = render::build_sprite_atlas(frames);
    if (!packed.ok()) {
        errors_ = {{project::ErrorCode::invalid_asset, "atlas",
                    std::string(render::to_string(packed.error->code)) +
                        ": " + packed.error->message}};
        return false;
    }
    auto updated = sprite_definition(
        *manifest_, id, current.asset->document.name,
        current.asset->source_kind, *packed.atlas, std::move(tags),
        std::move(slices));
    auto regenerated = project::regenerate_sprite_sheet(
        project_root_, *manifest_, updated, packed.atlas->png);
    if (!regenerated.ok()) {
        errors_ = std::move(regenerated.errors);
        return false;
    }
    imported_sprite_sheet_ = ImportedSpriteSheet{
        .asset = std::move(*regenerated.asset),
        .atlas = std::move(packed.atlas->image),
    };
    errors_.clear();
    return true;
}

bool ProjectSession::set_project_name(
    std::string name, const AutosaveScheduler::Clock::time_point now) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_manifest, "project",
                    "a project must be open before editing its name"}};
        return false;
    }
    if (manifest_->name == name) {
        return true;
    }
    auto candidate = *manifest_;
    candidate.name = name;
    auto validation = project::validate_manifest(candidate);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(std::make_unique<SetValueCommand<std::string>>(
            manifest_->name, std::move(name)))) {
        errors_ = {{project::ErrorCode::invalid_manifest, "name",
                    "cannot execute the name modification"}};
        return false;
    }
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::set_pixels_per_unit(
    const double pixels_per_unit,
    const AutosaveScheduler::Clock::time_point now) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_manifest, "project",
                    "a project must be open before editing its units"}};
        return false;
    }
    if (manifest_->pixels_per_unit == pixels_per_unit) {
        return true;
    }
    auto candidate = *manifest_;
    candidate.pixels_per_unit = pixels_per_unit;
    auto validation = project::validate_manifest(candidate);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(std::make_unique<SetValueCommand<double>>(
            manifest_->pixels_per_unit, pixels_per_unit))) {
        errors_ = {{project::ErrorCode::invalid_manifest, "pixelsPerUnit",
                    "cannot execute the unit modification"}};
        return false;
    }
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::undo(const AutosaveScheduler::Clock::time_point now) {
    if (!commands_.undo()) {
        return false;
    }
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::redo(const AutosaveScheduler::Clock::time_point now) {
    if (!commands_.redo()) {
        return false;
    }
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::save() {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_manifest, "project",
                    "a project must be open before saving"}};
        return false;
    }
    auto report = project::save_manifest_atomic(project_root_, *manifest_);
    if (!report.ok()) {
        errors_ = std::move(report.errors);
        return false;
    }
    commands_.mark_clean();
    autosave_.reset();
    errors_.clear();
    return true;
}

AutosaveStatus ProjectSession::update_autosave(
    const AutosaveScheduler::Clock::time_point now) {
    if (!has_project()) {
        autosave_.reset();
        return AutosaveStatus::not_due;
    }
    if (!commands_.dirty()) {
        if (!autosave_.pending()) {
            return AutosaveStatus::not_due;
        }
        auto report = project::save_autosave_atomic(
            project_root_, "project.json",
            project::serialize_manifest(*manifest_),
            validate_serialized_manifest);
        if (!report.ok()) {
            errors_ = std::move(report.errors);
            return AutosaveStatus::failed;
        }
        autosave_.mark_saved();
        errors_.clear();
        return AutosaveStatus::saved;
    }
    if (!autosave_.due(now)) {
        return AutosaveStatus::not_due;
    }
    auto report = project::save_autosave_atomic(
        project_root_, "project.json", project::serialize_manifest(*manifest_),
        validate_serialized_manifest);
    if (!report.ok()) {
        errors_ = std::move(report.errors);
        return AutosaveStatus::failed;
    }
    autosave_.mark_saved();
    errors_.clear();
    return AutosaveStatus::saved;
}

bool ProjectSession::accept_recovery(
    const AutosaveScheduler::Clock::time_point now) {
    if (!recovery_manifest_.has_value()) {
        return false;
    }
    manifest_ = std::move(recovery_manifest_);
    recovery_manifest_.reset();
    commands_.clear();
    commands_.mark_dirty();
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

void ProjectSession::decline_recovery() noexcept {
    recovery_manifest_.reset();
    errors_.clear();
}

bool ProjectSession::has_project() const noexcept {
    return manifest_.has_value();
}

bool ProjectSession::dirty() const noexcept {
    return has_project() && commands_.dirty();
}

bool ProjectSession::can_undo() const noexcept {
    return commands_.can_undo();
}

bool ProjectSession::can_redo() const noexcept {
    return commands_.can_redo();
}

bool ProjectSession::has_recovery() const noexcept {
    return recovery_manifest_.has_value();
}

const std::filesystem::path& ProjectSession::project_root() const noexcept {
    return project_root_;
}

const std::optional<project::ProjectManifest>& ProjectSession::manifest() const noexcept {
    return manifest_;
}

const std::vector<project::Error>& ProjectSession::errors() const noexcept {
    return errors_;
}

const std::optional<ImportedTexture>& ProjectSession::imported_texture() const noexcept {
    return imported_texture_;
}

const std::optional<ImportedVector>& ProjectSession::imported_vector() const noexcept {
    return imported_vector_;
}

const std::optional<ImportedSpriteSheet>&
ProjectSession::imported_sprite_sheet() const noexcept {
    return imported_sprite_sheet_;
}

} // namespace fabric::editor
