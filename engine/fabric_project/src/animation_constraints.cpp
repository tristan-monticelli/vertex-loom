#include "fabric/project/animation_constraints.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <utility>

namespace fabric::project {
namespace {

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

bool finite(const core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool ordered(const std::optional<core::Vec2>& minimum,
             const std::optional<core::Vec2>& maximum) {
    return !minimum || !maximum ||
        (minimum->x <= maximum->x && minimum->y <= maximum->y);
}

} // namespace

ValidationReport validate_animation_constraints(
    const std::vector<AnimationConstraint>& constraints) {
    ValidationReport report;
    std::set<std::string> ids;
    std::set<int> orders;
    std::vector<std::pair<std::string, std::string>> edges;
    for (const auto& constraint : constraints) {
        if (!core::ResourceId::is_valid(constraint.id) ||
            !ids.insert(constraint.id).second)
            error(report.errors, ErrorCode::duplicate_resource, "constraints.id",
                  "constraint ids must be valid and unique");
        if (constraint.target_node.empty() || constraint.source_node.empty() ||
            constraint.target_node == constraint.source_node)
            error(report.errors, ErrorCode::invalid_asset, "constraints.nodes",
                  "constraints require distinct source and target nodes");
        if (!orders.insert(constraint.order).second)
            error(report.errors, ErrorCode::duplicate_resource, "constraints.order",
                  "constraint order values must be unique");
        const auto valid_vec = [&](const std::optional<core::Vec2>& value,
                                   const char* field) {
            if (value && !finite(*value))
                error(report.errors, ErrorCode::invalid_asset, field,
                      "constraint bounds must be finite");
        };
        valid_vec(constraint.min_position, "constraints.minPosition");
        valid_vec(constraint.max_position, "constraints.maxPosition");
        valid_vec(constraint.min_scale, "constraints.minScale");
        valid_vec(constraint.max_scale, "constraints.maxScale");
        if (!ordered(constraint.min_position, constraint.max_position) ||
            !ordered(constraint.min_scale, constraint.max_scale))
            error(report.errors, ErrorCode::invalid_asset, "constraints.bounds",
                  "minimum bounds must not exceed maximum bounds");
        if ((constraint.min_rotation_degrees &&
             !std::isfinite(*constraint.min_rotation_degrees)) ||
            (constraint.max_rotation_degrees &&
             !std::isfinite(*constraint.max_rotation_degrees)) ||
            (constraint.min_rotation_degrees && constraint.max_rotation_degrees &&
             *constraint.min_rotation_degrees > *constraint.max_rotation_degrees))
            error(report.errors, ErrorCode::invalid_asset, "constraints.rotationBounds",
                  "rotation bounds must be finite and ordered");
        edges.emplace_back(constraint.source_node, constraint.target_node);
    }

    std::set<std::string> visiting;
    std::set<std::string> visited;
    std::function<void(const std::string&)> visit = [&](const std::string& node) {
        if (visiting.contains(node)) {
            error(report.errors, ErrorCode::resource_cycle, "constraints",
                  "constraint dependencies must not contain cycles");
            return;
        }
        if (!visited.insert(node).second)
            return;
        visiting.insert(node);
        for (const auto& [source, target] : edges)
            if (source == node)
                visit(target);
        visiting.erase(node);
    };
    for (const auto& [source, target] : edges) {
        visit(source);
        visit(target);
    }
    return report;
}

std::vector<const AnimationConstraint*> order_animation_constraints(
    const std::vector<AnimationConstraint>& constraints) {
    if (!validate_animation_constraints(constraints).ok())
        return {};
    std::vector<const AnimationConstraint*> ordered;
    for (const auto& constraint : constraints)
        ordered.push_back(&constraint);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* left, const auto* right) {
                         return left->order < right->order;
                     });
    return ordered;
}

} // namespace fabric::project
