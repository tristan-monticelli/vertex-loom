#pragma once

#include "fabric/core/types.hpp"

#include <cstdint>
#include <optional>

namespace fabric::runtime {

class Camera2D {
public:
    void set_viewport(std::int32_t width, std::int32_t height) noexcept;
    void set_position(core::Vec2 position) noexcept;
    void set_zoom(float zoom) noexcept;
    void set_limits(std::optional<core::Rect> limits) noexcept;
    void set_follow_target(std::optional<core::Vec2> target) noexcept;
    void pan(core::Vec2 delta) noexcept;
    void zoom_at(core::Vec2 screen_position, float factor) noexcept;
    void update(float time_step) noexcept;

    [[nodiscard]] core::Vec2 position() const noexcept { return position_; }
    [[nodiscard]] float zoom() const noexcept { return zoom_; }
    [[nodiscard]] std::optional<core::Rect> limits() const noexcept { return limits_; }
    [[nodiscard]] core::Rect world_bounds() const noexcept;
    [[nodiscard]] core::Vec2 screen_to_world(core::Vec2) const noexcept;

private:
    [[nodiscard]] core::Vec2 clamp_position(core::Vec2 position, float zoom) const noexcept;

    std::int32_t viewport_width_{1};
    std::int32_t viewport_height_{1};
    core::Vec2 position_{};
    core::Vec2 target_position_{};
    float zoom_{1.0F};
    float target_zoom_{1.0F};
    std::optional<core::Rect> limits_;
    std::optional<core::Vec2> follow_target_;
};

} // namespace fabric::runtime
