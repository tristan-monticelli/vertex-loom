#include "fabric/editor/session_transition.hpp"

#include <utility>

namespace fabric::editor {

void SessionTransitionGuard::request(const SessionAction action,
                                     const bool dirty) noexcept {
    action_ = action;
    confirmation_required_ = dirty;
}

std::optional<SessionAction> SessionTransitionGuard::take_ready() noexcept {
    if (!action_ || confirmation_required_) {
        return std::nullopt;
    }
    auto ready = std::exchange(action_, std::nullopt);
    return ready;
}

std::optional<SessionAction> SessionTransitionGuard::resolve(
    const UnsavedDecision decision, const bool save_succeeded) noexcept {
    if (!action_ || !confirmation_required_) {
        return std::nullopt;
    }
    if (decision == UnsavedDecision::save && !save_succeeded) {
        return std::nullopt;
    }
    if (decision == UnsavedDecision::cancel) {
        action_.reset();
        confirmation_required_ = false;
        return std::nullopt;
    }
    confirmation_required_ = false;
    return take_ready();
}

bool SessionTransitionGuard::confirmation_required() const noexcept {
    return confirmation_required_;
}

bool SessionTransitionGuard::pending() const noexcept {
    return action_.has_value();
}

} // namespace fabric::editor
