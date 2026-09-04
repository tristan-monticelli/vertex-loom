#include "fabric/render/textured_path_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace fabric::render {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr float direction_epsilon = 1.0e-6F;
constexpr std::size_t maximum_curve_segments = 4096U;
constexpr std::size_t round_cap_segments = 12U;

struct RibbonSection {
    core::Vec2 left;
    core::Vec2 right;
    float distance{};
};

core::Vec2 add(const core::Vec2 left, const core::Vec2 right) {
    return {left.x + right.x, left.y + right.y};
}

core::Vec2 subtract(const core::Vec2 left, const core::Vec2 right) {
    return {left.x - right.x, left.y - right.y};
}

core::Vec2 multiply(const core::Vec2 value, const float scalar) {
    return {value.x * scalar, value.y * scalar};
}

float dot(const core::Vec2 left, const core::Vec2 right) {
    return left.x * right.x + left.y * right.y;
}

float cross(const core::Vec2 left, const core::Vec2 right) {
    return left.x * right.y - left.y * right.x;
}

float length(const core::Vec2 value) {
    return std::sqrt(dot(value, value));
}

bool finite(const core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

core::Vec2 direction(const core::Vec2 from, const core::Vec2 to) {
    const auto delta = subtract(to, from);
    const float magnitude = length(delta);
    if (magnitude <= direction_epsilon) return {};
    return multiply(delta, 1.0F / magnitude);
}

core::Vec2 normal(const core::Vec2 tangent) {
    return {-tangent.y, tangent.x};
}

core::Vec2 cubic_point(const core::Vec2 start,
                       const project::TexturedPathCommand& command,
                       const float time) {
    const float inverse = 1.0F - time;
    const float start_weight = inverse * inverse * inverse;
    const float first_weight = 3.0F * inverse * inverse * time;
    const float second_weight = 3.0F * inverse * time * time;
    const float end_weight = time * time * time;
    return {
        start_weight * start.x + first_weight * command.control1.x +
            second_weight * command.control2.x + end_weight * command.point.x,
        start_weight * start.y + first_weight * command.control1.y +
            second_weight * command.control2.y + end_weight * command.point.y,
    };
}

std::vector<core::Vec2> flatten_path(const project::TexturedPath& path,
                                     const float tolerance) {
    std::vector<core::Vec2> points;
    points.reserve(path.commands.size());
    points.push_back(path.commands.front().point);
    auto current = path.commands.front().point;
    for (std::size_t index = 1U; index < path.commands.size(); ++index) {
        const auto& command = path.commands[index];
        if (command.kind == project::TexturedPathCommandKind::line) {
            points.push_back(command.point);
        } else if (command.kind == project::TexturedPathCommandKind::cubic) {
            const float control_length =
                length(subtract(command.control1, current)) +
                length(subtract(command.control2, command.control1)) +
                length(subtract(command.point, command.control2));
            const float ratio = control_length / tolerance;
            const auto requested = !std::isfinite(ratio) ||
                    ratio >= static_cast<float>(maximum_curve_segments)
                ? maximum_curve_segments
                : static_cast<std::size_t>(std::ceil(ratio));
            const auto segments = std::clamp(
                requested, std::size_t{4U}, maximum_curve_segments);
            for (std::size_t segment = 1U; segment <= segments; ++segment) {
                const float time = static_cast<float>(segment) /
                    static_cast<float>(segments);
                const auto point = cubic_point(current, command, time);
                if (point != points.back()) points.push_back(point);
            }
        }
        current = command.point;
    }
    if (path.closed) points.push_back(points.front());
    return points;
}

std::vector<float> cumulative_distances(
    const std::vector<core::Vec2>& points) {
    std::vector<float> distances(points.size(), 0.0F);
    for (std::size_t index = 1U; index < points.size(); ++index) {
        distances[index] = distances[index - 1U] +
            length(subtract(points[index], points[index - 1U]));
    }
    return distances;
}

float width_at(const project::TexturedPath& path,
               const float normalized_distance) {
    if (path.width_profile.empty()) return path.width;
    const auto next = std::ranges::lower_bound(
        path.width_profile, normalized_distance, {},
        &project::TexturedPathWidthKey::position);
    if (next == path.width_profile.begin()) return next->width;
    if (next == path.width_profile.end()) return path.width_profile.back().width;
    const auto& previous = *(next - 1);
    const float range = next->position - previous.position;
    const float amount = (normalized_distance - previous.position) / range;
    return previous.width + (next->width - previous.width) * amount;
}

void append_normal_section(std::vector<RibbonSection>& sections,
                           const core::Vec2 center,
                           const core::Vec2 section_normal,
                           const float half_width, const float distance) {
    const auto offset = multiply(section_normal, half_width);
    sections.push_back({.left = add(center, offset),
                        .right = subtract(center, offset),
                        .distance = distance});
}

bool append_join_sections(std::vector<RibbonSection>& sections,
                          const project::TexturedPath& path,
                          const core::Vec2 center,
                          const core::Vec2 previous_direction,
                          const core::Vec2 next_direction,
                          const float half_width, const float distance) {
    const auto previous_normal = normal(previous_direction);
    const auto next_normal = normal(next_direction);
    const float turn = std::atan2(cross(previous_normal, next_normal),
                                  dot(previous_normal, next_normal));
    if (std::abs(std::abs(turn) - pi) <= direction_epsilon) return false;
    if (std::abs(turn) <= direction_epsilon) {
        append_normal_section(sections, center, next_normal, half_width,
                              distance);
        return true;
    }
    if (path.join == project::TexturedPathJoin::bevel) {
        append_normal_section(sections, center, previous_normal, half_width,
                              distance);
        append_normal_section(sections, center, next_normal, half_width,
                              distance);
        return true;
    }
    if (path.join == project::TexturedPathJoin::round) {
        const auto steps = std::max(
            std::size_t{1U}, static_cast<std::size_t>(
                std::ceil(std::abs(turn) / (pi / 8.0F))));
        const float start = std::atan2(previous_normal.y, previous_normal.x);
        for (std::size_t step = 0U; step <= steps; ++step) {
            const float amount = static_cast<float>(step) /
                static_cast<float>(steps);
            const float angle = start + turn * amount;
            append_normal_section(sections, center,
                                  {std::cos(angle), std::sin(angle)},
                                  half_width, distance);
        }
        return true;
    }

    const auto summed = add(previous_normal, next_normal);
    const float magnitude = length(summed);
    if (magnitude <= direction_epsilon) return false;
    const auto miter = multiply(summed, 1.0F / magnitude);
    const float denominator = dot(miter, next_normal);
    if (denominator <= direction_epsilon ||
        1.0F / denominator > path.miter_limit) {
        append_normal_section(sections, center, previous_normal, half_width,
                              distance);
        append_normal_section(sections, center, next_normal, half_width,
                              distance);
        return true;
    }
    const auto offset = multiply(miter, half_width / denominator);
    sections.push_back({.left = add(center, offset),
                        .right = subtract(center, offset),
                        .distance = distance});
    return true;
}

float texture_u(const project::TexturedPath& path, const float distance,
                const float total_distance) {
    // Repetition is expressed per complete path: 1 maps the source once from
    // the deterministic start to the end, independently of world-unit length.
    const float base = distance / total_distance;
    return path.texture_metrics.origin.x + path.uv_offset.x +
        base * path.uv_scale.x * path.texture_metrics.size.x;
}

float texture_v(const project::TexturedPath& path, const float across) {
    return path.texture_metrics.origin.y + path.uv_offset.y +
        across * path.uv_scale.y * path.texture_metrics.size.y;
}

void append_round_cap(VectorDrawPacket& packet,
                      const project::TexturedPath& path,
                      const core::Vec2 center, const core::Vec2 tangent,
                      const core::Vec2 section_normal, const float half_width,
                      const float distance, const float total_distance,
                      const bool start) {
    const auto center_index = static_cast<std::uint32_t>(
        packet.fill_vertices.size());
    packet.fill_vertices.push_back(center);
    packet.fill_uv.push_back(
        {texture_u(path, distance, total_distance),
         texture_v(path, 0.5F)});
    for (std::size_t step = 0U; step <= round_cap_segments; ++step) {
        const float angle = pi * static_cast<float>(step) /
            static_cast<float>(round_cap_segments);
        const auto offset = start
            ? add(multiply(section_normal, std::cos(angle)),
                  multiply(tangent, -std::sin(angle)))
            : add(multiply(section_normal, -std::cos(angle)),
                  multiply(tangent, std::sin(angle)));
        packet.fill_vertices.push_back(
            add(center, multiply(offset, half_width)));
        const float transverse = dot(offset, section_normal);
        packet.fill_uv.push_back(
            {texture_u(path, distance, total_distance),
             texture_v(path, (1.0F - transverse) * 0.5F)});
        if (step > 0U) {
            const auto current = static_cast<std::uint32_t>(
                packet.fill_vertices.size() - 1U);
            packet.fill_indices.insert(packet.fill_indices.end(),
                                       {center_index, current - 1U, current});
        }
    }
}

} // namespace

VectorGeometryResult build_textured_path_draw_packets(
    const project::TexturedPath& path, const float curve_tolerance) {
    VectorGeometryResult result;
    const auto validation = project::validate_textured_path({}, path);
    for (const auto& error : validation.errors) {
        result.errors.push_back(error.field + ": " + error.message);
    }
    if (!std::isfinite(curve_tolerance) || curve_tolerance <= 0.0F) {
        result.errors.push_back(
            "curve tolerance must be finite and positive");
    }
    if (!result.errors.empty()) return result;

    const auto points = flatten_path(path, curve_tolerance);
    const auto distances = cumulative_distances(points);
    if (points.size() < 2U || distances.back() <= direction_epsilon ||
        !std::isfinite(distances.back())) {
        result.errors.push_back("textured path must have positive finite length");
        return result;
    }
    const float total_distance = distances.back();
    const std::size_t unique_count = path.closed
        ? points.size() - 1U : points.size();
    std::vector<RibbonSection> sections;
    sections.reserve(unique_count * 2U + 1U);
    std::size_t first_section_count = 1U;

    for (std::size_t index = 0U; index < unique_count; ++index) {
        const float normalized = distances[index] / total_distance;
        const float half_width = width_at(path, normalized) * 0.5F;
        if (!path.closed && (index == 0U || index + 1U == unique_count)) {
            const auto tangent = index == 0U
                ? direction(points[0], points[1])
                : direction(points[index - 1U], points[index]);
            if (length(tangent) <= direction_epsilon) {
                result.errors.push_back(
                    "textured path contains an unresolved endpoint direction");
                return result;
            }
            auto center = points[index];
            if (path.cap == project::TexturedPathCap::square) {
                center = add(center, multiply(
                    tangent, index == 0U ? -half_width : half_width));
            }
            append_normal_section(sections, center, normal(tangent),
                                  half_width, distances[index]);
            continue;
        }

        const std::size_t previous = index == 0U
            ? unique_count - 1U : index - 1U;
        const std::size_t next = (index + 1U) % unique_count;
        const auto previous_direction = direction(points[previous], points[index]);
        const auto next_direction = direction(points[index], points[next]);
        const auto before = sections.size();
        if (length(previous_direction) <= direction_epsilon ||
            length(next_direction) <= direction_epsilon ||
            !append_join_sections(sections, path, points[index],
                                  previous_direction, next_direction,
                                  half_width, distances[index])) {
            result.errors.push_back(
                "textured path contains a reversing or unresolved join");
            return result;
        }
        if (path.closed && index == 0U) {
            first_section_count = sections.size() - before;
        }
    }
    if (path.closed) {
        const auto first_sections = std::vector<RibbonSection>{
            sections.begin(), sections.begin() +
                static_cast<std::ptrdiff_t>(first_section_count)};
        for (auto section : first_sections) {
            section.distance = total_distance;
            sections.push_back(section);
        }
    }
    constexpr auto cap_vertex_budget = 2U * (round_cap_segments + 2U);
    const auto maximum_sections =
        (std::numeric_limits<std::uint32_t>::max() - cap_vertex_budget) / 2U;
    if (sections.size() > maximum_sections) {
        result.errors.push_back("textured path produces too many vertices");
        return result;
    }

    VectorDrawPacket packet{
        .node_id = path.document.id.value,
        .fill_color = path.color,
        .image_fill = project::VectorImageFill{
            .texture = path.texture,
            .fit = project::VectorImageFit::stretch,
            .opacity = path.opacity},
        .repeat_texture_x =
            path.uv_mode != project::TexturedPathUvMode::stretch,
        .mirror_texture_x =
            path.uv_mode == project::TexturedPathUvMode::mirror,
        .outline = points,
        .closed_outline = path.closed,
    };
    if (!(path.shader == project::ShaderSurfaceSettings{})) packet.shader = path.shader;
    packet.fill_vertices.reserve(sections.size() * 2U);
    packet.fill_uv.reserve(sections.size() * 2U);
    for (const auto& section : sections) {
        packet.fill_vertices.push_back(section.left);
        packet.fill_vertices.push_back(section.right);
        const float u = texture_u(path, section.distance, total_distance);
        packet.fill_uv.push_back({u, texture_v(path, 0.0F)});
        packet.fill_uv.push_back(
            {u, texture_v(path, 1.0F)});
    }
    for (std::size_t index = 0U; index + 1U < sections.size(); ++index) {
        const auto base = static_cast<std::uint32_t>(index * 2U);
        packet.fill_indices.insert(packet.fill_indices.end(),
                                   {base, base + 1U, base + 2U,
                                    base + 1U, base + 3U, base + 2U});
    }

    if (!path.closed && path.cap == project::TexturedPathCap::round) {
        const auto start_tangent = direction(points[0], points[1]);
        const auto end_tangent = direction(
            points[points.size() - 2U], points.back());
        append_round_cap(packet, path, points.front(), start_tangent,
                         normal(start_tangent), width_at(path, 0.0F) * 0.5F,
                         0.0F, total_distance, true);
        append_round_cap(packet, path, points.back(), end_tangent,
                         normal(end_tangent), width_at(path, 1.0F) * 0.5F,
                         total_distance, total_distance, false);
    }
    if (!std::ranges::all_of(packet.fill_vertices, finite) ||
        !std::ranges::all_of(packet.fill_uv, finite)) {
        result.errors.push_back(
            "textured path produces non-finite geometry or UVs");
        return result;
    }
    result.packets.push_back(std::move(packet));
    return result;
}

} // namespace fabric::render
