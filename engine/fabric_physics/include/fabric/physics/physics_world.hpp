#pragma once

#include "fabric/core/types.hpp"

#include <cstddef>

namespace fabric::project { struct MapDocument; }

namespace fabric::physics {

class PhysicsWorld {
public:
    PhysicsWorld() noexcept = default;
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&& other) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&& other) noexcept;

    [[nodiscard]] bool create(float gravity_x = 0.0F, float gravity_y = -9.81F) noexcept;
    void destroy() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool step(float time_step, std::size_t sub_steps = 4) noexcept;
    [[nodiscard]] bool load_map_collisions(const project::MapDocument&) noexcept;
    [[nodiscard]] bool create_character(core::Vec2 position,
                                        core::Vec2 half_extents = {0.5F, 0.5F}) noexcept;
    void set_character_velocity(core::Vec2 velocity) noexcept;
    [[nodiscard]] core::Vec2 character_velocity() const noexcept;
    [[nodiscard]] core::Vec2 character_position() const noexcept;
    [[nodiscard]] bool character_valid() const noexcept;

private:
    struct Impl;
    Impl* impl_{};
};

} // namespace fabric::physics
