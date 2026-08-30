#pragma once

#include "fabric/project/replay.hpp"
#include "fabric/runtime/input.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fabric::runtime {

struct ReplayObservedState {
    std::string node_id;
    float x{};
    float y{};
    float rotation_turns{};
};

struct ReplayCheckpointVerification {
    std::size_t mismatches{};
    std::size_t corrected{};
    std::size_t missing{};
    [[nodiscard]] bool matched() const noexcept { return mismatches == 0U && missing == 0U; }
};

[[nodiscard]] ReplayCheckpointVerification verify_and_correct_checkpoint(
    const project::ReplayCheckpoint&, std::vector<ReplayObservedState>&) noexcept;

class ReplayPlayer {
public:
    explicit ReplayPlayer(const project::ReplayDocument& replay) noexcept : replay_(replay) {}

    // Advances a replay cursor and applies its logical actions to the input map.
    // Frames may be skipped, but they may never move backwards.
    [[nodiscard]] bool advance(std::uint64_t frame, InputActionMap& input) noexcept;
    [[nodiscard]] std::uint64_t seed() const noexcept { return replay_.seed; }
    [[nodiscard]] const std::vector<project::ReplayEvent>& events() const noexcept {
        return events_;
    }
    [[nodiscard]] const std::optional<project::ReplayCheckpoint>& checkpoint() const noexcept {
        return checkpoint_;
    }
    [[nodiscard]] std::optional<std::uint64_t> current_frame() const noexcept {
        return current_frame_;
    }

private:
    const project::ReplayDocument& replay_;
    std::optional<std::uint64_t> current_frame_;
    std::vector<project::ReplayEvent> events_;
    std::optional<project::ReplayCheckpoint> checkpoint_;
};

} // namespace fabric::runtime
