#pragma once

#include <chrono>
#include <optional>

namespace fabric::editor {

enum class AutosaveStatus {
    not_due,
    saved,
    failed,
};

class AutosaveScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    explicit AutosaveScheduler(
        Duration idle_delay = std::chrono::seconds{2},
        Duration maximum_delay = std::chrono::seconds{30});

    void mark_changed(Clock::time_point now = Clock::now()) noexcept;
    [[nodiscard]] bool due(Clock::time_point now = Clock::now()) const noexcept;
    [[nodiscard]] bool pending() const noexcept;
    void mark_saved() noexcept;
    void reset() noexcept;

private:
    Duration idle_delay_;
    Duration maximum_delay_;
    std::optional<Clock::time_point> first_change_;
    std::optional<Clock::time_point> last_change_;
};

} // namespace fabric::editor
