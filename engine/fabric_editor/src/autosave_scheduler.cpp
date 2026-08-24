#include "fabric/editor/autosave_scheduler.hpp"

#include <algorithm>

namespace fabric::editor {

AutosaveScheduler::AutosaveScheduler(Duration idle_delay,
                                     Duration maximum_delay)
    : idle_delay_(std::max(Duration::zero(), idle_delay)),
      maximum_delay_(std::max(Duration::zero(), maximum_delay)) {}

void AutosaveScheduler::mark_changed(const Clock::time_point now) noexcept {
    if (!first_change_.has_value()) {
        first_change_ = now;
    }
    last_change_ = now;
}

bool AutosaveScheduler::due(const Clock::time_point now) const noexcept {
    if (!first_change_.has_value() || !last_change_.has_value()) {
        return false;
    }
    return now >= *last_change_ && now >= *first_change_ &&
           (now - *last_change_ >= idle_delay_ ||
            now - *first_change_ >= maximum_delay_);
}

bool AutosaveScheduler::pending() const noexcept {
    return first_change_.has_value();
}

void AutosaveScheduler::mark_saved() noexcept {
    reset();
}

void AutosaveScheduler::reset() noexcept {
    first_change_.reset();
    last_change_.reset();
}

} // namespace fabric::editor
