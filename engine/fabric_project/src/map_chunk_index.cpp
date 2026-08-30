#include "fabric/project/map_chunk_index.hpp"

#include <algorithm>
#include <cmath>

namespace fabric::project {

std::pair<std::int32_t, std::int32_t> MapChunkIndex::chunk_for(const core::Vec2 position) {
    return {static_cast<std::int32_t>(std::floor(position.x / map_chunk_size)),
            static_cast<std::int32_t>(std::floor(position.y / map_chunk_size))};
}

bool MapChunkIndex::rebuild(const MapDocument& map) {
    if (!validate_map(ProjectManifest{}, map).ok())
        return false;
    entries_.clear();
    entries_.reserve(map.instances.size());
    for (const auto& instance : map.instances)
        entries_.push_back({instance.id, instance.chunk_x, instance.chunk_y,
                            instance.transform.position});
    std::stable_sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
        if (left.chunk_y != right.chunk_y) return left.chunk_y < right.chunk_y;
        if (left.chunk_x != right.chunk_x) return left.chunk_x < right.chunk_x;
        return left.id < right.id;
    });
    return true;
}

std::vector<std::string> MapChunkIndex::query(const core::Rect& viewport) const {
    std::vector<std::string> result;
    if (viewport.size.x < 0.0F || viewport.size.y < 0.0F)
        return result;
    const auto minimum = chunk_for(viewport.origin);
    const auto maximum = chunk_for({viewport.origin.x + viewport.size.x,
                                    viewport.origin.y + viewport.size.y});
    for (const auto& entry : entries_) {
        if (entry.chunk_x < minimum.first || entry.chunk_x > maximum.first ||
            entry.chunk_y < minimum.second || entry.chunk_y > maximum.second)
            continue;
        if (entry.position.x < viewport.origin.x ||
            entry.position.y < viewport.origin.y ||
            entry.position.x > viewport.origin.x + viewport.size.x ||
            entry.position.y > viewport.origin.y + viewport.size.y)
            continue;
        result.push_back(entry.id);
    }
    return result;
}

const std::vector<IndexedMapInstance>& MapChunkIndex::entries() const noexcept {
    return entries_;
}

} // namespace fabric::project
