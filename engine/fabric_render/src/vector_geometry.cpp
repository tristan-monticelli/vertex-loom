#include "fabric/render/vector_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

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

core::Vec2 apply_transform(const core::Vec2 point,
                           const core::Transform& transform) {
    const float x = (point.x - transform.pivot.x) * transform.scale.x;
    const float y = (point.y - transform.pivot.y) * transform.scale.y;
    constexpr float pi = 3.14159265358979323846F;
    const float angle = transform.rotation_degrees * pi / 180.0F;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {transform.position.x + transform.pivot.x + x * cosine - y * sine,
            transform.position.y + transform.pivot.y + x * sine + y * cosine};
}

const project::VectorNode* find_node(
    const std::vector<project::VectorNode>& nodes, const std::string& id) {
    const auto iterator = std::ranges::find_if(
        nodes, [&id](const project::VectorNode& candidate) {
            return candidate.id == id;
        });
    return iterator == nodes.end() ? nullptr : &*iterator;
}

bool resolve_world_point(const std::vector<project::VectorNode>& nodes,
                         const project::VectorNode& node,
                         const core::Vec2 local,
                         std::unordered_set<std::string>& active,
                         core::Vec2& world,
                         std::string& error) {
    if (!active.insert(node.id).second) {
        error = "native transform hierarchy contains a cycle at node: " +
                node.id;
        return false;
    }
    const auto transformed = apply_transform(local, node.transform);
    if (!node.parent_id.has_value()) {
        world = transformed;
        active.erase(node.id);
        return true;
    }
    const auto* parent = find_node(nodes, *node.parent_id);
    if (parent == nullptr) {
        error = "native transform parent is missing: " + *node.parent_id;
        active.erase(node.id);
        return false;
    }
    const bool resolved = resolve_world_point(nodes, *parent, transformed,
                                              active, world, error);
    active.erase(node.id);
    return resolved;
}

bool transform_outline(const std::vector<project::VectorNode>& nodes,
                       const project::VectorNode& node,
                       std::vector<core::Vec2>& outline,
                       std::string& error) {
    std::vector<core::Vec2> transformed;
    transformed.reserve(outline.size());
    for (const auto point : outline) {
        std::unordered_set<std::string> active;
        core::Vec2 world{};
        if (!resolve_world_point(nodes, node, point, active, world, error)) {
            return false;
        }
        transformed.push_back(world);
    }
    outline = std::move(transformed);
    return true;
}

} // namespace

VectorGeometryResult build_raster_view_draw_packets(
    const RasterViewPacketInput& input) {
    VectorGeometryResult result;
    if (input.source_width == 0U || input.source_height == 0U) {
        result.errors.push_back("raster source dimensions must be positive");
        return result;
    }
    if (!std::isfinite(input.pixels_per_unit) || input.pixels_per_unit <= 0.0F) {
        result.errors.push_back("raster pixels per unit must be finite and positive");
        return result;
    }
    if (input.texture.expected_type != "texture" ||
        !core::ResourceId::is_valid(input.texture.id.value)) {
        result.errors.push_back("raster packet requires a valid texture reference");
        return result;
    }
    if (input.view) {
        const auto validation = project::validate_raster_view(
            *input.view, input.source_width, input.source_height);
        for (const auto& error : validation.errors) {
            result.errors.push_back(error.field + ": " + error.message);
        }
        if (!result.errors.empty()) return result;
    }

    const auto crop = input.view
        ? input.view->crop
        : core::Rect{{0.0F, 0.0F},
                     {static_cast<float>(input.source_width),
                      static_cast<float>(input.source_height)}};
    const auto pivot = input.view
        ? input.view->pivot : core::Vec2{0.5F, 0.5F};
    const float width = crop.size.x / input.pixels_per_unit;
    const float height = crop.size.y / input.pixels_per_unit;
    const float left = -width * pivot.x;
    const float right = width * (1.0F - pivot.x);
    const float top = -height * pivot.y;
    const float bottom = height * (1.0F - pivot.y);
    std::vector<core::Vec2> quad{
        {left, top}, {right, top}, {right, bottom}, {left, bottom}};
    if (input.view) {
        for (auto& point : quad) point = apply_transform(point, input.view->transform);
    }
    const auto uv_min = core::Vec2{
        crop.origin.x / static_cast<float>(input.source_width),
        crop.origin.y / static_cast<float>(input.source_height)};
    const auto uv_max = core::Vec2{
        (crop.origin.x + crop.size.x) / static_cast<float>(input.source_width),
        (crop.origin.y + crop.size.y) / static_cast<float>(input.source_height)};

    result.packets.push_back({
        .node_id = input.node_id,
        .image_fill = project::VectorImageFill{
            .texture = input.texture,
            .transform = input.view ? input.view->transform : core::Transform{}},
        .raster_filter = input.view
            ? input.view->filter : project::RasterFilter::linear,
        .outline = quad,
        .fill_vertices = quad,
        .fill_uv = {{uv_min.x, uv_min.y}, {uv_max.x, uv_min.y},
                    {uv_max.x, uv_max.y}, {uv_min.x, uv_max.y}},
        .fill_indices = {0U, 1U, 2U, 0U, 2U, 3U},
        .closed_outline = true,
    });
    return result;
}

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
    const auto& nodes = asset.native->nodes;
    for (const auto& node : asset.native->nodes) {
        const auto local_outline = flatten_shape(node.shape, curve_tolerance);
        VectorDrawPacket packet{
            .node_id = node.id,
            .fill_color = node.fill.color,
            .image_fill = node.fill.image,
            .stroke = node.stroke,
            .outline = local_outline,
            .parent_id = node.parent_id,
            .clip_node_id = node.clip_node_id,
            .closed_outline = node.shape.kind != project::VectorShapeKind::line &&
                (node.shape.kind != project::VectorShapeKind::path ||
                 (!node.shape.path.empty() &&
                  node.shape.path.back().kind ==
                      project::VectorPathCommandKind::close)),
        };
        std::string transform_error;
        if (!transform_outline(nodes, node, packet.outline, transform_error)) {
            result.errors.push_back(std::move(transform_error));
            continue;
        }
        if (node.fill.kind == project::VectorFillKind::solid ||
            node.fill.kind == project::VectorFillKind::image) {
            packet.fill_indices = triangulate(packet.outline);
            packet.fill_vertices = packet.outline;
            if (node.fill.kind == project::VectorFillKind::image) {
                const auto& bounds = node.shape.bounds;
                const float width = bounds.size.x;
                const float height = bounds.size.y;
                packet.fill_uv.reserve(local_outline.size());
                for (const auto point : local_outline) {
                    core::Vec2 uv{
                        (point.x - bounds.origin.x) / width,
                        (point.y - bounds.origin.y) / height};
                    uv = apply_transform(uv, node.fill.image->transform);
                    packet.fill_uv.push_back(uv);
                }
            }
            if (packet.outline.size() >= 3U && packet.fill_indices.empty()) {
                result.errors.push_back("native shape could not be triangulated: " +
                                       node.id);
            }
        }
        result.packets.push_back(std::move(packet));
    }
    return result;
}

VectorGeometryResult VectorGeometryCache::get_or_build(
    const project::VectorAsset& asset, const float curve_tolerance) {
    const std::string key = project::serialize_vector_asset(asset) +
                            "\ncurveTolerance=" +
                            std::to_string(curve_tolerance);
    const auto existing = entries_.find(key);
    if (existing != entries_.end()) return existing->second;
    auto [inserted, _] = entries_.emplace(
        key, build_native_draw_packets(asset, curve_tolerance));
    return inserted->second;
}

void VectorGeometryCache::clear() noexcept { entries_.clear(); }

std::size_t VectorGeometryCache::size() const noexcept { return entries_.size(); }

} // namespace fabric::render
