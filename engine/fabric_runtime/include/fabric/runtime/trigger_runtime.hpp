#pragma once

#include "fabric/project/map.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace fabric::runtime {

enum class GameplayEventKind { entered, exited };

struct GameplayEvent {
    core::ResourceId id;
    std::string trigger_id;
    std::string actor_id;
    std::vector<project::MapProperty> payload;
    GameplayEventKind kind{GameplayEventKind::entered};
};

struct TriggerActor {
    std::string id;
    core::Rect bounds;
};

class TriggerRuntime {
public:
    explicit TriggerRuntime(const project::MapDocument& map);

    void reset() noexcept;
    [[nodiscard]] std::vector<GameplayEvent> update(core::Vec2 position);
    [[nodiscard]] std::vector<GameplayEvent> update(
        const std::vector<TriggerActor>& actors);
    [[nodiscard]] std::size_t active_count() const noexcept;

private:
    [[nodiscard]] bool intersects(const project::CollisionShape& shape,
                                  const core::Rect& bounds) const noexcept;

    const project::MapDocument* map_{};
    std::vector<std::set<std::string>> active_;
};

} // namespace fabric::runtime
