#include "fabric/runtime/behavior_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <utility>

namespace fabric::runtime {
namespace {
std::string_view source_node_type(BehaviorSignalSource source) {
    switch (source) {
    case BehaviorSignalSource::action: return "action_source";
    case BehaviorSignalSource::ai_decision: return "ai_source";
    case BehaviorSignalSource::map_event: return "event_source";
    case BehaviorSignalSource::trigger: return "trigger_source";
    case BehaviorSignalSource::timer: return "timer_source";
    case BehaviorSignalSource::property: return "property_source";
    }
    return "action_source";
}

const project::BehaviorValue* property(
    const project::BehaviorNodeDefinition& node, std::string_view id) {
    const auto found = std::ranges::find_if(node.properties, [&](const auto& item) {
        return item.id == id;
    });
    return found == node.properties.end() ? nullptr : &found->value;
}

template <typename T>
std::optional<T> property_as(const project::BehaviorNodeDefinition& node,
                             std::string_view id) {
    const auto* value = property(node, id);
    if (!value) return std::nullopt;
    if (const auto* typed = std::get_if<T>(value)) return *typed;
    return std::nullopt;
}

std::optional<BehaviorActionKind> action_kind(std::string_view type) {
    if (type == "set_property") return BehaviorActionKind::set_property;
    if (type == "emit_event") return BehaviorActionKind::emit_event;
    if (type == "play_animation") return BehaviorActionKind::play_animation;
    if (type == "move") return BehaviorActionKind::move;
    if (type == "activate_mechanic") return BehaviorActionKind::activate_mechanic;
    if (type == "transform_entity") return BehaviorActionKind::transform_entity;
    return std::nullopt;
}
} // namespace

BehaviorEvaluator::BehaviorEvaluator(project::BehaviorGraph graph,
                                     const std::size_t trace_capacity)
    : graph_(std::move(graph)), trace_capacity_(std::max<std::size_t>(1, trace_capacity)) {
    reset();
}

std::vector<BehaviorAction> BehaviorEvaluator::evaluate(
    const BehaviorSignal& signal, const float fixed_step_seconds) {
    const float step = std::isfinite(fixed_step_seconds) && fixed_step_seconds >= 0.0F
        ? fixed_step_seconds : 0.0F;
    for (auto& [unused, remaining] : cooldowns_) remaining = std::max(0.0F, remaining - step);

    std::deque<std::string> queue;
    for (auto iterator = pending_.begin(); iterator != pending_.end();) {
        iterator->remaining -= step;
        if (iterator->remaining <= 0.0F) {
            queue.push_back(iterator->node_id);
            iterator = pending_.erase(iterator);
        } else ++iterator;
    }

    const auto append_trace = [&](std::string node_id, std::string message) {
        trace_.push_back({std::move(node_id), std::move(message)});
        if (trace_.size() > trace_capacity_)
            trace_.erase(trace_.begin(), trace_.begin() +
                         static_cast<std::ptrdiff_t>(trace_.size() - trace_capacity_));
    };
    const auto enqueue_from = [&](const std::string& node, std::string_view port) {
        for (const auto& connection : graph_.connections)
            if (connection.from_node == node && connection.from_port == port)
                queue.push_back(connection.to_node);
    };

    for (const auto& node : graph_.nodes) {
        if (node.type != source_node_type(signal.source)) continue;
        const auto semantic = property_as<std::string>(node, "semantic_id");
        if (semantic && *semantic == signal.semantic_id) {
            append_trace(node.id, "signal:" + signal.semantic_id);
            enqueue_from(node.id, "out");
        }
    }

    std::vector<BehaviorAction> actions;
    bool condition = true;
    std::size_t guard{};
    while (!queue.empty() && guard++ < graph_.nodes.size() * 8U + 32U) {
        const auto id = std::move(queue.front()); queue.pop_front();
        const auto found = std::ranges::find_if(graph_.nodes, [&](const auto& node) { return node.id == id; });
        if (found == graph_.nodes.end()) continue;
        const auto& node = *found;
        append_trace(node.id, "visit:" + node.type);

        if (node.type == "condition") {
            if (const auto parameter = property_as<std::string>(node, "parameter_id")) {
                const auto value = parameters_.find(*parameter);
                condition = value != parameters_.end() &&
                    std::holds_alternative<bool>(value->second) &&
                    std::get<bool>(value->second);
            } else condition = property_as<bool>(node, "value").value_or(false);
            enqueue_from(node.id, "out");
            continue;
        }
        if (node.type == "branch") {
            enqueue_from(node.id, condition ? "true" : "false");
            continue;
        }
        if (node.type == "delay") {
            const float duration = std::max(0.0F, property_as<float>(node, "duration").value_or(0.0F));
            for (const auto& connection : graph_.connections)
                if (connection.from_node == node.id && connection.from_port == "out")
                    pending_.push_back({connection.to_node, duration});
            continue;
        }
        if (node.type == "cooldown") {
            const float duration = std::max(0.0F, property_as<float>(node, "duration").value_or(0.0F));
            if (cooldowns_[node.id] > 0.0F) { append_trace(node.id, "blocked:cooldown"); continue; }
            cooldowns_[node.id] = duration;
            enqueue_from(node.id, "out");
            continue;
        }
        if (node.type == "state") {
            const auto group = property_as<std::string>(node, "group").value_or("default");
            const auto state = property_as<std::string>(node, "state_id").value_or("default");
            if (!states_.contains(group)) states_[group] = state;
            if (states_[group] == state) enqueue_from(node.id, "out");
            continue;
        }
        if (node.type == "transition") {
            const auto group = property_as<std::string>(node, "group").value_or("default");
            const auto from = property_as<std::string>(node, "from_state").value_or("");
            const auto to = property_as<std::string>(node, "to_state").value_or("");
            if ((from.empty() || states_[group] == from) && !to.empty()) {
                states_[group] = to;
                enqueue_from(node.id, "out");
            }
            continue;
        }
        if (const auto kind = action_kind(node.type)) {
            actions.push_back({*kind, node.id, node.properties});
            if (*kind == BehaviorActionKind::set_property) {
                const auto target = property_as<std::string>(node, "target");
                const auto* value = property(node, "value");
                if (target && value) parameters_[*target] = *value;
            }
        }
        enqueue_from(node.id, "out");
    }
    return actions;
}

const std::vector<BehaviorTraceEntry>& BehaviorEvaluator::trace() const noexcept {
    return trace_;
}

const std::map<std::string, project::BehaviorValue>&
BehaviorEvaluator::parameters() const noexcept { return parameters_; }

void BehaviorEvaluator::reset() {
    trace_.clear(); cooldowns_.clear(); states_.clear(); pending_.clear();
    parameters_.clear();
    for (const auto& parameter : graph_.parameters)
        parameters_.emplace(parameter.id, parameter.default_value);
}
} // namespace fabric::runtime
