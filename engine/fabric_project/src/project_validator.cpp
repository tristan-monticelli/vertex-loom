#include "fabric/project/manifest.hpp"
#include "fabric/project/entity.hpp"
#include "fabric/project/input.hpp"
#include "fabric/project/animation.hpp"
#include "fabric/project/material.hpp"
#include "fabric/project/map.hpp"
#include "fabric/project/resource_registry.hpp"
#include "fabric/project/replay.hpp"
#include "fabric/project/scene.hpp"
#include "fabric/project/texture_asset.hpp"
#include "fabric/project/textured_path.hpp"
#include "fabric/project/vector_asset.hpp"
#include "fabric/project/visual_component.hpp"
#include "fabric/project/visual_composition.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

namespace fabric::project {
namespace {

void add_error(std::vector<Error>& errors, const ErrorCode code,
               std::string field, std::string message) {
    errors.push_back(Error{code, std::move(field), std::move(message)});
}

bool is_within_project(const std::filesystem::path& canonical_root,
                       const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(canonical_root);
    if (relative.empty() || relative == "." || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

template <typename Loader, typename ReferenceCollector>
void inspect_asset_documents(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest,
    const std::filesystem::path& canonical_root,
    const std::filesystem::path& base_directory,
    const std::string_view directory_name,
    const std::string_view document_suffix,
    const std::string_view error_field,
    Loader&& loader,
    ReferenceCollector&& collect_references,
    ResourceRegistry& registry,
    std::vector<Error>& errors) {
    const auto asset_directory = project_root / base_directory / directory_name;
    std::error_code filesystem_error;
    if (!std::filesystem::exists(asset_directory, filesystem_error)) {
        if (filesystem_error) {
            add_error(errors, ErrorCode::io_error, std::string(error_field),
                      "cannot inspect the asset directory");
        }
        return;
    }
    const auto canonical_directory = std::filesystem::weakly_canonical(
        asset_directory, filesystem_error);
    if (filesystem_error ||
        !is_within_project(canonical_root, canonical_directory)) {
        add_error(errors, ErrorCode::invalid_path, std::string(error_field),
                  "asset directory must remain inside the project root");
        return;
    }

    for (std::filesystem::directory_iterator iterator(
             asset_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const auto& entry = *iterator;
        const std::string filename = entry.path().filename().string();
        if (!entry.is_regular_file(filesystem_error) ||
            !filename.ends_with(document_suffix)) {
            continue;
        }
        auto loaded_asset = loader(
            project_root, manifest,
            entry.path().lexically_relative(project_root));
        if (loaded_asset.ok()) {
            auto registration = registry.register_resource(ResourceEntry{
                .document = loaded_asset.asset->document,
                .document_path =
                    entry.path().lexically_relative(project_root),
                .references = collect_references(*loaded_asset.asset),
            });
            errors.insert(
                errors.end(),
                std::make_move_iterator(registration.errors.begin()),
                std::make_move_iterator(registration.errors.end()));
        }
        errors.insert(errors.end(),
                      std::make_move_iterator(loaded_asset.errors.begin()),
                      std::make_move_iterator(loaded_asset.errors.end()));
    }
    if (filesystem_error) {
        add_error(errors, ErrorCode::io_error, std::string(error_field),
                  "cannot inspect asset documents");
    }
}

void register_map_prefabs(const std::filesystem::path& project_root,
                          const ProjectManifest& manifest,
                          ResourceRegistry& registry,
                          std::vector<Error>& errors) {
    const auto map_directory = project_root / manifest.directories.maps;
    std::error_code filesystem_error;
    if (!std::filesystem::exists(map_directory, filesystem_error) ||
        filesystem_error) return;
    for (std::filesystem::directory_iterator iterator(
             map_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const auto filename = iterator->path().filename().string();
        if (!iterator->is_regular_file(filesystem_error) ||
            !filename.ends_with(".map.json")) continue;
        const auto loaded_map = load_map(
            project_root, manifest,
            iterator->path().lexically_relative(project_root));
        if (!loaded_map.ok()) continue;
        for (const auto& prefab : loaded_map.asset->prefabs) {
            auto registration = registry.register_resource({
                .document = {.schema_version = current_map_schema_version,
                             .type = "prefab",
                             .id = {.value = prefab.id},
                             .name = prefab.id},
                .document_path = iterator->path().lexically_relative(project_root),
                .references = {prefab.entity}});
            errors.insert(errors.end(),
                          std::make_move_iterator(registration.errors.begin()),
                          std::make_move_iterator(registration.errors.end()));
        }
    }
}

void inspect_input_documents(const std::filesystem::path& project_root,
                             const ProjectManifest& manifest,
                             const std::filesystem::path& canonical_root,
                             ResourceRegistry& registry,
                             std::vector<Error>& errors) {
    const auto input_directory = project_root / manifest.directories.assets / "input";
    std::error_code filesystem_error;
    if (!std::filesystem::exists(input_directory, filesystem_error)) {
        if (filesystem_error)
            add_error(errors, ErrorCode::io_error, "assets.input",
                      "cannot inspect input directory");
        return;
    }
    const auto canonical_directory = std::filesystem::weakly_canonical(
        input_directory, filesystem_error);
    if (filesystem_error || !is_within_project(canonical_root, canonical_directory)) {
        add_error(errors, ErrorCode::invalid_path, "assets.input",
                  "input directory must remain inside the project root");
        return;
    }
    for (std::filesystem::directory_iterator iterator(
             input_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const auto& entry = *iterator;
        const auto filename = entry.path().filename().string();
        if (!entry.is_regular_file(filesystem_error) ||
            !filename.ends_with(".input.json")) continue;
        const auto loaded = load_input(
            project_root, manifest, entry.path().lexically_relative(project_root));
        if (loaded.ok()) {
            auto registration = registry.register_resource({
                .document = loaded.input->document,
                .document_path = entry.path().lexically_relative(project_root),
                .references = {}});
            errors.insert(errors.end(), registration.errors.begin(),
                          registration.errors.end());
        }
        errors.insert(errors.end(), loaded.errors.begin(), loaded.errors.end());
    }
    if (filesystem_error)
        add_error(errors, ErrorCode::io_error, "assets.input",
                  "cannot inspect input documents");
}

void validate_composition_raster_views(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, std::vector<Error>& errors) {
    const auto directory = project_root / manifest.directories.assets /
        "compositions";
    std::error_code filesystem_error;
    if (!std::filesystem::exists(directory, filesystem_error) ||
        filesystem_error) return;
    for (std::filesystem::directory_iterator iterator(directory, filesystem_error),
         end; !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const auto filename = iterator->path().filename().string();
        if (!iterator->is_regular_file(filesystem_error) ||
            !filename.ends_with(".composition.json")) continue;
        const auto composition = load_visual_composition(
            project_root, manifest,
            iterator->path().lexically_relative(project_root));
        if (!composition.ok()) continue;
        for (std::size_t index = 0; index < composition.asset->layers.size();
             ++index) {
            const auto& layer = composition.asset->layers[index];
            if (layer.kind != VisualLayerKind::raster || !layer.raster_view) {
                continue;
            }
            const auto texture = load_texture_asset(
                project_root, manifest,
                texture_document_path(manifest, layer.resource.id));
            if (!texture.ok()) continue;
            const auto validation = validate_raster_view(
                *layer.raster_view, texture.asset->width,
                texture.asset->height);
            for (const auto& error : validation.errors) {
                add_error(errors, error.code,
                          composition.asset->document.id.value + ".layers[" +
                              std::to_string(index) + "].rasterView." +
                              error.field,
                          error.message);
            }
        }
    }
}

void validate_visual_component_bindings(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, std::vector<Error>& errors) {
    const auto directory = project_root / manifest.directories.assets /
        "components";
    std::error_code filesystem_error;
    if (!std::filesystem::exists(directory, filesystem_error) ||
        filesystem_error) return;
    for (std::filesystem::directory_iterator iterator(directory, filesystem_error),
         end; !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const auto filename = iterator->path().filename().string();
        if (!iterator->is_regular_file(filesystem_error) ||
            !filename.ends_with(".component.json")) continue;
        const auto component = load_visual_component(
            project_root, manifest,
            iterator->path().lexically_relative(project_root));
        if (!component.ok()) continue;
        const auto composition = load_visual_composition(
            project_root, manifest,
            visual_composition_document_path(
                manifest, component.asset->composition.id));
        if (!composition.ok()) continue;
        for (std::size_t index = 0; index < component.asset->parameters.size();
             ++index) {
            const auto& parameter = component.asset->parameters[index];
            if (!std::ranges::any_of(
                    composition.asset->layers, [&](const auto& layer) {
                        return layer.id == parameter.target.node_id;
                    })) {
                add_error(errors, ErrorCode::missing_resource,
                          component.asset->document.id.value +
                              ".parameters[" + std::to_string(index) +
                              "].target.nodeId",
                          "parameter target layer is missing from the internal composition");
            }
        }
    }
}

void validate_visual_component_instances(
    const std::filesystem::path& project_root,
    const ProjectManifest& manifest, std::vector<Error>& errors) {
    const auto directory = project_root / manifest.directories.assets /
        "compositions";
    std::error_code filesystem_error;
    if (!std::filesystem::exists(directory, filesystem_error) ||
        filesystem_error) return;
    for (std::filesystem::directory_iterator iterator(directory, filesystem_error),
         end; !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const auto filename = iterator->path().filename().string();
        if (!iterator->is_regular_file(filesystem_error) ||
            !filename.ends_with(".composition.json")) continue;
        const auto composition = load_visual_composition(
            project_root, manifest,
            iterator->path().lexically_relative(project_root));
        if (!composition.ok()) continue;
        for (std::size_t index = 0; index < composition.asset->layers.size();
             ++index) {
            const auto& layer = composition.asset->layers[index];
            if (layer.kind != VisualLayerKind::component ||
                !layer.component_instance) continue;
            const auto component = load_visual_component(
                project_root, manifest,
                visual_component_document_path(manifest, layer.resource.id));
            if (!component.ok()) continue;
            const auto resolved = resolve_visual_component_instance(
                *component.asset, *layer.component_instance);
            for (const auto& error : resolved.errors) {
                add_error(errors, error.code,
                          composition.asset->document.id.value +
                              ".layers[" + std::to_string(index) +
                              "].componentInstance." + error.field,
                          error.message);
            }
        }
    }
}

} // namespace

ManifestResult load_manifest(const std::filesystem::path& project_root) {
    ManifestResult result;
    const auto manifest_path = project_root / "project.json";
    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        add_error(result.errors, ErrorCode::missing_file, "project.json",
                  "cannot open project manifest");
        return result;
    }

    const std::string contents{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        add_error(result.errors, ErrorCode::io_error, "project.json",
                  "failed while reading project manifest");
        return result;
    }
    return parse_manifest(contents);
}

ManifestResult load_project(const std::filesystem::path& project_root) {
    ManifestResult result;
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(project_root, filesystem_error)) {
        add_error(result.errors, ErrorCode::missing_directory, "project",
                  "project root is not an accessible directory");
        return result;
    }

    ManifestResult loaded = load_manifest(project_root);
    if (!loaded.ok()) {
        return loaded;
    }

    filesystem_error.clear();
    const auto canonical_root = std::filesystem::weakly_canonical(
        project_root, filesystem_error);
    if (filesystem_error) {
        add_error(result.errors, ErrorCode::io_error, "project",
                  "cannot resolve the project root");
        return result;
    }

    const auto& directories = loaded.manifest->directories;
    const std::array required_directories{
        std::pair{"directories.assets", &directories.assets},
        std::pair{"directories.entities", &directories.entities},
        std::pair{"directories.maps", &directories.maps},
        std::pair{"directories.scenes", &directories.scenes},
        std::pair{"directories.schemas", &directories.schemas},
    };
    for (const auto& [field, relative_path] : required_directories) {
        filesystem_error.clear();
        if (!std::filesystem::is_directory(project_root / *relative_path,
                                           filesystem_error)) {
            add_error(result.errors, ErrorCode::missing_directory, field,
                      "required project directory is missing or inaccessible");
            continue;
        }
        filesystem_error.clear();
        const auto canonical_directory = std::filesystem::weakly_canonical(
            project_root / *relative_path, filesystem_error);
        if (filesystem_error ||
            !is_within_project(canonical_root, canonical_directory)) {
            add_error(result.errors, ErrorCode::invalid_path, field,
                      "resolved directory must remain inside the project root");
        }
    }
    if (!result.errors.empty()) {
        return result;
    }

    ResourceRegistry registry;
    const auto no_references = [](const auto&) {
        return std::vector<ResourceReference>{};
    };
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.assets, "textures",
        ".texture.json", "assets.textures", load_texture_asset,
        no_references,
        registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.assets, "vectors",
        ".vector.json", "assets.vectors", load_vector_asset,
        vector_resource_references, registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.assets, "materials",
        ".material.json", "assets.materials", load_material,
        material_resource_references, registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root,
        loaded.manifest->directories.assets, "components",
        ".component.json", "assets.components", load_visual_component,
        visual_component_resource_references, registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root,
        loaded.manifest->directories.assets, "compositions",
        ".composition.json", "assets.compositions", load_visual_composition,
        visual_composition_resource_references, registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root,
        loaded.manifest->directories.assets, "paths",
        ".textured-path.json", "assets.paths", load_textured_path,
        textured_path_resource_references, registry, result.errors);
    validate_composition_raster_views(
        project_root, *loaded.manifest, result.errors);
    validate_visual_component_bindings(
        project_root, *loaded.manifest, result.errors);
    validate_visual_component_instances(
        project_root, *loaded.manifest, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.assets, "animations",
        ".animation.json", "assets.animations", load_animation,
        animation_resource_references, registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.assets, "replays",
        ".replay.json", "assets.replays", load_replay, replay_resource_references,
        registry, result.errors);
    inspect_input_documents(project_root, *loaded.manifest, canonical_root,
                            registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.maps, "",
        ".map.json", "maps", load_map, map_resource_references,
        registry, result.errors);
    register_map_prefabs(project_root, *loaded.manifest, registry, result.errors);
    inspect_asset_documents(
        project_root, *loaded.manifest, canonical_root, loaded.manifest->directories.scenes, "",
        ".scene.json", "scenes", load_scene, scene_resource_references,
        registry, result.errors);
    const auto entity_directory = project_root / loaded.manifest->directories.entities;
    std::error_code entity_error;
    if (std::filesystem::exists(entity_directory, entity_error)) {
        const auto canonical_entity_directory = std::filesystem::weakly_canonical(
            entity_directory, entity_error);
        if (entity_error || !is_within_project(canonical_root,
                                               canonical_entity_directory)) {
            add_error(result.errors, ErrorCode::invalid_path, "entities",
                      "entity directory must remain inside the project root");
        } else {
            for (std::filesystem::directory_iterator iterator(
                     entity_directory, entity_error), end;
                 !entity_error && iterator != end;
                 iterator.increment(entity_error)) {
                const auto filename = iterator->path().filename().string();
                if (!iterator->is_regular_file(entity_error) ||
                    !filename.ends_with(".entity.json")) continue;
                auto loaded_entity = load_entity(
                    project_root, *loaded.manifest,
                    iterator->path().lexically_relative(project_root));
                if (loaded_entity.ok()) {
                    for (std::size_t node_index = 0;
                         node_index < loaded_entity.entity->nodes.size();
                         ++node_index) {
                        const auto& node = loaded_entity.entity->nodes[node_index];
                        if (node.drawable.kind !=
                                EntityDrawableKind::visual_component ||
                            !node.drawable.resource ||
                            !node.drawable.component_instance) continue;
                        const auto component = load_visual_component(
                            project_root, *loaded.manifest,
                            visual_component_document_path(
                                *loaded.manifest,
                                node.drawable.resource->id));
                        if (!component.ok()) continue;
                        const auto resolved = resolve_visual_component_instance(
                            *component.asset,
                            *node.drawable.component_instance);
                        for (const auto& error : resolved.errors) {
                            add_error(
                                result.errors, error.code,
                                loaded_entity.entity->document.id.value +
                                    ".nodes[" + std::to_string(node_index) +
                                    "].componentInstance." + error.field,
                                error.message);
                        }
                    }
                    auto registration = registry.register_resource({
                        .document = loaded_entity.entity->document,
                        .document_path = iterator->path().lexically_relative(project_root),
                        .references = entity_resource_references(*loaded_entity.entity),
                    });
                    result.errors.insert(result.errors.end(),
                                         std::make_move_iterator(registration.errors.begin()),
                                         std::make_move_iterator(registration.errors.end()));
                }
                result.errors.insert(result.errors.end(),
                                     std::make_move_iterator(loaded_entity.errors.begin()),
                                     std::make_move_iterator(loaded_entity.errors.end()));
            }
            if (entity_error) add_error(result.errors, ErrorCode::io_error,
                                        "entities", "cannot inspect entity documents");
        }
    } else if (entity_error) {
        add_error(result.errors, ErrorCode::io_error, "entities",
                  "cannot inspect entity directory");
    }
    auto graph_validation = registry.validate();
    result.errors.insert(
        result.errors.end(),
        std::make_move_iterator(graph_validation.errors.begin()),
        std::make_move_iterator(graph_validation.errors.end()));
    if (result.errors.empty()) {
        result.manifest = std::move(loaded.manifest);
    }
    return result;
}

ValidationReport validate_project(const std::filesystem::path& project_root) {
    auto loaded = load_project(project_root);
    return ValidationReport{.errors = std::move(loaded.errors)};
}

} // namespace fabric::project
