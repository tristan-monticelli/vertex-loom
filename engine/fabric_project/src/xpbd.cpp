#include "fabric/project/xpbd.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fabric::project {
namespace {

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

core::Vec2 subtract(const core::Vec2 a, const core::Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

core::Vec2 add(const core::Vec2 a, const core::Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

core::Vec2 scale(const core::Vec2 a, float factor) {
    return {a.x * factor, a.y * factor};
}

float dot(const core::Vec2 a, const core::Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

float length(const core::Vec2 a) {
    return std::sqrt(dot(a, a));
}

float cross(const core::Vec2 a, const core::Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

float triangle_area(const core::Vec2 a, const core::Vec2 b, const core::Vec2 c) {
    return 0.5F * cross(subtract(b, a), subtract(c, a));
}

bool finite(const core::Vec2 a) {
    return std::isfinite(a.x) && std::isfinite(a.y);
}

core::Vec2 quantize(const core::Vec2 value, float factor) {
    return {std::round(value.x * factor) / factor,
            std::round(value.y * factor) / factor};
}

} // namespace

ValidationReport validate_xpbd_system(const XpbdSystem& system, float dt,
                                      std::size_t iterations) {
    ValidationReport report;
    if (!std::isfinite(dt) || dt <= 0.0F)
        error(report.errors, ErrorCode::invalid_asset, "dt", "must be finite and positive");
    if (iterations == 0)
        error(report.errors, ErrorCode::invalid_asset, "iterations", "must be greater than zero");
    for (const auto& particle : system.particles) {
        if (!finite(particle.position) || !std::isfinite(particle.inverse_mass) ||
            particle.inverse_mass < 0.0F)
            error(report.errors, ErrorCode::invalid_asset, "particles", "must be finite with non-negative inverse mass");
    }
    for (const auto& constraint : system.distance_constraints) {
        if (constraint.first >= system.particles.size() ||
            constraint.second >= system.particles.size() ||
            constraint.first == constraint.second ||
            !std::isfinite(constraint.rest_length) || constraint.rest_length < 0.0F ||
            !std::isfinite(constraint.compliance) || constraint.compliance < 0.0F ||
            !std::isfinite(constraint.lambda))
            error(report.errors, ErrorCode::invalid_asset, "distanceConstraints",
                  "distance constraint is invalid");
    }
    for (const auto& constraint : system.pin_constraints) {
        if (constraint.particle >= system.particles.size() || !finite(constraint.target) ||
            !std::isfinite(constraint.compliance) || constraint.compliance < 0.0F ||
            !finite(constraint.lambda))
            error(report.errors, ErrorCode::invalid_asset, "pinConstraints",
                  "pin constraint is invalid");
    }
    for (const auto& constraint : system.bending_constraints) {
        if (constraint.first >= system.particles.size() ||
            constraint.middle >= system.particles.size() ||
            constraint.third >= system.particles.size() ||
            constraint.first == constraint.middle || constraint.middle == constraint.third ||
            !std::isfinite(constraint.rest_length) || constraint.rest_length < 0.0F ||
            !std::isfinite(constraint.compliance) || constraint.compliance < 0.0F ||
            !std::isfinite(constraint.lambda))
            error(report.errors, ErrorCode::invalid_asset, "bendingConstraints",
                  "bending constraint is invalid");
    }
    for (const auto& constraint : system.area_constraints) {
        if (constraint.first >= system.particles.size() ||
            constraint.second >= system.particles.size() ||
            constraint.third >= system.particles.size() ||
            constraint.first == constraint.second || constraint.second == constraint.third ||
            constraint.first == constraint.third || !std::isfinite(constraint.rest_area) ||
            !std::isfinite(constraint.compliance) || constraint.compliance < 0.0F ||
            !std::isfinite(constraint.lambda))
            error(report.errors, ErrorCode::invalid_asset, "areaConstraints",
                  "area constraint is invalid");
    }
    for (const auto& constraint : system.collision_constraints) {
        if (constraint.particle >= system.particles.size() || !finite(constraint.normal) ||
            length(constraint.normal) <= 1.0e-12F || !std::isfinite(constraint.offset) ||
            !std::isfinite(constraint.compliance) || constraint.compliance < 0.0F ||
            !std::isfinite(constraint.lambda) || constraint.lambda < 0.0F)
            error(report.errors, ErrorCode::invalid_asset, "collisionConstraints",
                  "collision constraint is invalid");
    }
    return report;
}

XpbdResult solve_xpbd_substep(XpbdSystem& system, float dt, std::size_t iterations,
                              float quantization_factor) {
    XpbdResult result;
    const auto validation = validate_xpbd_system(system, dt, iterations);
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }
    if (!std::isfinite(quantization_factor) || quantization_factor <= 0.0F) {
        error(result.errors, ErrorCode::invalid_asset, "quantization",
              "must be finite and positive");
        return result;
    }
    const float dt_squared = dt * dt;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        for (auto& constraint : system.distance_constraints) {
            auto& first = system.particles[constraint.first];
            auto& second = system.particles[constraint.second];
            const auto delta = subtract(second.position, first.position);
            const float distance = length(delta);
            const auto direction = distance > 1.0e-12F
                ? scale(delta, 1.0F / distance) : core::Vec2{};
            const float value = distance - constraint.rest_length;
            const float alpha = constraint.compliance / dt_squared;
            const float weight = first.inverse_mass + second.inverse_mass;
            const float denominator = weight + alpha;
            if (denominator <= 0.0F)
                continue;
            const float delta_lambda = (-value - alpha * constraint.lambda) / denominator;
            constraint.lambda += delta_lambda;
            first.position = add(first.position,
                                 scale(direction, -first.inverse_mass * delta_lambda));
            second.position = add(second.position,
                                  scale(direction, second.inverse_mass * delta_lambda));
        }
        for (auto& constraint : system.pin_constraints) {
            auto& particle = system.particles[constraint.particle];
            const auto value = subtract(particle.position, constraint.target);
            const float alpha = constraint.compliance / dt_squared;
            const float denominator = particle.inverse_mass + alpha;
            if (denominator <= 0.0F)
                continue;
            const auto delta_lambda = scale(value, -1.0F / denominator);
            constraint.lambda = add(constraint.lambda, delta_lambda);
            particle.position = add(particle.position,
                                    scale(delta_lambda, particle.inverse_mass));
        }
        for (auto& constraint : system.bending_constraints) {
            auto& first = system.particles[constraint.first];
            auto& third = system.particles[constraint.third];
            const auto delta = subtract(third.position, first.position);
            const float distance = length(delta);
            const auto direction = distance > 1.0e-12F
                ? scale(delta, 1.0F / distance) : core::Vec2{};
            const float value = distance - constraint.rest_length;
            const float alpha = constraint.compliance / dt_squared;
            const float weight = first.inverse_mass + third.inverse_mass;
            const float denominator = weight + alpha;
            if (denominator <= 0.0F)
                continue;
            const float delta_lambda = (-value - alpha * constraint.lambda) / denominator;
            constraint.lambda += delta_lambda;
            first.position = add(first.position,
                                 scale(direction, -first.inverse_mass * delta_lambda));
            third.position = add(third.position,
                                 scale(direction, third.inverse_mass * delta_lambda));
        }
        for (auto& constraint : system.area_constraints) {
            auto& first = system.particles[constraint.first];
            auto& second = system.particles[constraint.second];
            auto& third = system.particles[constraint.third];
            const auto p0 = first.position;
            const auto p1 = second.position;
            const auto p2 = third.position;
            const core::Vec2 gradient_first{0.5F * (p1.y - p2.y),
                                            0.5F * (p2.x - p1.x)};
            const core::Vec2 gradient_second{0.5F * (p2.y - p0.y),
                                             0.5F * (p0.x - p2.x)};
            const core::Vec2 gradient_third{0.5F * (p0.y - p1.y),
                                             0.5F * (p1.x - p0.x)};
            const float value = triangle_area(p0, p1, p2) - constraint.rest_area;
            const float alpha = constraint.compliance / dt_squared;
            const float weight = first.inverse_mass * dot(gradient_first, gradient_first) +
                second.inverse_mass * dot(gradient_second, gradient_second) +
                third.inverse_mass * dot(gradient_third, gradient_third);
            const float denominator = weight + alpha;
            if (denominator <= 0.0F)
                continue;
            const float delta_lambda = (-value - alpha * constraint.lambda) / denominator;
            constraint.lambda += delta_lambda;
            first.position = add(first.position,
                                 scale(gradient_first, first.inverse_mass * delta_lambda));
            second.position = add(second.position,
                                  scale(gradient_second, second.inverse_mass * delta_lambda));
            third.position = add(third.position,
                                 scale(gradient_third, third.inverse_mass * delta_lambda));
        }
        for (auto& constraint : system.collision_constraints) {
            auto& particle = system.particles[constraint.particle];
            const float normal_length = length(constraint.normal);
            const auto normal = scale(constraint.normal, 1.0F / normal_length);
            const float value = dot(particle.position, normal) - constraint.offset;
            if (value >= 0.0F && constraint.lambda == 0.0F)
                continue;
            const float alpha = constraint.compliance / dt_squared;
            const float denominator = particle.inverse_mass + alpha;
            if (denominator <= 0.0F)
                continue;
            const float delta_lambda = (-value - alpha * constraint.lambda) / denominator;
            const float next_lambda = std::max(0.0F, constraint.lambda + delta_lambda);
            const float applied = next_lambda - constraint.lambda;
            constraint.lambda = next_lambda;
            particle.position = add(particle.position,
                                    scale(normal, particle.inverse_mass * applied));
        }
        for (auto& particle : system.particles)
            particle.position = quantize(particle.position, quantization_factor);
        result.iterations = iteration + 1;
    }
    return result;
}

} // namespace fabric::project
