#include "animation_publish_probe.hpp"

#include "fabric/project/map.hpp"
#include "fabric/project/map_package.hpp"
#include "fabric/runtime/preview_runtime.hpp"

#include <algorithm>
#include <optional>
#include <ranges>

namespace fabric::asset_studio {

AnimationPublishProof prove_published_animation_workflow(
    const std::filesystem::path& project_root,
    const project::ProjectManifest& manifest,
    const std::string& entity_id,
    const std::string& animation_id,
    const std::string& target_node_id,
    const float evaluation_time) {
    AnimationPublishProof proof;
    project::MapDocument map;
    map.document.id = {.value = "animation-workflow-runtime"};
    map.document.name = "Animation workflow runtime proof";
    map.layers.push_back({
        .id = "instances",
        .name = "Instances",
        .kind = project::MapLayerKind::instances,
    });
    map.instances.push_back({
        .id = "workflow-instance",
        .entity = project::ResourceReference{
            {.value = entity_id}, "entity"},
        .layer_id = "instances",
        .properties = {{
            "animation",
            project::ResourceReference{{.value = animation_id}, "animation"},
        }},
    });

    proof.map_published = project::publish_map(
        project_root, manifest, map).ok();
    if (!proof.map_published) return proof;

    const auto package_root =
        project_root.parent_path() / "animation-workflow.map-package";
    const auto published = project::publish_map_package(
        project_root, map.document.id, package_root);
    proof.package_published = published.ok();
    if (!proof.package_published) return proof;
    proof.package_contains_animation = std::ranges::any_of(
        published.manifest->resources, [&](const auto& resource) {
            return resource.resource.expected_type == "animation" &&
                resource.resource.id.value == animation_id;
        });

    runtime::PreviewRuntime runtime;
    proof.runtime_loaded = runtime.load({
        .package_root = package_root,
        .mode = runtime::RuntimeMode::smoke_test,
        .frame_limit = 1U,
    });
    if (!proof.runtime_loaded) return proof;
    proof.runtime_ran = runtime.run();

    const auto evaluated = runtime.evaluate_instance_animation(
        "workflow-instance", evaluation_time);
    std::optional<core::Vec2> evaluated_position;
    if (evaluated && evaluated->ok()) {
        const auto property = std::ranges::find_if(
            evaluated->properties, [&](const auto& candidate) {
            return candidate.binding.node_id == target_node_id &&
                candidate.binding.component_id == "transform" &&
                candidate.binding.property_id == "position";
        });
        if (property != evaluated->properties.end())
            evaluated_position = std::get_if<core::Vec2>(&property->value)
                ? std::optional{std::get<core::Vec2>(property->value)}
                : std::nullopt;
    }
    proof.animation_evaluated = evaluated_position.has_value();
    const auto nodes = runtime.evaluate_instance_nodes(
        "workflow-instance", evaluation_time);
    proof.target_node_evaluated = nodes && evaluated_position &&
        std::ranges::any_of(*nodes, [&](const auto& node) {
            return node.id == target_node_id &&
                node.transform.position == *evaluated_position;
        });
    const auto marker_hits = runtime.animation_markers(
        {.value = animation_id}, std::max(0.0F, evaluation_time - 0.01F),
        evaluation_time + 0.01F);
    proof.marker_evaluated = std::ranges::any_of(
        marker_hits, [](const auto& marker) {
            return marker.id.starts_with("event-");
        });
    return proof;
}

} // namespace fabric::asset_studio
