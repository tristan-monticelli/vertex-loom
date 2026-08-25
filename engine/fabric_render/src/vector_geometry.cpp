#include "fabric/render/vector_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fabric::render {
namespace {

float cross(const core::Vec2 a, const core::Vec2 b, const core::Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

float polygon_area(const std::vector<core::Vec2>& points) {
    float area = 0.0F;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& current = points[index];
        const auto& next = points[(index + 1U) % points.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return area * 0.5F;
}

bool point_in_triangle(const core::Vec2 point, const core::Vec2 a,
                       const core::Vec2 b, const core::Vec2 c) {
    const float first = cross(a, b, point);
    const float second = cross(b, c, point);
    const float third = cross(c, a, point);
    constexpr float epsilon = 1.0e-6F;
    return first >= -epsilon && second >= -epsilon && third >= -epsilon;
}

std::vector<std::uint32_t> triangulate(const std::vector<core::Vec2>& points) {
    std::vector<std::uint32_t> result;
    if (points.size() < 3U) return result;
    std::vector<std::uint32_t> remaining(points.size());
    std::iota(remaining.begin(), remaining.end(), 0U);
    if (polygon_area(points) < 0.0F) {
        std::reverse(remaining.begin(), remaining.end());
    }
    while (remaining.size() > 3U) {
        bool clipped = false;
        for (std::size_t index = 0; index < remaining.size(); ++index) {
            const auto previous = remaining[(index + remaining.size() - 1U) %
                                            remaining.size()];
            const auto current = remaining[index];
            const auto next = remaining[(index + 1U) % remaining.size()];
            if (cross(points[previous], points[current], points[next]) <= 0.0F) {
                continue;
            }
            bool contains_point = false;
            for (const auto candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) {
                    continue;
                }
                if (point_in_triangle(points[candidate], points[previous],
                                      points[current], points[next])) {
                    contains_point = true;
                    break;
                }
            }
            if (contains_point) continue;
            result.insert(result.end(), {previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
            clipped = true;
            break;
        }
        if (!clipped) {
            result.clear();
            return result;
        }
    }
    result.insert(result.end(), {remaining[0], remaining[1], remaining[2]});
    return result;
}

std::vector<core::Vec2> flatten_shape(const project::VectorShape& shape,
                                      const float tolerance) {
    if (shape.kind == project::VectorShapeKind::rectangle) {
        return {{shape.bounds.origin.x, shape.bounds.origin.y},
                {shape.bounds.origin.x + shape.bounds.size.x,
                 shape.bounds.origin.y},
                {shape.bounds.origin.x + shape.bounds.size.x,
                 shape.bounds.origin.y + shape.bounds.size.y},
                {shape.bounds.origin.x, shape.bounds.origin.y + shape.bounds.size.y}};
    }
    if (shape.kind == project::VectorShapeKind::ellipse) {
        constexpr int segments = 64;
        std::vector<core::Vec2> points;
        points.reserve(segments);
        constexpr float pi = 3.14159265358979323846F;
        const core::Vec2 center{shape.bounds.origin.x + shape.bounds.size.x * 0.5F,
                                shape.bounds.origin.y + shape.bounds.size.y * 0.5F};
        for (int index = 0; index < segments; ++index) {
            const float angle = 2.0F * pi * static_cast<float>(index) / segments;
            points.push_back({center.x + std::cos(angle) * shape.bounds.size.x * 0.5F,
                              center.y + std::sin(angle) * shape.bounds.size.y * 0.5F});
        }
        return points;
    }
    if (shape.kind == project::VectorShapeKind::line) return shape.points;

    std::vector<core::Vec2> points;
    core::Vec2 current{};
    core::Vec2 first{};
    bool has_current = false;
    const int segments = std::max(4, static_cast<int>(std::ceil(1.0F / tolerance)));
    for (const auto& command : shape.path) {
        if (command.kind == project::VectorPathCommandKind::move) {
            current = first = command.point;
            has_current = true;
            points.push_back(current);
        } else if (command.kind == project::VectorPathCommandKind::line && has_current) {
            current = command.point;
            points.push_back(current);
        } else if (command.kind == project::VectorPathCommandKind::cubic && has_current) {
            const auto start = current;
            for (int index = 1; index <= segments; ++index) {
                const float t = static_cast<float>(index) /
                    static_cast<float>(segments);
                const float inverse = 1.0F - t;
                current = {
                    inverse * inverse * inverse * start.x +
                        3.0F * inverse * inverse * t * command.control1.x +
                        3.0F * inverse * t * t * command.control2.x +
                        t * t * t * command.point.x,
                    inverse * inverse * inverse * start.y +
                        3.0F * inverse * inverse * t * command.control1.y +
                        3.0F * inverse * t * t * command.control2.y +
                        t * t * t * command.point.y};
                points.push_back(current);
            }
        } else if (command.kind == project::VectorPathCommandKind::close &&
                   has_current) {
            current = first;
            points.push_back(current);
        }
    }
    if (points.size() > 1U && points.front() == points.back()) points.pop_back();
    return points;
}

} // namespace

VectorGeometryResult build_native_draw_packets(
    const project::VectorAsset& asset, const float curve_tolerance) {
    VectorGeometryResult result;
    if (asset.source_kind != project::VectorSourceKind::native ||
        !asset.native.has_value()) {
        result.errors.push_back("draw packets require native vector geometry");
        return result;
    }
    if (!std::isfinite(curve_tolerance) || curve_tolerance <= 0.0F) {
        result.errors.push_back("curve tolerance must be finite and positive");
        return result;
    }
    for (const auto& node : asset.native->nodes) {
        VectorDrawPacket packet{
            .node_id = node.id,
            .fill_color = node.fill.color,
            .stroke = node.stroke,
            .outline = flatten_shape(node.shape, curve_tolerance),
            .parent_id = node.parent_id,
            .clip_node_id = node.clip_node_id,
        };
        if (node.fill.kind == project::VectorFillKind::solid) {
            packet.fill_indices = triangulate(packet.outline);
            packet.fill_vertices = packet.outline;
            if (packet.outline.size() >= 3U && packet.fill_indices.empty()) {
                result.errors.push_back("native shape could not be triangulated: " +
                                       node.id);
            }
        }
        result.packets.push_back(std::move(packet));
    }
    return result;
}

} // namespace fabric::render
