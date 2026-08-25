#include "fabric/editor/project_session.hpp"

#include "fabric/editor/creation_prompts.hpp"
#include "fabric/project/document_storage.hpp"

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
    created_vector_.reset();
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
    created_vector_.reset();
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
    created_vector_.reset();
    errors_.clear();
    return true;
}

bool ProjectSession::create_vector_artwork(
    const CreateVectorArtworkPrompt& prompt) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before creating an artwork"}};
        return false;
    }
    const auto prompt_validation = prompt.validate(project_root_, *manifest_);
    if (!prompt_validation.ok()) {
        errors_.clear();
        for (const auto& error : prompt_validation.errors) {
            errors_.push_back({project::ErrorCode::invalid_asset,
                               error.field, error.message});
        }
        return false;
    }

    project::NativeVectorDefinition native{
        .size = {static_cast<float>(prompt.width),
                 static_cast<float>(prompt.height)},
        .origin = prompt.origin == ArtworkOrigin::center
            ? project::VectorOrigin::center
            : project::VectorOrigin::top_left,
    };
    if (prompt.first_shape != InitialShape::empty) {
        project::VectorFill fill;
        if (prompt.initial_fill == InitialFill::color) {
            fill.kind = project::VectorFillKind::solid;
            fill.color = prompt.initial_color;
        } else if (prompt.initial_fill == InitialFill::image) {
            fill.kind = project::VectorFillKind::image;
            fill.image = project::VectorImageFill{
                .texture = {{.value = prompt.initial_image_id}, "texture"},
                .fit = prompt.image_fit,
                .transform = prompt.image_transform,
                .opacity = static_cast<float>(prompt.image_opacity),
                .deform_with_shape = prompt.deform_image_with_shape,
            };
        }
        const core::Vec2 shape_origin =
            prompt.origin == ArtworkOrigin::center
                ? core::Vec2{-native.size.x * 0.5F, -native.size.y * 0.5F}
                : core::Vec2{};
        native.nodes.push_back(project::VectorNode{
            .id = "node-1",
            .name = std::string(label(prompt.first_shape)),
            .shape = {
                .id = "shape-1",
                .kind = prompt.first_shape == InitialShape::ellipse
                    ? project::VectorShapeKind::ellipse
                    : project::VectorShapeKind::rectangle,
                .bounds = {.origin = shape_origin, .size = native.size},
            },
            .fill = std::move(fill),
        });
    }
    project::VectorAsset asset{
        .document = {
            .schema_version = project::current_vector_schema_version,
            .type = "vector",
            .id = {.value = prompt.id},
            .name = prompt.name,
        },
        .source_kind = project::VectorSourceKind::native,
        .native = std::move(native),
    };
    auto published = project::publish_native_vector_asset(
        project_root_, *manifest_, asset);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }
    created_vector_ = std::move(*published.asset);
    imported_vector_.reset();
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

const std::optional<project::VectorAsset>&
ProjectSession::created_vector() const noexcept {
    return created_vector_;
}

} // namespace fabric::editor
