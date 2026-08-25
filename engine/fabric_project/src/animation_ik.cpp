#include "fabric/project/animation_ik.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabric::project {
namespace {

float length(const core::Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

core::Vec2 subtract(const core::Vec2 left, const core::Vec2 right) {
    return {left.x - right.x, left.y - right.y};
}

core::Vec2 add(const core::Vec2 left, const core::Vec2 right) {
    return {left.x + right.x, left.y + right.y};
}

core::Vec2 scale(const core::Vec2 value, const float factor) {
    return {value.x * factor, value.y * factor};
}

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

bool finite(const core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace

FabrikResult solve_fabrik(const FabrikRequest& request) {
    FabrikResult result;
    result.joints = request.joints;
    if (request.joints.size() < 2)
        error(result.errors, ErrorCode::invalid_asset, "joints",
              "FABRIK requires at least two joints");
    if (request.max_iterations == 0)
        error(result.errors, ErrorCode::invalid_asset, "maxIterations",
              "must be greater than zero");
    if (!std::isfinite(request.tolerance) || request.tolerance < 0.0F)
        error(result.errors, ErrorCode::invalid_asset, "tolerance",
              "must be finite and non-negative");
    if (!finite(request.target))
        error(result.errors, ErrorCode::invalid_asset, "target", "must be finite");
    for (const auto joint : request.joints)
        if (!finite(joint))
            error(result.errors, ErrorCode::invalid_asset, "joints", "must be finite");
    if (!result.errors.empty())
        return result;

    const core::Vec2 root = request.joints.front();
    std::vector<float> lengths;
    lengths.reserve(request.joints.size() - 1);
    float total_length = 0.0F;
    for (std::size_t i = 1; i < request.joints.size(); ++i) {
        const float segment = length(subtract(request.joints[i], request.joints[i - 1]));
        if (!std::isfinite(segment) || segment <= 0.0F) {
            error(result.errors, ErrorCode::invalid_asset, "joints",
                  "adjacent joints must have positive distance");
            return result;
        }
        lengths.push_back(segment);
        total_length += segment;
    }

    auto positions = request.joints;
    const float root_distance = length(subtract(request.target, root));
    if (root_distance >= total_length) {
        const auto direction = scale(subtract(request.target, root),
                                     root_distance == 0.0F ? 0.0F : 1.0F / root_distance);
        for (std::size_t i = 1; i < positions.size(); ++i)
            positions[i] = add(positions[i - 1], scale(direction, lengths[i - 1]));
        result.joints = std::move(positions);
        result.iterations = 1;
        result.converged = true;
        return result;
    }

    for (std::size_t iteration = 0; iteration < request.max_iterations; ++iteration) {
        positions.back() = request.target;
        for (std::size_t i = positions.size() - 1; i > 0; --i) {
            const auto direction = subtract(positions[i - 1], positions[i]);
            const float distance = length(direction);
            positions[i - 1] = add(positions[i],
                scale(direction, lengths[i - 1] / std::max(distance, 1.0e-12F)));
        }
        positions.front() = root;
        for (std::size_t i = 1; i < positions.size(); ++i) {
            const auto direction = subtract(positions[i], positions[i - 1]);
            const float distance = length(direction);
            positions[i] = add(positions[i - 1],
                scale(direction, lengths[i - 1] / std::max(distance, 1.0e-12F)));
        }
        result.iterations = iteration + 1;
        if (length(subtract(positions.back(), request.target)) <= request.tolerance) {
            result.converged = true;
            break;
        }
    }
    result.joints = std::move(positions);
    return result;
}

} // namespace fabric::project
