#include "fabric/editor/project_session.hpp"

#include "fabric/editor/animation_timeline.hpp"

#include "fabric/editor/creation_prompts.hpp"
#include "fabric/project/document_storage.hpp"
#include "fabric/render/svg_vector.hpp"

#include <algorithm>
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

std::optional<std::vector<StudioResource>> index_project_resources(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    std::vector<project::Error>& errors) {
    std::vector<StudioResource> indexed;
    const auto inspect = [&](const StudioResourceKind kind,
                             const std::filesystem::path& directory,
                             const std::string_view suffix) -> bool {
        std::error_code error;
        const bool exists = std::filesystem::exists(directory, error);
        if (!exists && !error) {
            return true;
        }
        if (error || !std::filesystem::is_directory(directory, error)) {
            errors = {{project::ErrorCode::io_error, "resources",
                       "cannot inspect the resource directory"}};
            return false;
        }
        for (std::filesystem::directory_iterator iterator{directory, error};
             !error && iterator != std::filesystem::directory_iterator{};
             iterator.increment(error)) {
            if (!iterator->is_regular_file(error)) {
                error.clear();
                continue;
            }
            const auto filename = iterator->path().filename().string();
            if (!filename.ends_with(suffix)) {
                continue;
            }
            const auto relative = std::filesystem::relative(
                iterator->path(), project_root, error);
            if (error) {
                break;
            }
            if (kind == StudioResourceKind::texture) {
                auto loaded = project::load_texture_asset(
                    project_root, manifest, relative);
                if (!loaded.ok()) {
                    errors = std::move(loaded.errors);
                    return false;
                }
                indexed.push_back({kind, loaded.asset->document.id,
                                   loaded.asset->document.name, relative, false});
            } else if (kind == StudioResourceKind::vector) {
                auto loaded = project::load_vector_asset(
                    project_root, manifest, relative);
                if (!loaded.ok()) {
                    errors = std::move(loaded.errors);
                    return false;
                }
                indexed.push_back({
                    kind, loaded.asset->document.id, loaded.asset->document.name,
                    relative,
                    loaded.asset->source_kind == project::VectorSourceKind::native});
            } else if (kind == StudioResourceKind::material) {
                auto loaded = project::load_material(
                    project_root, manifest, relative);
                if (!loaded.ok()) {
                    errors = std::move(loaded.errors);
                    return false;
                }
                indexed.push_back({kind, loaded.asset->document.id,
                                   loaded.asset->document.name, relative, false});
            } else if (kind == StudioResourceKind::entity) {
                auto loaded = project::load_entity(
                    project_root, manifest, relative);
                if (!loaded.ok()) {
                    errors = std::move(loaded.errors);
                    return false;
                }
                indexed.push_back({kind, loaded.entity->document.id,
                                   loaded.entity->document.name, relative, false});
            } else {
                auto loaded = project::load_animation(
                    project_root, manifest, relative);
                if (!loaded.ok()) {
                    errors = std::move(loaded.errors);
                    return false;
                }
                indexed.push_back({kind, loaded.asset->document.id,
                                   loaded.asset->document.name, relative, false});
            }
        }
        if (error) {
            errors = {{project::ErrorCode::io_error, "resources",
                       "cannot enumerate the resource directory"}};
            return false;
        }
        return true;
    };

    const auto assets = project_root / manifest.directories.assets;
    if (!inspect(StudioResourceKind::texture, assets / "textures",
                 ".texture.json") ||
        !inspect(StudioResourceKind::vector, assets / "vectors",
                 ".vector.json") ||
        !inspect(StudioResourceKind::material, assets / "materials",
                 ".material.json") ||
        !inspect(StudioResourceKind::entity,
                 project_root / manifest.directories.entities,
                 ".entity.json") ||
        !inspect(StudioResourceKind::animation,
                 assets / "animations", ".animation.json")) {
        return std::nullopt;
    }
    std::ranges::sort(indexed, [](const StudioResource& left,
                                 const StudioResource& right) {
        if (left.kind != right.kind) {
            return left.kind < right.kind;
        }
        if (left.name != right.name) {
            return left.name < right.name;
        }
        return left.id.value < right.id.value;
    });
    return indexed;
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

template <typename Value>
class ReplaceValueCommand final : public Command {
public:
    ReplaceValueCommand(Value& target, Value next)
        : target_(target), before_(target), after_(std::move(next)) {}

    bool execute() override {
        target_ = after_;
        return true;
    }

    bool undo() override {
        target_ = before_;
        return true;
    }

private:
    Value& target_;
    Value before_;
    Value after_;
};

class ConvertLinkedSvgCommand final : public Command {
public:
    ConvertLinkedSvgCommand(std::optional<project::VectorAsset>& target,
                            std::optional<ImportedVector>& imported,
                            StudioResource& resource,
                            project::VectorAsset converted)
        : target_(target), imported_(imported), resource_(resource),
          before_imported_(imported), converted_(std::move(converted)),
          before_native_(resource.native) {}

    bool execute() override {
        target_ = converted_;
        imported_.reset();
        resource_.native = true;
        return true;
    }

    bool undo() override {
        target_.reset();
        imported_ = before_imported_;
        resource_.native = before_native_;
        return true;
    }

private:
    std::optional<project::VectorAsset>& target_;
    std::optional<ImportedVector>& imported_;
    StudioResource& resource_;
    std::optional<ImportedVector> before_imported_;
    project::VectorAsset converted_;
    bool before_native_{};
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
    selected_material_.reset();
    selected_entity_.reset();
    selected_animation_.reset();
    resources_.clear();
    selected_resource_index_.reset();
    recovery_manifest_.reset();
    recovery_vector_.reset();
    recovery_entity_.reset();
    recovery_animation_.reset();
    selected_vector_document_path_.clear();
    selected_entity_document_path_.clear();
    selected_animation_document_path_.clear();
    dirty_document_ = DirtyDocument::none;
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

    std::vector<project::Error> index_errors;
    auto indexed = index_project_resources(
        project_root, *loaded.manifest, index_errors);
    if (!indexed) {
        errors_ = std::move(index_errors);
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
    selected_material_.reset();
    selected_entity_.reset();
    selected_animation_.reset();
    resources_ = std::move(*indexed);
    selected_resource_index_.reset();
    recovery_manifest_ = std::move(recovery_manifest);
    recovery_vector_.reset();
    recovery_entity_.reset();
    recovery_animation_.reset();
    selected_vector_document_path_.clear();
    selected_entity_document_path_.clear();
    selected_animation_document_path_.clear();
    dirty_document_ = DirtyDocument::none;
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
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "save vector changes before importing another resource"}};
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
    if (!refresh_resources()) {
        return false;
    }
    return select_resource(StudioResourceKind::texture, id);
}

bool ProjectSession::import_svg(const std::filesystem::path& source,
                                const core::ResourceId& id,
                                const std::string& name) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before importing a vector"}};
        return false;
    }
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "save vector changes before importing another resource"}};
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
    if (!refresh_resources()) {
        return false;
    }
    return select_resource(StudioResourceKind::vector, id);
}

bool ProjectSession::create_vector_artwork(
    const CreateVectorArtworkPrompt& prompt) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before creating an artwork"}};
        return false;
    }
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "save vector changes before creating another resource"}};
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
            .id = prompt.resource_id(project_root_, *manifest_),
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
    selected_material_.reset();
    selected_entity_.reset();
    selected_animation_.reset();
    const auto created_id = created_vector_->document.id;
    if (!refresh_resources()) {
        return false;
    }
    return select_resource(StudioResourceKind::vector, created_id);
}

bool ProjectSession::create_material(const CreateMaterialPrompt& prompt) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before creating a material"}};
        return false;
    }
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "save vector changes before creating another resource"}};
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
    project::MaterialDefinition material{
        .document = {
            .schema_version = project::current_material_schema_version,
            .type = "material",
            .id = prompt.resource_id(project_root_, *manifest_),
            .name = prompt.name,
        },
        .color = prompt.color,
        .opacity = static_cast<float>(prompt.opacity),
        .blend = prompt.blend,
        .uv_transform = prompt.uv_transform,
    };
    if (!prompt.texture_id.empty()) {
        material.texture = project::ResourceReference{
            {.value = prompt.texture_id}, "texture"};
    }
    if (!prompt.vector_pattern_id.empty()) {
        material.vector_pattern = project::ResourceReference{
            {.value = prompt.vector_pattern_id}, "vector"};
    }
    const auto published = project::publish_material(
        project_root_, *manifest_, material);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }
    selected_material_ = std::move(*published.asset);
    imported_texture_.reset();
    imported_vector_.reset();
    created_vector_.reset();
    selected_entity_.reset();
    selected_animation_.reset();
    selected_vector_document_path_.clear();
    const auto created_id = selected_material_->document.id;
    if (!refresh_resources()) return false;
    return select_resource(StudioResourceKind::material, created_id);
}

bool ProjectSession::create_entity(const CreateEntityPrompt& prompt) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before creating an entity"}};
        return false;
    }
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "save vector changes before creating another resource"}};
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
    project::EntityDefinition entity{
        .document = {
            .schema_version = project::current_entity_schema_version,
            .type = "entity",
            .id = prompt.resource_id_for_document(project_root_, *manifest_),
            .name = prompt.name,
        },
    };
    project::EntityNode node{
        .id = "root",
        .name = prompt.node_name,
        .transform = prompt.transform,
        .z_order = prompt.z_order,
        .drawable = {.kind = prompt.drawable},
    };
    if (!prompt.resource_id.empty()) {
        node.drawable.resource = project::ResourceReference{
            {.value = prompt.resource_id},
            prompt.drawable == project::EntityDrawableKind::texture
                ? "texture"
                : "vector"};
    }
    if (!prompt.material_id.empty()) {
        node.drawable.material = project::ResourceReference{
            {.value = prompt.material_id}, "material"};
    }
    entity.nodes.push_back(std::move(node));
    const auto published = project::publish_entity(
        project_root_, *manifest_, entity);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }
    selected_entity_ = std::move(*published.entity);
    imported_texture_.reset();
    imported_vector_.reset();
    created_vector_.reset();
    selected_material_.reset();
    selected_animation_.reset();
    selected_vector_document_path_.clear();
    const auto created_id = selected_entity_->document.id;
    if (!refresh_resources()) return false;
    return select_resource(StudioResourceKind::entity, created_id);
}

bool ProjectSession::create_animation(const CreateAnimationPrompt& prompt) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before creating an animation"}};
        return false;
    }
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "save vector changes before creating another resource"}};
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
    project::AnimationClip animation{
        .document = {
            .schema_version = project::current_animation_schema_version,
            .type = "animation",
            .id = prompt.resource_id_for_document(project_root_, *manifest_),
            .name = prompt.name,
        },
        .duration = static_cast<float>(prompt.duration),
        .loop = prompt.loop,
    };
    if (!prompt.marker_id.empty()) {
        animation.markers.push_back({prompt.marker_id,
                                     static_cast<float>(prompt.marker_time)});
    }
    const auto published = project::publish_animation(
        project_root_, *manifest_, animation);
    if (!published.ok()) {
        errors_ = std::move(published.errors);
        return false;
    }
    selected_animation_ = std::move(*published.asset);
    imported_texture_.reset();
    imported_vector_.reset();
    created_vector_.reset();
    selected_material_.reset();
    selected_entity_.reset();
    selected_vector_document_path_.clear();
    const auto created_id = selected_animation_->document.id;
    if (!refresh_resources()) return false;
    return select_resource(StudioResourceKind::animation, created_id);
}

bool ProjectSession::convert_selected_linked_svg_to_native(
    const AutosaveScheduler::Clock::time_point now) {
    auto* selected = selected_resource();
    if (selected == nullptr || selected->kind != StudioResourceKind::vector ||
        selected->native || !imported_vector_ || !manifest_ ||
        selected_vector_document_path_.empty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select a linked SVG before converting it"}};
        return false;
    }
    if (commands_.dirty()) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "save or undo the current change before converting an SVG"}};
        return false;
    }
    const auto converted = render::convert_svg_to_native(
        project_root_ / imported_vector_->asset.source,
        imported_vector_->asset.document.id,
        imported_vector_->asset.document.name);
    if (!converted.ok()) {
        errors_ = std::move(converted.errors);
        return false;
    }
    auto validation = project::validate_vector_asset(*manifest_, *converted.asset);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(std::make_unique<ConvertLinkedSvgCommand>(
            created_vector_, imported_vector_, *selected,
            std::move(*converted.asset)))) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "cannot execute SVG native conversion"}};
        return false;
    }
    dirty_document_ = DirtyDocument::vector;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::refresh_resources() {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_asset, "project",
                    "a project must be open before indexing resources"}};
        return false;
    }

    std::optional<std::pair<StudioResourceKind, core::ResourceId>> previous;
    if (const auto* selected = selected_resource()) {
        previous = std::pair{selected->kind, selected->id};
    }
    auto indexed = index_project_resources(project_root_, *manifest_, errors_);
    if (!indexed) {
        return false;
    }
    resources_ = std::move(*indexed);
    selected_resource_index_.reset();
    if (previous) {
        const auto match = std::ranges::find_if(
            resources_, [&](const StudioResource& resource) {
                return resource.kind == previous->first &&
                    resource.id == previous->second;
            });
        if (match != resources_.end()) {
            selected_resource_index_ =
                static_cast<std::size_t>(std::distance(resources_.begin(), match));
        }
    }
    errors_.clear();
    return true;
}

bool ProjectSession::select_resource(const StudioResourceKind kind,
                                     const core::ResourceId& id) {
    std::vector<project::Error> selection_warnings;
    const auto match = std::ranges::find_if(
        resources_, [&](const StudioResource& resource) {
            return resource.kind == kind && resource.id == id;
        });
    if (match == resources_.end()) {
        errors_ = {{project::ErrorCode::missing_resource, "selection",
                    "the selected resource is not indexed"}};
        return false;
    }
    if (commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        const auto* selected = selected_resource();
        if (selected == nullptr || selected->kind != kind || selected->id != id) {
            errors_ = {{project::ErrorCode::invalid_asset, "selection",
                        "save or undo vector changes before changing selection"}};
            return false;
        }
        return true;
    }
    if (!commands_.dirty()) {
        if (dirty_document_ != DirtyDocument::none && autosave_.pending() &&
            update_autosave() == AutosaveStatus::failed) {
            return false;
        }
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
    }
    const auto index = static_cast<std::size_t>(
        std::distance(resources_.begin(), match));
    if (kind == StudioResourceKind::texture) {
        auto loaded = project::load_texture_asset(
            project_root_, *manifest_, match->document_path);
        if (!loaded.ok()) {
            errors_ = std::move(loaded.errors);
            return false;
        }
        auto image = render::load_png(project_root_ / loaded.asset->source);
        if (!image.ok()) {
            errors_ = {{project::ErrorCode::invalid_asset, "selection",
                        image.error->message}};
            return false;
        }
        imported_texture_ = ImportedTexture{
            .asset = std::move(*loaded.asset), .image = std::move(*image.image)};
        imported_vector_.reset();
        selected_material_.reset();
        selected_entity_.reset();
        selected_animation_.reset();
        created_vector_.reset();
        selected_vector_document_path_.clear();
        recovery_vector_.reset();
        selected_entity_document_path_.clear();
        recovery_entity_.reset();
        selected_animation_document_path_.clear();
        recovery_animation_.reset();
    } else if (kind == StudioResourceKind::vector) {
        auto loaded = project::load_vector_asset(
            project_root_, *manifest_, match->document_path);
        if (!loaded.ok()) {
            errors_ = std::move(loaded.errors);
            return false;
        }
        imported_texture_.reset();
        selected_material_.reset();
        selected_entity_.reset();
        selected_animation_.reset();
        if (loaded.asset->source_kind == project::VectorSourceKind::linked_svg) {
            auto image = render::load_svg_preview(project_root_ / loaded.asset->source);
            if (!image.ok()) {
                errors_ = {{project::ErrorCode::invalid_asset, "selection",
                            image.error->message}};
                return false;
            }
            imported_vector_ = ImportedVector{
                .asset = std::move(*loaded.asset),
                .preview = std::move(*image.image)};
            created_vector_.reset();
            selected_vector_document_path_ = match->document_path;
            recovery_vector_.reset();
        } else {
            created_vector_ = std::move(*loaded.asset);
            imported_vector_.reset();
            selected_vector_document_path_ = match->document_path;
            auto recovery = project::inspect_recovery(
                project_root_, selected_vector_document_path_,
                [this](const std::string_view contents) {
                    auto parsed = project::parse_vector_asset(*manifest_, contents);
                    return project::ValidationReport{
                        .errors = std::move(parsed.errors)};
                });
            recovery_vector_.reset();
            if (recovery.candidate) {
                auto parsed = project::parse_vector_asset(
                    *manifest_, recovery.candidate->contents);
                if (parsed.ok()) {
                    recovery_vector_ = std::move(parsed.asset);
                } else {
                    recovery.errors = std::move(parsed.errors);
                }
            }
            if (!recovery.errors.empty()) {
                selection_warnings = std::move(recovery.errors);
            }
        }
        selected_entity_document_path_.clear();
        recovery_entity_.reset();
        selected_animation_document_path_.clear();
        recovery_animation_.reset();
    } else if (kind == StudioResourceKind::material) {
        auto loaded = project::load_material(
            project_root_, *manifest_, match->document_path);
        if (!loaded.ok()) {
            errors_ = std::move(loaded.errors);
            return false;
        }
        imported_texture_.reset();
        imported_vector_.reset();
        created_vector_.reset();
        selected_material_ = std::move(*loaded.asset);
        selected_entity_.reset();
        selected_animation_.reset();
        selected_vector_document_path_.clear();
        recovery_vector_.reset();
        selected_entity_document_path_.clear();
        recovery_entity_.reset();
        selected_animation_document_path_.clear();
        recovery_animation_.reset();
    } else if (kind == StudioResourceKind::entity) {
        auto loaded = project::load_entity(
            project_root_, *manifest_, match->document_path);
        if (!loaded.ok()) {
            errors_ = std::move(loaded.errors);
            return false;
        }
        imported_texture_.reset();
        imported_vector_.reset();
        created_vector_.reset();
        selected_material_.reset();
        selected_entity_ = std::move(*loaded.entity);
        selected_animation_.reset();
        selected_vector_document_path_.clear();
        recovery_vector_.reset();
        selected_entity_document_path_ = match->document_path;
        recovery_entity_.reset();
        auto recovery = project::inspect_recovery(
            project_root_, selected_entity_document_path_,
            [this](const std::string_view contents) {
                auto parsed = project::parse_entity(*manifest_, contents);
                return project::ValidationReport{
                    .errors = std::move(parsed.errors)};
            });
        if (recovery.candidate) {
            auto parsed = project::parse_entity(
                *manifest_, recovery.candidate->contents);
            if (parsed.ok()) recovery_entity_ = std::move(parsed.entity);
            else recovery.errors = std::move(parsed.errors);
        }
        selected_animation_document_path_.clear();
        recovery_animation_.reset();
        if (!recovery.errors.empty()) {
            selection_warnings = std::move(recovery.errors);
        }
    } else {
        auto loaded = project::load_animation(
            project_root_, *manifest_, match->document_path);
        if (!loaded.ok()) {
            errors_ = std::move(loaded.errors);
            return false;
        }
        imported_texture_.reset();
        imported_vector_.reset();
        created_vector_.reset();
        selected_material_.reset();
        selected_entity_.reset();
        selected_animation_ = std::move(*loaded.asset);
        selected_vector_document_path_.clear();
        recovery_vector_.reset();
        selected_entity_document_path_.clear();
        recovery_entity_.reset();
        selected_animation_document_path_ = match->document_path;
        recovery_animation_.reset();
        auto recovery = project::inspect_recovery(
            project_root_, selected_animation_document_path_,
            [this](const std::string_view contents) {
                auto parsed = project::parse_animation(*manifest_, contents);
                return project::ValidationReport{
                    .errors = std::move(parsed.errors)};
            });
        if (recovery.candidate) {
            auto parsed = project::parse_animation(
                *manifest_, recovery.candidate->contents);
            if (parsed.ok()) recovery_animation_ = std::move(parsed.asset);
            else recovery.errors = std::move(parsed.errors);
        }
        if (!recovery.errors.empty()) {
            selection_warnings = std::move(recovery.errors);
        }
    }
    selected_resource_index_ = index;
    errors_ = std::move(selection_warnings);
    return true;
}

bool ProjectSession::set_project_name(
    std::string name, const AutosaveScheduler::Clock::time_point now) {
    if (!has_project()) {
        errors_ = {{project::ErrorCode::invalid_manifest, "project",
                    "a project must be open before editing its name"}};
        return false;
    }
    if (commands_.dirty() && (dirty_document_ == DirtyDocument::vector ||
                              dirty_document_ == DirtyDocument::entity ||
                              dirty_document_ == DirtyDocument::animation)) {
        errors_ = {{project::ErrorCode::invalid_manifest, "project",
                    "save current asset changes before editing project settings"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed) {
            return false;
        }
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
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
    dirty_document_ = DirtyDocument::manifest;
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
    if (commands_.dirty() && (dirty_document_ == DirtyDocument::vector ||
                              dirty_document_ == DirtyDocument::entity ||
                              dirty_document_ == DirtyDocument::animation)) {
        errors_ = {{project::ErrorCode::invalid_manifest, "project",
                    "save current asset changes before editing project settings"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed) {
            return false;
        }
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
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
    dirty_document_ = DirtyDocument::manifest;
    errors_.clear();
    return true;
}

bool ProjectSession::set_selected_vector_node(
    const std::size_t node_index, project::VectorNode node,
    const AutosaveScheduler::Clock::time_point now) {
    if (!created_vector_ ||
        created_vector_->source_kind != project::VectorSourceKind::native ||
        !created_vector_->native ||
        node_index >= created_vector_->native->nodes.size()) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select a native vector node before editing it"}};
        return false;
    }
    if (commands_.dirty() && dirty_document_ != DirtyDocument::vector) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "save project settings before editing a vector"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed) {
            return false;
        }
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
    }
    if (created_vector_->native->nodes[node_index].locked && node.locked) {
        errors_ = {{project::ErrorCode::invalid_asset, "node",
                    "locked vector nodes cannot be edited"}};
        return false;
    }
    auto candidate = *created_vector_;
    candidate.native->nodes[node_index] = node;
    auto validation = project::validate_vector_asset(*manifest_, candidate);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(
            std::make_unique<SetValueCommand<project::VectorNode>>(
                created_vector_->native->nodes[node_index], std::move(node)))) {
        errors_ = {{project::ErrorCode::invalid_asset, "node",
                    "cannot execute the vector node modification"}};
        return false;
    }
    dirty_document_ = DirtyDocument::vector;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::set_selected_entity_node(
    const std::size_t node_index, project::EntityNode node,
    const AutosaveScheduler::Clock::time_point now) {
    if (!selected_entity_ || node_index >= selected_entity_->nodes.size()) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select an entity node before editing it"}};
        return false;
    }
    if (commands_.dirty() && dirty_document_ != DirtyDocument::entity) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "save or undo the current document before editing an entity"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed) {
            return false;
        }
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
    }
    auto candidate = *selected_entity_;
    candidate.nodes[node_index] = node;
    auto validation = project::validate_entity(*manifest_, candidate);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(std::make_unique<SetValueCommand<project::EntityNode>>(
            selected_entity_->nodes[node_index], std::move(node)))) {
        errors_ = {{project::ErrorCode::invalid_asset, "node",
                    "cannot execute the entity node modification"}};
        return false;
    }
    dirty_document_ = DirtyDocument::entity;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::add_selected_entity_node(
    project::EntityNode node, const AutosaveScheduler::Clock::time_point now) {
    if (!selected_entity_) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select an entity before adding a node"}};
        return false;
    }
    if (commands_.dirty() && dirty_document_ != DirtyDocument::entity) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "save or undo the current document before editing an entity"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed)
            return false;
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
    }
    auto candidate = *selected_entity_;
    candidate.nodes.push_back(std::move(node));
    auto validation = project::validate_entity(*manifest_, candidate);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(std::make_unique<ReplaceValueCommand<
            std::vector<project::EntityNode>>>(
            selected_entity_->nodes, std::move(candidate.nodes)))) {
        errors_ = {{project::ErrorCode::invalid_asset, "nodes",
                    "could not add the entity node"}};
        return false;
    }
    dirty_document_ = DirtyDocument::entity;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::duplicate_selected_entity_node(
    const std::size_t node_index,
    const AutosaveScheduler::Clock::time_point now) {
    if (!selected_entity_ || node_index >= selected_entity_->nodes.size()) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select an entity node before duplicating it"}};
        return false;
    }
    auto node = selected_entity_->nodes[node_index];
    const auto base_id = node.id + "-copy";
    node.id = base_id;
    std::size_t suffix = 2;
    while (std::ranges::any_of(selected_entity_->nodes,
                               [&](const auto& candidate) {
                                   return candidate.id == node.id;
                               })) {
        node.id = base_id + "-" + std::to_string(suffix++);
    }
    node.name += " copy";
    return add_selected_entity_node(std::move(node), now);
}

bool ProjectSession::remove_selected_entity_node(
    const std::size_t node_index,
    const AutosaveScheduler::Clock::time_point now) {
    if (!selected_entity_ || node_index >= selected_entity_->nodes.size()) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select an entity node before removing it"}};
        return false;
    }
    const auto& node = selected_entity_->nodes[node_index];
    if (std::ranges::any_of(selected_entity_->nodes,
                            [&](const auto& candidate) {
                                return candidate.parent &&
                                    *candidate.parent == node.id;
                            })) {
        errors_ = {{project::ErrorCode::invalid_asset, "nodes.parent",
                    "remove or reparent child nodes before removing this node"}};
        return false;
    }
    if (commands_.dirty() && dirty_document_ != DirtyDocument::entity) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "save or undo the current document before editing an entity"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed)
            return false;
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
    }
    auto candidate = *selected_entity_;
    candidate.nodes.erase(candidate.nodes.begin() +
                          static_cast<std::ptrdiff_t>(node_index));
    auto validation = project::validate_entity(*manifest_, candidate);
    if (!validation.ok()) {
        errors_ = std::move(validation.errors);
        return false;
    }
    if (!commands_.execute(std::make_unique<ReplaceValueCommand<
            std::vector<project::EntityNode>>>(
            selected_entity_->nodes, std::move(candidate.nodes)))) {
        errors_ = {{project::ErrorCode::invalid_asset, "nodes",
                    "could not remove the entity node"}};
        return false;
    }
    dirty_document_ = DirtyDocument::entity;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::prepare_animation_edit(
    const AutosaveScheduler::Clock::time_point now) {
    if (!selected_animation_) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "select an animation before editing it"}};
        return false;
    }
    if (commands_.dirty() && dirty_document_ != DirtyDocument::animation) {
        errors_ = {{project::ErrorCode::invalid_asset, "selection",
                    "save or undo the current document before editing an animation"}};
        return false;
    }
    if (!commands_.dirty() && dirty_document_ != DirtyDocument::none) {
        if (autosave_.pending() && update_autosave(now) == AutosaveStatus::failed)
            return false;
        commands_.clear();
        autosave_.reset();
        dirty_document_ = DirtyDocument::none;
    }
    return true;
}

bool ProjectSession::set_selected_animation_duration(
    const float duration, const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.set_duration(duration)) {
        errors_ = {{project::ErrorCode::invalid_asset, "duration",
                    "animation duration is invalid"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::set_selected_animation_loop(
    const bool loop, const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.set_loop(loop)) {
        errors_ = {{project::ErrorCode::invalid_asset, "loop",
                    "animation loop flag could not be changed"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::insert_selected_animation_key(
    project::PropertyBinding binding, const float time,
    project::AnimationValue value,
    const project::AnimationInterpolation interpolation,
    const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.insert_key(std::move(binding), time, std::move(value),
                             interpolation)) {
        errors_ = {{project::ErrorCode::invalid_asset, "tracks",
                    "animation key is invalid or conflicts with its track"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::remove_selected_animation_key(
    project::PropertyBinding binding, const std::size_t key_index,
    const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.remove_key(binding, key_index)) {
        errors_ = {{project::ErrorCode::invalid_asset, "tracks.keys",
                    "the key does not exist or is the last key of its track"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::move_selected_animation_key(
    project::PropertyBinding binding, const std::size_t key_index,
    const float time, const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.move_key(binding, key_index, time)) {
        errors_ = {{project::ErrorCode::invalid_asset, "tracks.keys.time",
                    "the key time is invalid"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::insert_selected_animation_marker(
    std::string id, const float time,
    const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.insert_marker(std::move(id), time)) {
        errors_ = {{project::ErrorCode::invalid_asset, "markers",
                    "the marker id or time is invalid or already exists"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

bool ProjectSession::remove_selected_animation_marker(
    const std::string_view id, const AutosaveScheduler::Clock::time_point now) {
    if (!prepare_animation_edit(now)) return false;
    AnimationTimeline timeline(*selected_animation_, commands_);
    if (!timeline.remove_marker(id)) {
        errors_ = {{project::ErrorCode::invalid_asset, "markers",
                    "the marker does not exist"}};
        return false;
    }
    dirty_document_ = DirtyDocument::animation;
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
    if (dirty_document_ == DirtyDocument::vector) {
        if (!created_vector_) {
            commands_.mark_clean();
            autosave_.reset();
            dirty_document_ = DirtyDocument::none;
            errors_.clear();
            return true;
        }
        auto saved = project::publish_native_vector_asset(
            project_root_, *manifest_, *created_vector_);
        if (!saved.ok()) {
            errors_ = std::move(saved.errors);
            return false;
        }
    } else if (dirty_document_ == DirtyDocument::entity) {
        if (!selected_entity_) {
            commands_.mark_clean();
            autosave_.reset();
            dirty_document_ = DirtyDocument::none;
            errors_.clear();
            return true;
        }
        auto saved = project::publish_entity(
            project_root_, *manifest_, *selected_entity_);
        if (!saved.ok()) {
            errors_ = std::move(saved.errors);
            return false;
        }
    } else if (dirty_document_ == DirtyDocument::animation) {
        if (!selected_animation_) {
            commands_.mark_clean();
            autosave_.reset();
            dirty_document_ = DirtyDocument::none;
            errors_.clear();
            return true;
        }
        auto saved = project::publish_animation(
            project_root_, *manifest_, *selected_animation_);
        if (!saved.ok()) {
            errors_ = std::move(saved.errors);
            return false;
        }
    } else {
        auto report = project::save_manifest_atomic(project_root_, *manifest_);
        if (!report.ok()) {
            errors_ = std::move(report.errors);
            return false;
        }
    }
    commands_.mark_clean();
    autosave_.reset();
    dirty_document_ = DirtyDocument::none;
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
        const bool vector_document = dirty_document_ == DirtyDocument::vector &&
            created_vector_ && !selected_vector_document_path_.empty();
        const bool entity_document = dirty_document_ == DirtyDocument::entity &&
            selected_entity_ && !selected_entity_document_path_.empty();
        const bool animation_document = dirty_document_ == DirtyDocument::animation &&
            selected_animation_ && !selected_animation_document_path_.empty();
        auto report = project::save_autosave_atomic(
            project_root_,
            vector_document ? selected_vector_document_path_
            : entity_document ? selected_entity_document_path_
            : animation_document ? selected_animation_document_path_
                            : std::filesystem::path{"project.json"},
            vector_document ? project::serialize_vector_asset(*created_vector_)
            : entity_document ? project::serialize_entity(*selected_entity_)
            : animation_document ? project::serialize_animation(*selected_animation_)
                            : project::serialize_manifest(*manifest_),
            vector_document
                ? project::DocumentValidator{[this](const std::string_view contents) {
                      auto parsed = project::parse_vector_asset(*manifest_, contents);
                      return project::ValidationReport{
                          .errors = std::move(parsed.errors)};
                  }}
                : entity_document
                ? project::DocumentValidator{[this](const std::string_view contents) {
                      auto parsed = project::parse_entity(*manifest_, contents);
                      return project::ValidationReport{
                          .errors = std::move(parsed.errors)};
                  }}
                : animation_document
                ? project::DocumentValidator{[this](const std::string_view contents) {
                      auto parsed = project::parse_animation(*manifest_, contents);
                      return project::ValidationReport{
                          .errors = std::move(parsed.errors)};
                  }}
                : project::DocumentValidator{validate_serialized_manifest});
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
    const bool vector_document = dirty_document_ == DirtyDocument::vector &&
        created_vector_ && !selected_vector_document_path_.empty();
    const bool entity_document = dirty_document_ == DirtyDocument::entity &&
        selected_entity_ && !selected_entity_document_path_.empty();
    const bool animation_document = dirty_document_ == DirtyDocument::animation &&
        selected_animation_ && !selected_animation_document_path_.empty();
    auto report = project::save_autosave_atomic(
        project_root_,
        vector_document ? selected_vector_document_path_
        : entity_document ? selected_entity_document_path_
        : animation_document ? selected_animation_document_path_
                        : std::filesystem::path{"project.json"},
        vector_document ? project::serialize_vector_asset(*created_vector_)
        : entity_document ? project::serialize_entity(*selected_entity_)
        : animation_document ? project::serialize_animation(*selected_animation_)
                        : project::serialize_manifest(*manifest_),
        vector_document
            ? project::DocumentValidator{[this](const std::string_view contents) {
                  auto parsed = project::parse_vector_asset(*manifest_, contents);
                  return project::ValidationReport{
                      .errors = std::move(parsed.errors)};
              }}
            : entity_document
            ? project::DocumentValidator{[this](const std::string_view contents) {
                  auto parsed = project::parse_entity(*manifest_, contents);
                  return project::ValidationReport{
                      .errors = std::move(parsed.errors)};
              }}
            : animation_document
            ? project::DocumentValidator{[this](const std::string_view contents) {
                  auto parsed = project::parse_animation(*manifest_, contents);
                  return project::ValidationReport{
                      .errors = std::move(parsed.errors)};
              }}
            : project::DocumentValidator{validate_serialized_manifest});
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
    if (recovery_vector_) {
        created_vector_ = std::move(recovery_vector_);
        recovery_vector_.reset();
        commands_.clear();
        commands_.mark_dirty();
        dirty_document_ = DirtyDocument::vector;
        autosave_.mark_changed(now);
        errors_.clear();
        return true;
    }
    if (recovery_entity_) {
        selected_entity_ = std::move(recovery_entity_);
        recovery_entity_.reset();
        commands_.clear();
        commands_.mark_dirty();
        dirty_document_ = DirtyDocument::entity;
        autosave_.mark_changed(now);
        errors_.clear();
        return true;
    }
    if (recovery_animation_) {
        selected_animation_ = std::move(recovery_animation_);
        recovery_animation_.reset();
        commands_.clear();
        commands_.mark_dirty();
        dirty_document_ = DirtyDocument::animation;
        autosave_.mark_changed(now);
        errors_.clear();
        return true;
    }
    if (!recovery_manifest_.has_value()) {
        return false;
    }
    manifest_ = std::move(recovery_manifest_);
    recovery_manifest_.reset();
    commands_.clear();
    commands_.mark_dirty();
    dirty_document_ = DirtyDocument::manifest;
    autosave_.mark_changed(now);
    errors_.clear();
    return true;
}

void ProjectSession::decline_recovery() noexcept {
    recovery_manifest_.reset();
    recovery_vector_.reset();
    recovery_entity_.reset();
    recovery_animation_.reset();
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
    return recovery_manifest_.has_value() || recovery_vector_.has_value() ||
        recovery_entity_.has_value() || recovery_animation_.has_value();
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

const std::optional<project::MaterialDefinition>&
ProjectSession::selected_material() const noexcept {
    return selected_material_;
}

const std::optional<project::EntityDefinition>&
ProjectSession::selected_entity() const noexcept {
    return selected_entity_;
}

const std::optional<project::AnimationClip>&
ProjectSession::selected_animation() const noexcept {
    return selected_animation_;
}

const std::vector<StudioResource>&
ProjectSession::resources() const noexcept {
    return resources_;
}

StudioResource* ProjectSession::selected_resource() noexcept {
    if (!selected_resource_index_ || *selected_resource_index_ >= resources_.size()) {
        return nullptr;
    }
    return &resources_[*selected_resource_index_];
}

const StudioResource* ProjectSession::selected_resource() const noexcept {
    if (!selected_resource_index_ || *selected_resource_index_ >= resources_.size()) {
        return nullptr;
    }
    return &resources_[*selected_resource_index_];
}

} // namespace fabric::editor
