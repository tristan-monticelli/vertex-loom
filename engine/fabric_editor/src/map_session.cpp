#include "fabric/editor/map_session.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

namespace fabric::editor {
namespace {

class MapSnapshotCommand final : public Command {
public:
    MapSnapshotCommand(project::MapDocument& target, project::MapDocument before,
                       project::MapDocument after)
        : target_(target), before_(std::move(before)), after_(std::move(after)) {}

    bool execute() override { target_ = after_; return true; }
    bool undo() override { target_ = before_; return true; }

private:
    project::MapDocument& target_;
    project::MapDocument before_;
    project::MapDocument after_;
};

bool commit(CommandStack& commands, project::MapDocument& target,
            project::MapDocument before, project::MapDocument after) {
    if (!project::validate_map(project::ProjectManifest{}, after).ok()) return false;
    return commands.execute(std::make_unique<MapSnapshotCommand>(
        target, std::move(before), std::move(after)));
}

std::optional<std::size_t> find_instance(const project::MapDocument& map,
                                         const core::ResourceId& id) {
    for (std::size_t index = 0; index < map.instances.size(); ++index)
        if (map.instances[index].id == id.value) return index;
    return std::nullopt;
}

std::optional<std::size_t> find_layer(const project::MapDocument& map,
                                      const core::ResourceId& id) {
    for (std::size_t index = 0; index < map.layers.size(); ++index)
        if (map.layers[index].id == id.value) return index;
    return std::nullopt;
}

std::optional<std::size_t> find_prefab(const project::MapDocument& map,
                                       const core::ResourceId& id) {
    for (std::size_t index = 0; index < map.prefabs.size(); ++index)
        if (map.prefabs[index].id == id.value) return index;
    return std::nullopt;
}

std::optional<std::size_t> find_event(const project::MapDocument& map,
                                      const core::ResourceId& id) {
    for (std::size_t index = 0; index < map.events.size(); ++index)
        if (map.events[index].id == id) return index;
    return std::nullopt;
}

std::optional<std::size_t> find_trigger(const project::MapDocument& map,
                                        const core::ResourceId& id) {
    for (std::size_t index = 0; index < map.triggers.size(); ++index)
        if (map.triggers[index].id == id.value) return index;
    return std::nullopt;
}

void set_chunk(project::MapInstance& instance) {
    instance.chunk_x = static_cast<std::int32_t>(
        std::floor(instance.transform.position.x / project::map_chunk_size));
    instance.chunk_y = static_cast<std::int32_t>(
        std::floor(instance.transform.position.y / project::map_chunk_size));
}

bool instance_locked(const project::MapDocument& map, const std::size_t index) {
    const auto& instance = map.instances[index];
    const auto layer = find_layer(map, {.value = instance.layer_id});
    return layer && map.layers[*layer].locked;
}

} // namespace

core::Vec2 MapSession::snap_position(const core::Vec2 position,
                                     const MapSnapSettings snapping) noexcept {
    if (!snapping.enabled || !std::isfinite(snapping.grid_size) ||
        snapping.grid_size <= 0.0F || !std::isfinite(snapping.origin.x) ||
        !std::isfinite(snapping.origin.y)) return position;
    return {snapping.origin.x + std::round((position.x - snapping.origin.x) /
                                               snapping.grid_size) * snapping.grid_size,
            snapping.origin.y + std::round((position.y - snapping.origin.y) /
                                               snapping.grid_size) * snapping.grid_size};
}

bool MapSession::create(const std::filesystem::path& project_root,
                        const project::MapDocument& map) {
    project_root_ = project_root;
    auto loaded = project::load_manifest(project_root);
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    if (!project::validate_map(*loaded.manifest, map).ok()) return false;
    manifest_ = std::move(*loaded.manifest);
    map_ = map;
    commands_.clear();
    errors_.clear();
    return save();
}

bool MapSession::open(const std::filesystem::path& project_root,
                      const core::ResourceId& map_id) {
    project_root_ = project_root;
    auto loaded = project::load_manifest(project_root);
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    auto loaded_map = project::load_map(
        project_root, *loaded.manifest,
        project::map_document_path(*loaded.manifest, map_id));
    if (!loaded_map.ok()) { errors_ = std::move(loaded_map.errors); return false; }
    manifest_ = std::move(*loaded.manifest);
    map_ = std::move(*loaded_map.asset);
    commands_.clear();
    errors_.clear();
    return true;
}

bool MapSession::save() {
    if (!map_ || !manifest_) return false;
    const auto result = project::publish_map(project_root_, *manifest_, *map_);
    if (!result.ok()) { errors_ = result.errors; return false; }
    commands_.mark_clean();
    errors_.clear();
    return true;
}

bool MapSession::place_instance(project::MapInstance instance,
                                const MapSnapSettings snapping) {
    if (!map_ || find_instance(*map_, {.value = instance.id})) return false;
    const auto layer = find_layer(*map_, {.value = instance.layer_id});
    if (!layer || map_->layers[*layer].locked) return false;
    instance.transform.position = snap_position(instance.transform.position, snapping);
    set_chunk(instance);
    auto next = *map_;
    next.instances.push_back(std::move(instance));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::remove_instance(const core::ResourceId& instance_id) {
    if (!map_) return false;
    const auto found = find_instance(*map_, instance_id);
    if (!found) return false;
    if (instance_locked(*map_, *found)) return false;
    auto next = *map_;
    next.instances.erase(next.instances.begin() + static_cast<std::ptrdiff_t>(*found));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::remove_instances(const std::vector<core::ResourceId>& instance_ids) {
    if (!map_ || instance_ids.empty()) return false;
    std::set<std::string> unique_ids;
    std::vector<std::size_t> indices;
    indices.reserve(instance_ids.size());
    for (const auto& instance_id : instance_ids) {
        if (!unique_ids.insert(instance_id.value).second) return false;
        const auto found = find_instance(*map_, instance_id);
        if (!found || instance_locked(*map_, *found)) return false;
        indices.push_back(*found);
    }
    std::sort(indices.rbegin(), indices.rend());
    auto next = *map_;
    for (const auto index : indices)
        next.instances.erase(next.instances.begin() + static_cast<std::ptrdiff_t>(index));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::duplicate_instance(const core::ResourceId& instance_id,
                                     const core::Vec2 offset,
                                     const MapSnapSettings snapping) {
    if (!map_ || !std::isfinite(offset.x) || !std::isfinite(offset.y)) return false;
    const auto found = find_instance(*map_, instance_id);
    if (!found || instance_locked(*map_, *found)) return false;
    auto copy = map_->instances[*found];
    const auto base_id = copy.id + "-copy";
    copy.id = base_id;
    for (std::size_t suffix = 2; find_instance(*map_, {.value = copy.id}); ++suffix)
        copy.id = base_id + "-" + std::to_string(suffix);
    copy.transform.position.x += offset.x;
    copy.transform.position.y += offset.y;
    copy.transform.position = snap_position(copy.transform.position, snapping);
    set_chunk(copy);
    auto next = *map_;
    next.instances.push_back(std::move(copy));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::reorder_instance(const core::ResourceId& instance_id,
                                  const std::size_t target_index) {
    if (!map_ || target_index >= map_->instances.size()) return false;
    const auto found = find_instance(*map_, instance_id);
    if (!found || instance_locked(*map_, *found) || *found == target_index) return false;
    auto next = *map_;
    auto instance = std::move(next.instances[*found]);
    next.instances.erase(next.instances.begin() + static_cast<std::ptrdiff_t>(*found));
    next.instances.insert(next.instances.begin() + static_cast<std::ptrdiff_t>(target_index),
                          std::move(instance));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_instance_transform(const core::ResourceId& instance_id,
                                         core::Transform transform,
                                         const MapSnapSettings snapping) {
    if (!map_) return false;
    const auto found = find_instance(*map_, instance_id);
    if (!found) return false;
    if (instance_locked(*map_, *found)) return false;
    auto next = *map_;
    transform.position = snap_position(transform.position, snapping);
    next.instances[*found].transform = transform;
    set_chunk(next.instances[*found]);
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_instance_layer(const core::ResourceId& instance_id,
                                    const core::ResourceId& layer_id) {
    if (!map_) return false;
    const auto instance_index = find_instance(*map_, instance_id);
    const auto target_layer = find_layer(*map_, layer_id);
    if (!instance_index || !target_layer ||
        map_->layers[*target_layer].kind != project::MapLayerKind::instances ||
        instance_locked(*map_, *instance_index) || map_->layers[*target_layer].locked)
        return false;
    auto next = *map_;
    next.instances[*instance_index].layer_id = layer_id.value;
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_instances_layer(const std::vector<core::ResourceId>& instance_ids,
                                     const core::ResourceId& layer_id) {
    if (!map_ || instance_ids.empty()) return false;
    const auto target_layer = find_layer(*map_, layer_id);
    if (!target_layer ||
        map_->layers[*target_layer].kind != project::MapLayerKind::instances ||
        map_->layers[*target_layer].locked) return false;
    auto next = *map_;
    for (const auto& instance_id : instance_ids) {
        const auto instance_index = find_instance(*map_, instance_id);
        if (!instance_index || instance_locked(*map_, *instance_index)) return false;
        next.instances[*instance_index].layer_id = layer_id.value;
    }
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_instance_property(const core::ResourceId& instance_id,
                                       project::MapProperty property) {
    if (!map_ || property.id.empty()) return false;
    const auto found = find_instance(*map_, instance_id);
    if (!found) return false;
    if (instance_locked(*map_, *found)) return false;
    auto next = *map_;
    auto& properties = next.instances[*found].properties;
    const auto existing = std::find_if(properties.begin(), properties.end(),
        [&](const auto& candidate) { return candidate.id == property.id; });
    if (existing != properties.end()) existing->value = std::move(property.value);
    else properties.push_back(std::move(property));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::translate_instances(
    const std::vector<core::ResourceId>& instance_ids, const core::Vec2 delta,
    const MapSnapSettings snapping) {
    if (!map_ || instance_ids.empty() || !std::isfinite(delta.x) ||
        !std::isfinite(delta.y)) return false;
    std::set<std::string> unique_ids;
    std::vector<std::size_t> indices;
    indices.reserve(instance_ids.size());
    for (const auto& id : instance_ids) {
        if (!unique_ids.insert(id.value).second) return false;
        const auto found = find_instance(*map_, id);
        if (!found || instance_locked(*map_, *found)) return false;
        indices.push_back(*found);
    }
    auto next = *map_;
    for (const auto index : indices) {
        auto& transform = next.instances[index].transform;
        transform.position.x += delta.x;
        transform.position.y += delta.y;
        transform.position = snap_position(transform.position, snapping);
        set_chunk(next.instances[index]);
    }
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_layer_visibility(const core::ResourceId& layer_id,
                                      const bool visible) {
    if (!map_) return false;
    const auto found = find_layer(*map_, layer_id);
    if (!found) return false;
    auto next = *map_;
    next.layers[*found].visible = visible;
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_layer_locked(const core::ResourceId& layer_id,
                                  const bool locked) {
    if (!map_) return false;
    const auto found = find_layer(*map_, layer_id);
    if (!found) return false;
    auto next = *map_;
    next.layers[*found].locked = locked;
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_layer_depth(const core::ResourceId& layer_id,
                                 const float depth) {
    if (!map_ || !std::isfinite(depth)) return false;
    const auto found = find_layer(*map_, layer_id);
    if (!found) return false;
    auto next = *map_;
    next.layers[*found].depth = depth;
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_prefab_override(const core::ResourceId& prefab_id,
                                     project::MapProperty property) {
    if (!map_ || property.id.empty()) return false;
    const auto found = find_prefab(*map_, prefab_id);
    if (!found) return false;
    auto next = *map_;
    auto& overrides = next.prefabs[*found].overrides;
    const auto existing = std::find_if(overrides.begin(), overrides.end(),
        [&](const auto& candidate) { return candidate.id == property.id; });
    if (existing != overrides.end()) existing->value = std::move(property.value);
    else overrides.push_back(std::move(property));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

std::vector<project::MapProperty> MapSession::effective_instance_properties(
    const core::ResourceId& instance_id) const {
    if (!map_) return {};
    const auto instance_index = find_instance(*map_, instance_id);
    if (!instance_index) return {};
    const auto& instance = map_->instances[*instance_index];
    std::vector<project::MapProperty> result;
    if (instance.prefab) {
        const auto prefab = find_prefab(*map_, instance.prefab->id);
        if (prefab) result = map_->prefabs[*prefab].overrides;
    }
    for (const auto& property : instance.properties) {
        const auto existing = std::find_if(result.begin(), result.end(),
            [&](const auto& candidate) { return candidate.id == property.id; });
        if (existing != result.end()) existing->value = property.value;
        else result.push_back(property);
    }
    return result;
}

bool MapSession::declare_event(project::MapEventDefinition event) {
    if (!map_ || !core::ResourceId::is_valid(event.id.value) ||
        find_event(*map_, event.id)) return false;
    auto next = *map_;
    next.events.push_back(std::move(event));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::remove_event(const core::ResourceId& event_id) {
    if (!map_) return false;
    const auto found = find_event(*map_, event_id);
    if (!found) return false;
    for (const auto& trigger : map_->triggers)
        if (trigger.event_id == event_id) return false;
    auto next = *map_;
    next.events.erase(next.events.begin() + static_cast<std::ptrdiff_t>(*found));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_event_payload(const core::ResourceId& event_id,
                                   std::vector<project::MapProperty> payload) {
    if (!map_) return false;
    const auto found = find_event(*map_, event_id);
    if (!found) return false;
    std::set<std::string> property_ids;
    for (const auto& property : payload)
        if (property.id.empty() || !property_ids.insert(property.id).second) return false;
    auto next = *map_;
    next.events[*found].payload = std::move(payload);
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::add_trigger(project::TriggerDefinition trigger) {
    if (!map_ || !core::ResourceId::is_valid(trigger.id) ||
        find_trigger(*map_, {.value = trigger.id}) ||
        !find_event(*map_, trigger.event_id)) return false;
    auto next = *map_;
    next.triggers.push_back(std::move(trigger));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::remove_trigger(const core::ResourceId& trigger_id) {
    if (!map_) return false;
    const auto found = find_trigger(*map_, trigger_id);
    if (!found) return false;
    auto next = *map_;
    next.triggers.erase(next.triggers.begin() + static_cast<std::ptrdiff_t>(*found));
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_trigger(const std::size_t trigger_index,
                             project::TriggerDefinition trigger) {
    if (!map_ || trigger_index >= map_->triggers.size() ||
        !core::ResourceId::is_valid(trigger.id) ||
        !find_event(*map_, trigger.event_id) ||
        trigger.collision_index >= map_->collisions.size()) return false;
    const auto layer = find_layer(*map_, {.value = trigger.layer_id});
    if (layer && map_->layers[*layer].locked) return false;
    auto next = *map_;
    const auto duplicate = std::find_if(next.triggers.begin(), next.triggers.end(),
        [&](const auto& candidate) {
            return candidate.id == trigger.id &&
                   candidate.id != next.triggers[trigger_index].id;
        });
    if (duplicate != next.triggers.end()) return false;
    next.triggers[trigger_index] = std::move(trigger);
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::set_collision_shape(const std::size_t collision_index,
                                     project::CollisionShape shape) {
    if (!map_ || collision_index >= map_->collisions.size()) return false;
    const auto layer = find_layer(*map_, {.value = shape.layer_id});
    if (layer && map_->layers[*layer].locked) return false;
    auto next = *map_;
    next.collisions[collision_index] = std::move(shape);
    auto before = *map_;
    return commit(commands_, *map_, std::move(before), std::move(next));
}

bool MapSession::undo() { return commands_.undo(); }
bool MapSession::redo() { return commands_.redo(); }

} // namespace fabric::editor
