#pragma once

#include "fabric/project/vector_asset.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fabric::render {

struct VectorDrawPacket {
    std::string node_id;
    std::optional<core::Color> fill_color;
    std::optional<project::VectorStroke> stroke;
    std::vector<core::Vec2> outline;
    std::vector<core::Vec2> fill_vertices;
    std::vector<std::uint32_t> fill_indices;
    std::optional<std::string> parent_id;
    std::optional<std::string> clip_node_id;

    friend bool operator==(const VectorDrawPacket&, const VectorDrawPacket&) = default;
};

struct VectorGeometryResult {
    std::vector<VectorDrawPacket> packets;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] VectorGeometryResult build_native_draw_packets(
    const project::VectorAsset& asset, float curve_tolerance = 0.25F);

class VectorGeometryCache {
public:
    [[nodiscard]] VectorGeometryResult get_or_build(
        const project::VectorAsset& asset, float curve_tolerance = 0.25F);
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, VectorGeometryResult> entries_;
};

} // namespace fabric::render
