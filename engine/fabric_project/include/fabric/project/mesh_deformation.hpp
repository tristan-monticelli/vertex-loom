#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/manifest.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fabric::project {

struct MeshInfluence {
    std::string node_id;
    float weight{};
    friend bool operator==(const MeshInfluence&, const MeshInfluence&) = default;
};

struct MeshVertex {
    core::Vec2 rest_position;
    std::vector<MeshInfluence> influences;
    friend bool operator==(const MeshVertex&, const MeshVertex&) = default;
};

struct MeshTriangle {
    std::size_t first{};
    std::size_t second{};
    std::size_t third{};
    friend bool operator==(const MeshTriangle&, const MeshTriangle&) = default;
};

struct DeformationMesh {
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
    friend bool operator==(const DeformationMesh&, const DeformationMesh&) = default;
};

struct DeformationPose {
    std::string node_id;
    core::Transform transform;
};

struct MeshDeformationResult {
    std::vector<core::Vec2> positions;
    std::vector<Error> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] ValidationReport validate_deformation_mesh(const DeformationMesh&);
[[nodiscard]] MeshDeformationResult deform_mesh(
    const DeformationMesh&, const std::vector<DeformationPose>&);

} // namespace fabric::project
