#include "fabric/project/mesh_deformation.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace fabric::project {
namespace {

void error(std::vector<Error>& errors, ErrorCode code, std::string field,
           std::string message) {
    errors.push_back({code, std::move(field), std::move(message)});
}

bool finite(core::Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(core::Transform value) {
    return finite(value.position) && std::isfinite(value.rotation_degrees) &&
        finite(value.scale) && finite(value.pivot);
}

core::Vec2 transform_point(core::Vec2 point, const core::Transform& transform) {
    constexpr float radians_per_degree = 0.01745329251994329577F;
    const float radians = transform.rotation_degrees * radians_per_degree;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const auto local = core::Vec2{
        (point.x - transform.pivot.x) * transform.scale.x + transform.pivot.x,
        (point.y - transform.pivot.y) * transform.scale.y + transform.pivot.y};
    return {local.x * cosine - local.y * sine + transform.position.x,
            local.x * sine + local.y * cosine + transform.position.y};
}

} // namespace

ValidationReport validate_deformation_mesh(const DeformationMesh& mesh) {
    ValidationReport report;
    for (std::size_t vertex_index = 0; vertex_index < mesh.vertices.size(); ++vertex_index) {
        const auto& vertex = mesh.vertices[vertex_index];
        if (!finite(vertex.rest_position) || vertex.influences.empty())
            error(report.errors, ErrorCode::invalid_asset, "vertices",
                  "vertices require finite positions and influences");
        float total_weight = 0.0F;
        for (const auto& influence : vertex.influences) {
            if (influence.node_id.empty() || !std::isfinite(influence.weight) ||
                influence.weight < 0.0F)
                error(report.errors, ErrorCode::invalid_asset, "influences",
                      "influences require non-negative finite weights and node ids");
            total_weight += influence.weight;
        }
        if (!std::isfinite(total_weight) || total_weight <= 0.0F)
            error(report.errors, ErrorCode::invalid_asset, "influences",
                  "influence weights must have a positive sum");
    }
    for (const auto& triangle : mesh.triangles) {
        if (triangle.first >= mesh.vertices.size() ||
            triangle.second >= mesh.vertices.size() ||
            triangle.third >= mesh.vertices.size() || triangle.first == triangle.second ||
            triangle.second == triangle.third || triangle.first == triangle.third)
            error(report.errors, ErrorCode::invalid_asset, "triangles",
                  "triangle indices must be distinct and in range");
    }
    return report;
}

MeshDeformationResult deform_mesh(const DeformationMesh& mesh,
                                  const std::vector<DeformationPose>& poses) {
    MeshDeformationResult result;
    const auto validation = validate_deformation_mesh(mesh);
    if (!validation.ok()) {
        result.errors = validation.errors;
        return result;
    }
    std::set<std::string> pose_ids;
    for (const auto& pose : poses) {
        if (pose.node_id.empty() || !pose_ids.insert(pose.node_id).second ||
            !finite(pose.transform))
            error(result.errors, ErrorCode::invalid_asset, "poses",
                  "poses require unique ids and finite transforms");
    }
    if (!result.errors.empty())
        return result;
    result.positions.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        core::Vec2 position{};
        float total_weight = 0.0F;
        for (const auto& influence : vertex.influences) {
            const auto pose = std::find_if(poses.begin(), poses.end(), [&](const auto& item) {
                return item.node_id == influence.node_id;
            });
            if (pose == poses.end()) {
                error(result.errors, ErrorCode::missing_resource, "poses",
                      "mesh influence references a missing pose");
                continue;
            }
            const auto transformed = transform_point(vertex.rest_position, pose->transform);
            position.x += transformed.x * influence.weight;
            position.y += transformed.y * influence.weight;
            total_weight += influence.weight;
        }
        if (total_weight > 0.0F) {
            position.x /= total_weight;
            position.y /= total_weight;
        }
        result.positions.push_back(position);
    }
    if (!result.errors.empty())
        result.positions.clear();
    return result;
}

} // namespace fabric::project
