#pragma once

#include "fabric/project/map.hpp"

#include <cstddef>
#include <vector>

namespace fabric::runtime {

enum class GameplayEventKind { entered, exited };

struct GameplayEvent {
    core::ResourceId id;
    std::string trigger_id;
    std::vector<project::MapProperty> payload;
    GameplayEventKind kind{GameplayEventKind::entered};
};

class TriggerRuntime {
public:
    explicit TriggerRuntime(const project::MapDocument& map);

    void reset() noexcept;
    [[nodiscard]] std::vector<GameplayEvent> update(core::Vec2 position);
    [[nodiscard]] std::size_t active_count() const noexcept;

private:
    [[nodiscard]] bool contains(const project::CollisionShape& shape,
                                core::Vec2 position) const noexcept;

    const project::MapDocument* map_{};
    std::vector<bool> active_;
};

} // namespace fabric::runtime
