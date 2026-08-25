#pragma once

#include "fabric/project/textured_path.hpp"
#include "fabric/render/vector_geometry.hpp"

namespace fabric::render {

[[nodiscard]] VectorGeometryResult build_textured_path_draw_packets(
    const project::TexturedPath& path, float curve_tolerance = 0.25F);

} // namespace fabric::render
