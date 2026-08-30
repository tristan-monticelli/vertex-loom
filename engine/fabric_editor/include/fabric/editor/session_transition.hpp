#pragma once

#include <optional>

namespace fabric::editor {

enum class SessionAction {
    create_project,
    open_project,
    quit,
};

enum class UnsavedDecision {
    save,
    discard,
    cancel,
};

class SessionTransitionGuard {
public:
    void request(SessionAction action, bool dirty) noexcept;
    [[nodiscard]] std::optional<SessionAction> take_ready() noexcept;
    [[nodiscard]] std::optional<SessionAction> resolve(
        UnsavedDecision decision, bool save_succeeded = true) noexcept;
    [[nodiscard]] bool confirmation_required() const noexcept;
    [[nodiscard]] bool pending() const noexcept;

private:
    std::optional<SessionAction> action_;
    bool confirmation_required_{};
};

} // namespace fabric::editor
