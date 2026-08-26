#pragma once

#include "fabric/project/behavior_graph.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fabric::runtime {

enum class BehaviorSignalSource { action, ai_decision, map_event, trigger, timer, property };
enum class BehaviorActionKind {
    set_property, emit_event, play_animation, move, activate_mechanic,
    transform_entity,
};

struct BehaviorSignal {
    BehaviorSignalSource source{BehaviorSignalSource::action};
    std::string semantic_id;
    std::vector<project::BehaviorNodeProperty> payload;
    friend bool operator==(const BehaviorSignal&, const BehaviorSignal&) = default;
};

struct BehaviorAction {
    BehaviorActionKind kind{BehaviorActionKind::set_property};
    std::string node_id;
    std::vector<project::BehaviorNodeProperty> properties;
    friend bool operator==(const BehaviorAction&, const BehaviorAction&) = default;
};

struct BehaviorTraceEntry {
    std::string node_id;
    std::string message;
    friend bool operator==(const BehaviorTraceEntry&, const BehaviorTraceEntry&) = default;
};

class BehaviorEvaluator {
public:
    explicit BehaviorEvaluator(project::BehaviorGraph graph,
                               std::size_t trace_capacity = 128);

    [[nodiscard]] std::vector<BehaviorAction> evaluate(
        const BehaviorSignal&, float fixed_step_seconds);
    [[nodiscard]] const std::vector<BehaviorTraceEntry>& trace() const noexcept;
    [[nodiscard]] const std::map<std::string, project::BehaviorValue>& parameters() const noexcept;
    void reset();

private:
    struct Pending { std::string node_id; float remaining{}; };
    project::BehaviorGraph graph_;
    std::size_t trace_capacity_;
    std::vector<BehaviorTraceEntry> trace_;
    std::map<std::string, project::BehaviorValue> parameters_;
    std::map<std::string, float> cooldowns_;
    std::map<std::string, std::string> states_;
    std::vector<Pending> pending_;
};

} // namespace fabric::runtime
