#include "fabric/runtime/replay_player.hpp"

#include <algorithm>

namespace fabric::runtime {

ReplayCheckpointVerification verify_and_correct_checkpoint(
    const project::ReplayCheckpoint& checkpoint,
    std::vector<ReplayObservedState>& observed) noexcept {
    ReplayCheckpointVerification result;
    for (const auto& expected : checkpoint.states) {
        const auto found = std::find_if(observed.begin(), observed.end(),
            [&](const auto& value) { return value.node_id == expected.node_id; });
        if (found == observed.end()) {
            ++result.missing;
            continue;
        }
        const auto expected_x = project::dequantize_replay_position(expected.x);
        const auto expected_y = project::dequantize_replay_position(expected.y);
        const auto expected_rotation = project::dequantize_replay_rotation(expected.rotation);
        if (project::quantize_replay_position(found->x) != expected.x ||
            project::quantize_replay_position(found->y) != expected.y ||
            project::quantize_replay_rotation(found->rotation_turns) != expected.rotation)
            ++result.mismatches;
        if (found->x != expected_x || found->y != expected_y ||
            found->rotation_turns != expected_rotation) {
            found->x = expected_x;
            found->y = expected_y;
            found->rotation_turns = expected_rotation;
            ++result.corrected;
        }
    }
    return result;
}

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
