#pragma once

#include "fabric/core/types.hpp"
#include "fabric/project/map.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fabric::project {

struct IndexedMapInstance {
    std::string id;
    std::int32_t chunk_x{};
    std::int32_t chunk_y{};
    core::Vec2 position;
};

class MapChunkIndex {
public:
    [[nodiscard]] bool rebuild(const MapDocument&);
    [[nodiscard]] std::vector<std::string> query(const core::Rect& viewport) const;
    [[nodiscard]] static std::pair<std::int32_t, std::int32_t> chunk_for(core::Vec2 position);
    [[nodiscard]] const std::vector<IndexedMapInstance>& entries() const noexcept;

private:
    std::vector<IndexedMapInstance> entries_;
};

} // namespace fabric::project
