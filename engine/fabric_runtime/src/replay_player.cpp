#include "fabric/runtime/replay_player.hpp"

namespace fabric::runtime {

bool ReplayPlayer::advance(const std::uint64_t frame, InputActionMap& input) noexcept {
    if (current_frame_ && frame < *current_frame_) return false;
    const auto first_frame = current_frame_ ? *current_frame_ + 1U : 0U;
    input.begin_frame();
    events_.clear();
    checkpoint_.reset();
    for (const auto& value : replay_.inputs) {
        if (value.frame < first_frame || value.frame > frame) continue;
        if (value.pressed) input.press_action(value.action);
        if (value.released) input.release_action(value.action);
    }
    for (const auto& value : replay_.events) {
        if (value.frame >= first_frame && value.frame <= frame)
            events_.push_back(value);
    }
    for (const auto& value : replay_.checkpoints) {
        if (value.frame >= first_frame && value.frame <= frame)
            checkpoint_ = value;
    }
    current_frame_ = frame;
    return true;
}

} // namespace fabric::runtime
