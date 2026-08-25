#include "fabric/editor/mechanic_session.hpp"

#include "fabric/project/document_storage.hpp"

#include <algorithm>
#include <memory>
#include <ranges>
#include <utility>

namespace fabric::editor {
namespace {

class MechanicSnapshotCommand final : public Command {
public:
    MechanicSnapshotCommand(project::MechanicGraph& target,
                            project::MechanicGraph before,
                            project::MechanicGraph after)
        : target_(target), before_(std::move(before)), after_(std::move(after)) {}
    bool execute() override { target_ = after_; return true; }
    bool undo() override { target_ = before_; return true; }
private:
    project::MechanicGraph& target_;
    project::MechanicGraph before_;
    project::MechanicGraph after_;
};

project::MechanicValue default_value(const project::MechanicValueType type,
                                     const std::string_view id) {
    using Type = project::MechanicValueType;
    switch (type) {
    case Type::boolean: return false;
    case Type::integer: return std::int64_t{};
    case Type::scalar:
        if (id == "density" || id == "friction" || id == "max-torque")
            return 1.0F;
        return 0.0F;
    case Type::text:
        if (id == "body-type") return std::string{"dynamic"};
        if (id == "event-id") return std::string{"event"};
        if (id == "mode") return std::string{"emit"};
        return std::string{"value"};
    case Type::vec2: return core::Vec2{1.0F, 1.0F};
    case Type::resource:
        return project::ResourceReference{{.value = "resource"}, "entity"};
    case Type::body_handle:
    case Type::pivot_handle:
    case Type::joint_handle: break;
    }
    return false;
}

project::MechanicNodeDefinition make_node(const project::MechanicNodeKind kind,
                                          std::string id) {
    const auto& schema = project::mechanic_node_schema(kind);
    project::MechanicNodeDefinition node{
        .id = std::move(id), .type = std::string{schema.type}};
    for (const auto& port : schema.ports)
        node.ports.push_back({
            .id = std::string{port.id}, .name = std::string{port.id},
            .direction = port.direction, .type = port.type});
    for (const auto& property : schema.properties) {
        if (property.required ||
            property.type != project::MechanicValueType::resource)
            node.properties.push_back({
                .id = std::string{property.id},
                .value = default_value(property.type, property.id)});
    }
    if (kind == project::MechanicNodeKind::motor)
        std::ranges::find(node.properties, "direction",
                          &project::MechanicNodeProperty::id)->value =
            std::int64_t{1};
    return node;
}

project::ValidationReport validate_serialized(
    const project::ProjectManifest& manifest, const std::string_view contents) {
    const auto parsed = project::parse_mechanic_graph(manifest, contents);
    return {.errors = parsed.errors};
}

} // namespace

bool MechanicSession::create(const std::filesystem::path& project_root,
                             const project::MapDocument& map,
                             const project::MechanicGraph& graph) {
    if (dirty()) return false;
    project_root_ = project_root;
    auto manifest = project::load_manifest(project_root);
    if (!manifest.ok()) { errors_ = std::move(manifest.errors); return false; }
    const auto validation = project::validate_mechanic_graph(*manifest.manifest, graph);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    const auto path = project::mechanic_graph_document_path(
        *manifest.manifest, graph.document.id);
    std::error_code filesystem_error;
    if (std::filesystem::exists(project_root / path, filesystem_error) ||
        filesystem_error) {
        errors_.push_back({project::ErrorCode::invalid_path, "document",
                           "mechanic document already exists or cannot be inspected"});
        return false;
    }
    manifest_ = std::move(*manifest.manifest);
    map_ = map;
    graph_ = graph;
    document_path_ = path;
    recovery_graph_.reset();
    commands_.clear();
    autosave_.reset();
    errors_.clear();
    rebuild_preview();
    return save();
}

bool MechanicSession::open(const std::filesystem::path& project_root,
                           const project::MapDocument& map,
                           const core::ResourceId& graph_id) {
    if (dirty()) return false;
    project_root_ = project_root;
    auto manifest = project::load_manifest(project_root);
    if (!manifest.ok()) { errors_ = std::move(manifest.errors); return false; }
    auto loaded = project::load_mechanic_graph(
        project_root, *manifest.manifest,
        project::mechanic_graph_document_path(*manifest.manifest, graph_id));
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    manifest_ = std::move(*manifest.manifest);
    map_ = map;
    graph_ = std::move(*loaded.asset);
    document_path_ = project::mechanic_graph_document_path(*manifest_, graph_id);
    recovery_graph_.reset();
    commands_.clear();
    autosave_.reset();
    errors_.clear();
    const auto recovery = project::inspect_recovery(
        project_root_, document_path_, [this](const std::string_view contents) {
            return validate_serialized(*manifest_, contents);
        });
    if (recovery.candidate) {
        auto parsed = project::parse_mechanic_graph(
            *manifest_, recovery.candidate->contents);
        if (parsed.ok()) recovery_graph_ = std::move(*parsed.asset);
    } else if (!recovery.errors.empty()) {
        errors_ = recovery.errors;
    }
    rebuild_preview();
    return true;
}

bool MechanicSession::save() {
    if (!graph_ || !manifest_) return false;
    const auto result = project::publish_mechanic_graph(
        project_root_, *manifest_, *graph_);
    if (!result.ok()) { errors_ = result.errors; return false; }
    commands_.mark_clean();
    autosave_.reset();
    errors_.clear();
    return true;
}

AutosaveStatus MechanicSession::update_autosave(
    const AutosaveScheduler::Clock::time_point now) {
    if (!graph_ || !manifest_ || document_path_.empty()) return AutosaveStatus::not_due;
    if (commands_.dirty() && !autosave_.pending()) autosave_.mark_changed(now);
    if (!autosave_.due(now)) return AutosaveStatus::not_due;
    const auto report = project::save_autosave_atomic(
        project_root_, document_path_, project::serialize_mechanic_graph(*graph_),
        [this](const std::string_view contents) {
            return validate_serialized(*manifest_, contents);
        });
    if (!report.ok()) { errors_ = report.errors; return AutosaveStatus::failed; }
    autosave_.mark_saved();
    errors_.clear();
    return AutosaveStatus::saved;
}

bool MechanicSession::accept_recovery(const AutosaveScheduler::Clock::time_point now) {
    if (!recovery_graph_) return false;
    graph_ = std::move(recovery_graph_);
    recovery_graph_.reset();
    commands_.clear();
    commands_.mark_dirty();
    autosave_.mark_changed(now);
    errors_.clear();
    rebuild_preview();
    return true;
}

void MechanicSession::decline_recovery() noexcept {
    recovery_graph_.reset();
    errors_.clear();
}

bool MechanicSession::commit(project::MechanicGraph next) {
    if (!graph_ || !manifest_ ||
        !project::validate_mechanic_graph(*manifest_, next).ok()) return false;
    auto before = *graph_;
    const auto committed = commands_.execute(std::make_unique<MechanicSnapshotCommand>(
        *graph_, std::move(before), std::move(next)));
    if (committed) rebuild_preview();
    return committed;
}

bool MechanicSession::add_node(const project::MechanicNodeKind kind, std::string id) {
    if (!graph_ || !core::ResourceId::is_valid(id) ||
        std::ranges::any_of(graph_->nodes, [&](const auto& node) { return node.id == id; }))
        return false;
    auto next = *graph_;
    next.nodes.push_back(make_node(kind, std::move(id)));
    return commit(std::move(next));
}

bool MechanicSession::remove_node(const core::ResourceId& node_id) {
    if (!graph_) return false;
    auto next = *graph_;
    const auto node = std::ranges::find(next.nodes, node_id.value,
                                        &project::MechanicNodeDefinition::id);
    if (node == next.nodes.end()) return false;
    next.nodes.erase(node);
    std::erase_if(next.connections, [&](const auto& connection) {
        return connection.from_node == node_id.value ||
               connection.to_node == node_id.value;
    });
    std::erase_if(next.parameters, [&](const auto& parameter) {
        return parameter.target_node == node_id.value;
    });
    return commit(std::move(next));
}

bool MechanicSession::set_node_property(const core::ResourceId& node_id,
                                        std::string property_id,
                                        project::MechanicValue value) {
    if (!graph_) return false;
    auto next = *graph_;
    const auto node = std::ranges::find(next.nodes, node_id.value,
                                        &project::MechanicNodeDefinition::id);
    if (node == next.nodes.end()) return false;
    const auto property = std::ranges::find(node->properties, property_id,
                                            &project::MechanicNodeProperty::id);
    if (property == node->properties.end()) {
        const auto kind = project::mechanic_node_kind(node->type);
        if (!kind) return false;
        const auto& schema = project::mechanic_node_schema(*kind);
        const auto optional = std::ranges::find(
            schema.properties, property_id,
            &project::MechanicNodePropertySchema::id);
        if (optional == schema.properties.end() || optional->required ||
            !project::mechanic_value_matches(optional->type, value)) return false;
        node->properties.push_back({std::move(property_id), std::move(value)});
    } else {
        property->value = std::move(value);
    }
    return commit(std::move(next));
}

bool MechanicSession::connect(project::MechanicConnection connection) {
    if (!graph_) return false;
    auto next = *graph_;
    next.connections.push_back(std::move(connection));
    return commit(std::move(next));
}

bool MechanicSession::disconnect(const std::size_t connection_index) {
    if (!graph_ || connection_index >= graph_->connections.size()) return false;
    auto next = *graph_;
    next.connections.erase(next.connections.begin() +
                           static_cast<std::ptrdiff_t>(connection_index));
    return commit(std::move(next));
}

bool MechanicSession::undo() {
    if (!commands_.undo()) return false;
    rebuild_preview();
    return true;
}

bool MechanicSession::redo() {
    if (!commands_.redo()) return false;
    rebuild_preview();
    return true;
}

bool MechanicSession::reset_preview() {
    if (!preview_errors_.empty()) return false;
    return simulation_.reset();
}

void MechanicSession::rebuild_preview() {
    simulation_ = physics::MechanicSimulation{};
    preview_errors_.clear();
    if (!graph_ || !map_) return;
    auto compiled = physics::compile_mechanic_graph(*graph_, *map_);
    if (!compiled.ok()) {
        preview_errors_ = std::move(compiled.errors);
        return;
    }
    if (!simulation_.load(std::move(*compiled.plan))) {
        preview_errors_.push_back({project::ErrorCode::invalid_asset,
                                   "simulation", "Box2D preview could not be built"});
    }
}

} // namespace fabric::editor
