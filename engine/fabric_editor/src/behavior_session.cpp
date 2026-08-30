#include "fabric/editor/behavior_session.hpp"

#include "fabric/project/document_storage.hpp"

#include <algorithm>
#include <memory>
#include <ranges>
#include <utility>

namespace fabric::editor {
namespace {
class SnapshotCommand final : public Command {
public:
    SnapshotCommand(project::BehaviorGraph& target, project::BehaviorGraph before,
                    project::BehaviorGraph after)
        : target_(target), before_(std::move(before)), after_(std::move(after)) {}
    bool execute() override { target_ = after_; return true; }
    bool undo() override { target_ = before_; return true; }
private:
    project::BehaviorGraph& target_;
    project::BehaviorGraph before_;
    project::BehaviorGraph after_;
};

project::BehaviorNodeDefinition make_node(std::string type, std::string id) {
    using Direction = project::BehaviorPortDirection;
    using Type = project::BehaviorValueType;
    project::BehaviorNodeDefinition node{.id = std::move(id), .type = std::move(type)};
    if (node.type.ends_with("_source")) {
        node.ports.push_back({"out", Direction::output, Type::signal});
        node.properties.push_back({"semantic_id", std::string{"action"}});
    } else if (node.type == "branch") {
        node.ports = {{"in", Direction::input, Type::signal},
                      {"true", Direction::output, Type::signal},
                      {"false", Direction::output, Type::signal}};
    } else {
        node.ports = {{"in", Direction::input, Type::signal},
                      {"out", Direction::output, Type::signal}};
    }
    if (node.type == "condition") node.properties.push_back({"value", true});
    if (node.type == "delay" || node.type == "cooldown")
        node.properties.push_back({"duration", 0.25F});
    if (node.type == "state") {
        node.properties.push_back({"group", std::string{"default"}});
        node.properties.push_back({"state_id", std::string{"idle"}});
    }
    if (node.type == "transition") {
        node.properties.push_back({"group", std::string{"default"}});
        node.properties.push_back({"from_state", std::string{"idle"}});
        node.properties.push_back({"to_state", std::string{"active"}});
    }
    if (node.type == "set_property") {
        node.properties.push_back({"target", std::string{"enabled"}});
        node.properties.push_back({"value", true});
    }
    if (node.type == "emit_event") node.properties.push_back({"event_id", std::string{"event"}});
    if (node.type == "move") node.properties.push_back({"vector", core::Vec2{1.0F, 0.0F}});
    if (node.type == "play_animation")
        node.properties.push_back({"animation", project::ResourceReference{{.value = "animation"}, "animation"}});
    if (node.type == "activate_mechanic")
        node.properties.push_back({"mechanic", project::ResourceReference{{.value = "mechanic"}, "mechanic"}});
    if (node.type == "transform_entity")
        node.properties.push_back({"transformation", project::ResourceReference{{.value = "transformation"}, "transformation"}});
    return node;
}

project::ValidationReport validate_serialized(
    const project::ProjectManifest& manifest, std::string_view contents) {
    const auto parsed = project::parse_behavior_graph(manifest, contents);
    return {.errors = parsed.errors};
}
} // namespace

bool BehaviorSession::create(const std::filesystem::path& root,
                             const project::BehaviorGraph& graph) {
    auto manifest = project::load_manifest(root);
    if (!manifest.ok()) { errors_ = std::move(manifest.errors); return false; }
    const auto validation = project::validate_behavior_graph(*manifest.manifest, graph);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    const auto path = project::behavior_graph_document_path(*manifest.manifest,
                                                            graph.document.id);
    std::error_code filesystem_error;
    if (std::filesystem::exists(root / path, filesystem_error) || filesystem_error) {
        errors_ = {{project::ErrorCode::asset_already_exists, "document",
                    "behavior document already exists or cannot be inspected"}};
        return false;
    }
    if (dirty() && !save()) return false;
    root_ = root; path_ = path; manifest_ = std::move(*manifest.manifest);
    graph_ = graph; recovery_.reset(); commands_.clear(); autosave_.reset();
    errors_.clear(); rebuild_preview();
    return save();
}

bool BehaviorSession::open(const std::filesystem::path& root,
                           const core::ResourceId& id) {
    auto manifest = project::load_manifest(root);
    if (!manifest.ok()) { errors_ = std::move(manifest.errors); return false; }
    const auto path = project::behavior_graph_document_path(*manifest.manifest, id);
    auto loaded = project::load_behavior_graph(root, *manifest.manifest, path);
    if (!loaded.ok()) { errors_ = std::move(loaded.errors); return false; }
    if (dirty() && !save()) return false;
    root_ = root; path_ = path; manifest_ = std::move(*manifest.manifest);
    graph_ = std::move(*loaded.asset); recovery_.reset(); commands_.clear();
    autosave_.reset(); errors_.clear();
    const auto candidate = project::inspect_recovery(root_, path_, [this](std::string_view value) {
        return validate_serialized(*manifest_, value);
    });
    if (candidate.candidate) {
        auto parsed = project::parse_behavior_graph(*manifest_, candidate.candidate->contents);
        if (parsed.ok()) recovery_ = std::move(*parsed.asset);
    } else if (!candidate.errors.empty()) errors_ = candidate.errors;
    rebuild_preview();
    return true;
}

bool BehaviorSession::save() {
    if (!graph_ || !manifest_) return false;
    const auto saved = project::publish_behavior_graph(root_, *manifest_, *graph_);
    if (!saved.ok()) { errors_ = saved.errors; return false; }
    commands_.mark_clean(); autosave_.reset(); errors_.clear(); return true;
}

AutosaveStatus BehaviorSession::update_autosave(
    const AutosaveScheduler::Clock::time_point now) {
    if (!graph_ || !manifest_) return AutosaveStatus::not_due;
    if (dirty() && !autosave_.pending()) autosave_.mark_changed(now);
    if (!autosave_.due(now)) return AutosaveStatus::not_due;
    const auto report = project::save_autosave_atomic(
        root_, path_, project::serialize_behavior_graph(*graph_),
        [this](std::string_view value) { return validate_serialized(*manifest_, value); });
    if (!report.ok()) { errors_ = report.errors; return AutosaveStatus::failed; }
    autosave_.mark_saved(); errors_.clear(); return AutosaveStatus::saved;
}

bool BehaviorSession::accept_recovery(const AutosaveScheduler::Clock::time_point now) {
    if (!recovery_) return false;
    graph_ = std::move(recovery_); recovery_.reset(); commands_.clear();
    commands_.mark_dirty(); autosave_.mark_changed(now); errors_.clear();
    rebuild_preview(); return true;
}

void BehaviorSession::decline_recovery() noexcept { recovery_.reset(); errors_.clear(); }

bool BehaviorSession::commit(project::BehaviorGraph next) {
    if (!graph_ || !manifest_) return false;
    const auto validation = project::validate_behavior_graph(*manifest_, next);
    if (!validation.ok()) { errors_ = validation.errors; return false; }
    auto before = *graph_;
    if (!commands_.execute(std::make_unique<SnapshotCommand>(
            *graph_, std::move(before), std::move(next)))) return false;
    errors_.clear(); rebuild_preview(); return true;
}

bool BehaviorSession::add_node(std::string type, std::string id) {
    if (!graph_ || !project::is_behavior_node_type(type) ||
        !core::ResourceId::is_valid(id) ||
        std::ranges::any_of(graph_->nodes, [&](const auto& node) { return node.id == id; })) return false;
    auto next = *graph_; next.nodes.push_back(make_node(std::move(type), std::move(id)));
    return commit(std::move(next));
}

bool BehaviorSession::duplicate_node(const core::ResourceId& id, std::string new_id) {
    if (!graph_ || !core::ResourceId::is_valid(new_id) ||
        std::ranges::any_of(graph_->nodes, [&](const auto& node) { return node.id == new_id; })) return false;
    const auto found = std::ranges::find(graph_->nodes, id.value,
                                         &project::BehaviorNodeDefinition::id);
    if (found == graph_->nodes.end()) return false;
    auto next = *graph_; auto copy = *found; copy.id = std::move(new_id);
    next.nodes.push_back(std::move(copy)); return commit(std::move(next));
}

bool BehaviorSession::remove_node(const core::ResourceId& id) {
    if (!graph_) return false;
    auto next = *graph_;
    if (std::erase_if(next.nodes, [&](const auto& node) { return node.id == id.value; }) != 1) return false;
    std::erase_if(next.connections, [&](const auto& connection) {
        return connection.from_node == id.value || connection.to_node == id.value;
    });
    return commit(std::move(next));
}

bool BehaviorSession::set_node_property(const core::ResourceId& id,
                                        std::string property_id,
                                        project::BehaviorValue value) {
    if (!graph_ || !core::ResourceId::is_valid(property_id)) return false;
    auto next = *graph_;
    const auto node = std::ranges::find(next.nodes, id.value,
                                        &project::BehaviorNodeDefinition::id);
    if (node == next.nodes.end()) return false;
    const auto found = std::ranges::find(node->properties, property_id,
                                         &project::BehaviorNodeProperty::id);
    if (found == node->properties.end()) node->properties.push_back({std::move(property_id), std::move(value)});
    else found->value = std::move(value);
    return commit(std::move(next));
}

bool BehaviorSession::connect(project::BehaviorConnection connection) {
    if (!graph_) return false;
    auto next = *graph_; next.connections.push_back(std::move(connection));
    return commit(std::move(next));
}

bool BehaviorSession::disconnect(const core::ResourceId& id) {
    if (!graph_) return false;
    auto next = *graph_;
    if (std::erase_if(next.connections, [&](const auto& item) { return item.id == id.value; }) != 1) return false;
    return commit(std::move(next));
}

bool BehaviorSession::undo() { if (!commands_.undo()) return false; rebuild_preview(); return true; }
bool BehaviorSession::redo() { if (!commands_.redo()) return false; rebuild_preview(); return true; }

std::vector<runtime::BehaviorAction> BehaviorSession::preview(
    const runtime::BehaviorSignal& signal, const float fixed_step_seconds) {
    return evaluator_ ? evaluator_->evaluate(signal, fixed_step_seconds)
                      : std::vector<runtime::BehaviorAction>{};
}

void BehaviorSession::reset_preview() { rebuild_preview(); }

const std::vector<runtime::BehaviorTraceEntry>& BehaviorSession::trace() const noexcept {
    static const std::vector<runtime::BehaviorTraceEntry> empty;
    return evaluator_ ? evaluator_->trace() : empty;
}

void BehaviorSession::rebuild_preview() {
    evaluator_.reset();
    if (graph_) evaluator_ = std::make_unique<runtime::BehaviorEvaluator>(*graph_);
}
} // namespace fabric::editor
