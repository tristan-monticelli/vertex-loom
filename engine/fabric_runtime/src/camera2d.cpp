#include "fabric/runtime/camera2d.hpp"

#include <algorithm>
#include <cmath>

namespace fabric::runtime {

void Camera2D::set_viewport(const std::int32_t width, const std::int32_t height) noexcept {
    viewport_width_ = std::max(width, 1);
    viewport_height_ = std::max(height, 1);
}

void Camera2D::set_position(const core::Vec2 position) noexcept {
    position_ = target_position_ = position;
}

void Camera2D::set_zoom(const float zoom) noexcept {
    target_zoom_ = std::clamp(zoom, 0.05F, 32.0F);
    zoom_ = target_zoom_;
}

void Camera2D::set_limits(const std::optional<core::Rect> limits) noexcept {
    if (limits && std::isfinite(limits->origin.x) && std::isfinite(limits->origin.y) &&
        std::isfinite(limits->size.x) && std::isfinite(limits->size.y) &&
        limits->size.x >= 0.0F && limits->size.y >= 0.0F) {
        limits_ = limits;
    } else {
        limits_.reset();
    }
    target_position_ = clamp_position(target_position_, target_zoom_);
    position_ = clamp_position(position_, zoom_);
}

void Camera2D::set_follow_target(const std::optional<core::Vec2> target) noexcept {
    follow_target_ = target;
}

void Camera2D::pan(const core::Vec2 delta) noexcept {
    target_position_.x += delta.x;
    target_position_.y += delta.y;
}

core::Vec2 Camera2D::screen_to_world(const core::Vec2 screen) const noexcept {
    return {position_.x + (screen.x - static_cast<float>(viewport_width_) * 0.5F) / zoom_,
            position_.y + (static_cast<float>(viewport_height_) * 0.5F - screen.y) / zoom_};
}

void Camera2D::zoom_at(const core::Vec2 screen, const float factor) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0F) return;
    const auto before = screen_to_world(screen);
    target_zoom_ = std::clamp(target_zoom_ * factor, 0.05F, 32.0F);
    target_position_ = {
        before.x - (screen.x - static_cast<float>(viewport_width_) * 0.5F) / target_zoom_,
        before.y - (static_cast<float>(viewport_height_) * 0.5F - screen.y) / target_zoom_};
}

void Camera2D::update(const float time_step) noexcept {
    if (!std::isfinite(time_step) || time_step <= 0.0F) return;
    if (follow_target_) target_position_ = *follow_target_;
    target_position_ = clamp_position(target_position_, target_zoom_);
    const auto blend = std::clamp(time_step * 12.0F, 0.0F, 1.0F);
    position_.x += (target_position_.x - position_.x) * blend;
    position_.y += (target_position_.y - position_.y) * blend;
    zoom_ += (target_zoom_ - zoom_) * blend;
    position_ = clamp_position(position_, zoom_);
}

core::Vec2 Camera2D::clamp_position(const core::Vec2 position,
                                     const float zoom) const noexcept {
    if (!limits_) return position;
    const auto half_width = static_cast<float>(viewport_width_) / (2.0F * zoom);
    const auto half_height = static_cast<float>(viewport_height_) / (2.0F * zoom);
    const auto min_x = limits_->origin.x + half_width;
    const auto max_x = limits_->origin.x + limits_->size.x - half_width;
    const auto min_y = limits_->origin.y + half_height;
    const auto max_y = limits_->origin.y + limits_->size.y - half_height;
    return {min_x > max_x ? limits_->origin.x + limits_->size.x * 0.5F
                          : std::clamp(position.x, min_x, max_x),
            min_y > max_y ? limits_->origin.y + limits_->size.y * 0.5F
                          : std::clamp(position.y, min_y, max_y)};
}

core::Rect Camera2D::world_bounds() const noexcept {
    const auto half_width = static_cast<float>(viewport_width_) / (2.0F * zoom_);
    const auto half_height = static_cast<float>(viewport_height_) / (2.0F * zoom_);
    return {{position_.x - half_width, position_.y - half_height},
            {half_width * 2.0F, half_height * 2.0F}};
}

} // namespace fabric::runtime
